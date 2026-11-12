# Replay how NaSkilla builds suggestion prompts on this PC.
# Default: print candidate counts and prompt sizes without calling Gemini.
# Pass --call to also POST a small request when the reader key is present.
import argparse
import json
import os
import random
import re
import sys
import urllib.error
import urllib.request
import zipfile

KEY_FILE = r"D:\.ai_api_key"
CFG_FILE = r"D:\.naskilla.cfg"
ROOTS = [r"D:\Library", r"D:\Books", r"D:\SendToPb", r"D:\SendToPocketBook", r"D:\Send to PocketBook"]
EXTS = (".epub", ".fb2", ".fb2.zip", ".txt", ".html", ".htm", ".rtf", ".pdf")
URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-lite-latest:generateContent?key="
HEAD = '{"contents":[{"parts":[{"text":"'
TAIL = '"}]}]}'
SAMPLE_MAX = 40
GENERIC = {
    "Library", "_Categories", "Books", "SendToPb", "SendToPocketBook",
    "Send to PocketBook", "system", "applications",
}


def json_escape(src):
    out = []
    for ch in src:
        o = ord(ch)
        if ch in ('"', '\\'):
            out.append('\\')
            out.append(ch)
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\r' or o < 32:
            continue
        else:
            out.append(ch)
    return "".join(out)


def is_book(name):
    low = name.lower()
    return low.endswith(EXTS)


def strip_ext(name):
    low = name.lower()
    for ext in (".fb2.zip", ".epub", ".fb2", ".txt", ".html", ".htm", ".rtf", ".pdf"):
        if low.endswith(ext):
            return name[: -len(ext)]
    return name


def generic_dir(name):
    if not name or name[0] in "._":
        return True
    return name in GENERIC


def small_number(s):
    if not s or not s.isdigit():
        return 0
    v = int(s)
    return v if 1 <= v <= 200 else 0


def strip_volume(name):
    s = name.strip()
    series = ""
    index = 0
    m = re.search(r"\(([^)]*#\s*(\d+)|(\d+))\)$", s)
    if m:
        index = int(m.group(2) or m.group(3))
        left = s[: m.start()].rstrip()
        series_m = re.match(r"^(.*?)(?:\s+#?\d+)?$", left)
        series = (series_m.group(1) if series_m else left).strip()
        title = left
        return title, series, index
    m = re.search(r"(?:\s+#(\d+)|\s+(\d{1,3}))$", s)
    if m:
        index = int(m.group(1) or m.group(2))
        if index < 1 or index > 200:
            return s, "", 0
        left = s[: m.start()].rstrip()
        words = left.split()
        if len(words) < 2 and not m.group(1):
            return s, "", 0
        return left, left, index
    return s, "", 0


def parse_name(path):
    base = strip_ext(os.path.basename(path))
    parent = os.path.basename(os.path.dirname(path))
    grand = os.path.basename(os.path.dirname(os.path.dirname(path)))
    title = ""
    author = ""
    series = ""
    index = 0
    parts = [p.strip() for p in base.split(" - ") if p.strip()]
    if len(parts) >= 3 and (small_number(parts[1]) or strip_volume(parts[1])[2]):
        author = parts[0]
        title = parts[2]
        vol_title, vol_series, vol = strip_volume(parts[1])
        index = small_number(parts[1]) or vol
        series = vol_series or author
    elif len(parts) >= 2 and small_number(parts[0]):
        title = parts[1]
        index = small_number(parts[0])
    elif len(parts) >= 2:
        left, left_series, left_vol = strip_volume(parts[0])
        if left_vol:
            title = parts[1]
            series = left_series
            index = left_vol
            author = parts[1] and parts[-1] if False else parts[1]
            # Title - Author with volume on the left side of the dash.
            title = left if left else parts[0]
            author = parts[1]
            series = left_series
            index = left_vol
            # Prefer right side as title when left was "Series 01".
            if left_series:
                title = parts[1]
                author = ""
        else:
            title = parts[0]
            author = parts[1]
    else:
        title, series, index = strip_volume(base if parts else base)
        if not title:
            title = base
    if not author and parent and not generic_dir(parent) and parent.lower() != title.lower():
        author = parent
    if not title:
        title = base
    return {
        "path": path,
        "title": title,
        "author": author,
        "series": series,
        "index": index,
    }


def looks_like_hash(name):
    if not name or len(name) < 8 or len(name) > 48:
        return False
    digit = False
    for ch in name:
        if ch.isdigit():
            digit = True
        elif not (("A" <= ch <= "Z") or ("a" <= ch <= "z") or ch in "_-"):
            return False
    return digit


def parse_opf_text(data):
    def grab(tag):
        m = re.search(r"<(?:dc:)?" + tag + r"[^>]*>([^<]+)", data, re.I)
        return m.group(1).strip() if m else ""

    title = grab("title")
    author = grab("creator")
    series = ""
    index = 0
    year = 0
    for m in re.finditer(r"<meta[^>]+>", data, re.I):
        tag = m.group(0)
        nm = re.search(r'name=["\']([^"\']+)["\']', tag, re.I)
        ct = re.search(r'content=["\']([^"\']+)["\']', tag, re.I)
        if not nm or not ct:
            continue
        name = nm.group(1).lower()
        content = ct.group(1)
        if name in ("calibre:series", "series"):
            series = content
        if name in ("calibre:series_index", "series_index"):
            try:
                index = int(float(content))
            except Exception:
                index = 0
    m = re.search(r"<(?:dc:)?date[^>]*>([^<]+)", data, re.I)
    if m:
        try:
            year = int(re.match(r"(\d{4})", m.group(1).strip()).group(1))
        except Exception:
            year = 0
    return {"title": title, "author": author, "series": series, "index": index, "year": year}


def sidecar_meta(path):
    folder = os.path.dirname(path)
    opf = os.path.join(folder, "metadata.opf")
    if not os.path.isfile(opf):
        return None
    try:
        with open(opf, "r", encoding="utf-8", errors="replace") as fh:
            return parse_opf_text(fh.read())
    except Exception:
        return None


def epub_meta(path):
    try:
        with zipfile.ZipFile(path) as z:
            opf = [n for n in z.namelist() if n.lower().endswith(".opf")]
            if not opf:
                return None
            data = z.read(opf[0]).decode("utf-8", "replace")
    except Exception:
        return None
    return parse_opf_text(data)


def enrich_book(book):
    base = strip_ext(os.path.basename(book["path"]))
    is_hash = looks_like_hash(base)
    book.setdefault("year", 0)
    side = sidecar_meta(book["path"])
    if side:
        if side["title"] and not book.get("title"):
            book["title"] = side["title"]
        if side["author"] and not book.get("author"):
            book["author"] = side["author"]
        if side["series"] and not book.get("series"):
            book["series"] = side["series"]
        if side["index"] > 0 and not book.get("index"):
            book["index"] = side["index"]
        if side.get("year", 0) >= 1000 and not book.get("year"):
            book["year"] = side["year"]
    if not is_hash and book.get("title") and book.get("author"):
        return book
    if not book["path"].lower().endswith(".epub"):
        return book
    meta = epub_meta(book["path"])
    if not meta:
        return book
    if meta["title"] and (is_hash or not book.get("title") or looks_like_hash(book.get("title") or "")):
        book["title"] = meta["title"]
    if meta["author"] and (is_hash or not book.get("author")):
        book["author"] = meta["author"]
    if meta["series"] and (is_hash or not book.get("series")):
        book["series"] = meta["series"]
    if meta["index"] > 0 and (is_hash or not book.get("index")):
        book["index"] = meta["index"]
    if meta.get("year", 0) >= 1000 and not book.get("year"):
        book["year"] = meta["year"]
    return book


def author_words(s):
    words = []
    cur = []
    for ch in s or "":
        o = ord(ch)
        if (
            ("a" <= ch <= "z")
            or ("A" <= ch <= "Z")
            or ("0" <= ch <= "9")
            or o >= 128
        ):
            cur.append(ch.lower() if "A" <= ch <= "Z" else ch)
        else:
            if cur:
                words.append("".join(cur))
                cur = []
    if cur:
        words.append("".join(cur))
    return words


SERIES_STOP = {"series", "seria", "cykl", "cycle", "the", "a", "an"}


def collect_tokens(s, for_series=False):
    out = []
    for w in author_words(s):
        if len(w) <= 1:
            continue
        if for_series and (w in SERIES_STOP or w.isdigit()):
            continue
        out.append(w)
    return out


def edit_distance_at_most_1(a, b):
    if a is None or b is None:
        return False
    if len(a) > len(b):
        a, b = b, a
    if len(b) - len(a) > 1:
        return False
    if len(a) == len(b):
        return sum(1 for x, y in zip(a, b) if x != y) <= 1
    i = j = 0
    while i < len(a) and j < len(b):
        if a[i] == b[j]:
            i += 1
            j += 1
        else:
            j += 1
            if j - i > 1:
                return False
    return True


def words_match_exact(wa, wb):
    if not wa or len(wa) != len(wb):
        return False
    used = [False] * len(wb)
    for word in wa:
        found = False
        for i, other in enumerate(wb):
            if used[i]:
                continue
            if word == other:
                used[i] = True
                found = True
                break
        if not found:
            return False
    return True


def words_match_near(wa, wb):
    if not wa or not wb:
        return False
    if len(wa) <= len(wb):
        shorter, longer = wa, wb
    else:
        shorter, longer = wb, wa
    if len(shorter) < 2 and not (len(wa) == 1 and len(wb) == 1):
        return False
    used = [False] * len(longer)
    for word in shorter:
        found = False
        for i, other in enumerate(longer):
            if used[i]:
                continue
            if word == other or (
                len(word) >= 5
                and len(other) >= 5
                and edit_distance_at_most_1(word, other)
            ):
                used[i] = True
                found = True
                break
        if not found:
            return False
    return True


def person_match(a, b):
    wa = collect_tokens(a, for_series=False)
    wb = collect_tokens(b, for_series=False)
    if not wa or not wb:
        return 0
    if words_match_exact(wa, wb):
        return 2
    if words_match_near(wa, wb):
        return 1
    return 0


def series_match(a, b):
    wa = collect_tokens(a, for_series=True)
    wb = collect_tokens(b, for_series=True)
    if not wa or not wb:
        return 0
    if words_match_exact(wa, wb):
        return 2
    if words_match_near(wa, wb):
        return 1
    return 0


def author_words_match(a, b):
    return person_match(a, b) == 2


def series_names_match(a, b):
    return series_match(a, b) == 2


def is_calibre_title_dir(name):
    if not name or not name.endswith(")"):
        return False
    op = name.rfind("(")
    if op <= 0 or name[op - 1] != " ":
        return False
    digits = name[op + 1 : -1]
    return bool(digits) and digits.isdigit()


def folder_author_from_path(path):
    parent = os.path.basename(os.path.dirname(path))
    grand = os.path.basename(os.path.dirname(os.path.dirname(path)))
    if is_calibre_title_dir(parent) and grand and not generic_dir(grand):
        # Strip trailing " (id)" for the returned grand? grand is author folder.
        return grand
    # Mirror C: parent after strip_calibre_id
    cleaned = parent
    if is_calibre_title_dir(parent):
        cleaned = parent[: parent.rfind(" (")].rstrip()
    if cleaned and not generic_dir(cleaned):
        return cleaned
    return ""


def collect_books():
    books = []
    seen = set()
    for root in ROOTS:
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if not d.startswith(".")]
            for fn in filenames:
                if not is_book(fn):
                    continue
                path = os.path.join(dirpath, fn)
                if path in seen:
                    continue
                seen.add(path)
                book = enrich_book(parse_name(path))
                book["author_alt"] = folder_author_from_path(path)
                book["hashed"] = looks_like_hash(strip_ext(os.path.basename(path)))
                books.append(book)
                if len(books) >= 8000:
                    return books
    return books


def device_path_to_windows(path):
    if not path:
        return path
    p = path.replace("/", "\\")
    if p.startswith("\\mnt\\ext1\\"):
        p = "D:\\" + p[len("\\mnt\\ext1\\") :]
    elif p.startswith("/mnt/ext1/"):
        p = "D:/" + p[len("/mnt/ext1/") :]
        p = p.replace("/", "\\")
    return p


def load_recent():
    recent = []
    if not os.path.isfile(CFG_FILE):
        return recent
    with open(CFG_FILE, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line.startswith("recent."):
                continue
            try:
                idx_s, path = line.split("=", 1)
                idx = int(idx_s.split(".", 1)[1])
            except Exception:
                continue
            while len(recent) <= idx:
                recent.append("")
            recent[idx] = device_path_to_windows(path.strip())
    return [p for p in recent if p]


def score_book(book, current):
    series = current.get("series") or ""
    author = current.get("author") or ""
    cur_index = current.get("index") or 0
    best = 0
    if series and book.get("series"):
        s = series_match(book["series"], series)
        if s > 0:
            if book.get("index", 0) > 0 and cur_index > 0 and book["index"] <= cur_index:
                s = 0
            else:
                best = max(best, s)
    if author:
        best = max(
            best,
            person_match(book.get("author") or "", author),
            person_match(book.get("author_alt") or "", author),
        )
    book["tier"] = best
    return best


def sort_series_key(b):
    idx = b.get("index") or 0
    year = b.get("year") or 0
    tier = b.get("tier") or 0
    return (
        -tier,
        idx if idx > 0 else 100000,
        year if year >= 1000 else 100000,
        (b.get("title") or "").lower(),
    )


def select_series(books, current, keep_unmatched_hash=True):
    series = current.get("series") or ""
    author = current.get("author") or ""
    if not series and not author:
        return []
    out = []
    for b in books:
        score = score_book(b, current)
        hashed = b.get("hashed") or looks_like_hash(
            strip_ext(os.path.basename(b.get("path") or ""))
        )
        if score > 0 or (keep_unmatched_hash and hashed):
            out.append(b)
    return out


def filter_series(books, current):
    """Finalize: re-score, drop non-matches, rank, cap at SAMPLE_MAX."""
    scored = []
    for b in books:
        if score_book(b, current) > 0:
            scored.append(b)
    scored.sort(key=sort_series_key)
    return scored[:SAMPLE_MAX]


def sample_random(books, want=SAMPLE_MAX):
    pool = list(books)
    random.shuffle(pool)
    return pool[: min(want, len(pool))]


def ask_for_mode(mode):
    if mode == 0:
        return (
            "Continue the series. List the next unread books in reading "
            "order from the first of the series to the last. Skip books "
            "already in the recent list. Write the series position in the "
            "reason, for example 3rd of James Bond. Some entries may have "
            "misspelled names. Pick books that belong to the same author "
            "or series as the current book, even if a name is slightly "
            "different."
        )
    if mode == 1:
        return "Suggest books with a similar theme or the same kind of story."
    if mode == 2:
        return "Suggest something different from the current book."
    return (
        "Suggest what to read next. Use the reading history and choose "
        "from the numbered list of books on this reader."
    )


def format_list(books, mode=None):
    lines = []
    for i, b in enumerate(books, 1):
        if mode == 0 and b.get("series") and b.get("index") and b.get("author"):
            lines.append(
                "%d. %s — %s — %s #%d"
                % (i, b["title"], b["author"], b["series"], b["index"])
            )
        elif mode == 0 and b.get("series") and b.get("index"):
            lines.append(
                "%d. %s — %s #%d" % (i, b["title"], b["series"], b["index"])
            )
        elif b.get("author"):
            lines.append("%d. %s — %s" % (i, b["title"], b["author"]))
        else:
            lines.append("%d. %s" % (i, b["title"]))
    return "\n".join(lines)


def format_recent(paths):
    lines = []
    for i, path in enumerate(paths, 1):
        b = enrich_book(parse_name(path))
        if b["author"]:
            lines.append("%d. %s — %s" % (i, b["title"], b["author"]))
        else:
            lines.append("%d. %s" % (i, b["title"]))
    return "\n".join(lines)


def build_prompt(mode, current, books, recent_paths):
    cur = "%s%s%s" % (
        current["title"] or "(none open)",
        " — " if current.get("author") else "",
        current.get("author") or "",
    )
    body = (
        "You recommend books that are already on this e-reader. "
        "Choose only from the numbered list. Do not invent titles.\n\n"
        "Request: %s\n"
        "Current book: %s"
    ) % (ask_for_mode(mode), cur)
    if (mode == 0 or mode == 3) and recent_paths:
        body += "\n\nRecently opened (newest first):\n" + format_recent(recent_paths)
    if mode == 0:
        end = (
            "\n\nReply with at most 5 lines in reading order, next unread "
            "volume first. Each line starts with the list number, then a "
            "vertical bar, then the position and a short reason.\n"
            "Example: 4 | 3rd of James Bond\n"
            "If none fit, reply NONE.\n"
        )
    else:
        end = (
            "\n\nReply with at most 5 lines. Each line starts with the list number, "
            "then a vertical bar, then a short reason.\n"
            "Example: 4 | Next in the series\n"
            "If none fit, reply NONE.\n"
        )
    body += "\n\nBooks on this reader:\n" + format_list(books, mode=mode) + end
    return body


def wrap(prompt):
    return (HEAD + json_escape(prompt) + TAIL).encode("utf-8")


def pick_current(books, recent):
    by_path = {b["path"].lower(): b for b in books}
    for path in recent:
        key = path.lower()
        if key in by_path:
            return by_path[key]
        b = enrich_book(parse_name(path))
        if b.get("title"):
            return b
    for b in books:
        title = (b.get("title") or "").lower()
        if "doktor no" in title or "doctor no" in title:
            return b
    for b in books:
        if b.get("author") and not looks_like_hash(b.get("title") or ""):
            return b
    return books[0] if books else {"title": "", "author": "", "series": "", "index": 0, "path": ""}


def post(key, payload):
    req = urllib.request.Request(
        URL + key,
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            body = resp.read()
            code = resp.status
    except urllib.error.HTTPError as exc:
        body = exc.read()
        code = exc.code
    except Exception as exc:
        return 0, str(exc).replace(key, "<key>")
    return code, body.decode("utf-8", errors="replace")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--call", action="store_true", help="Also call Gemini with a 5-book sample")
    args = parser.parse_args()

    books = collect_books()
    recent = load_recent()
    current = pick_current(books, recent)
    skip = current.get("path")
    catalog = [b for b in books if b["path"] != skip]
    print("books", len(books), "catalog_without_open", len(catalog), "recent", len(recent))
    print(
        "current",
        current.get("title"),
        "|",
        current.get("author"),
        "| series=",
        current.get("series") or "-",
        "index=",
        current.get("index"),
    )

    modes = [
        (0, "Continue a series", filter_series(catalog, current)),
        (1, "Same theme", sample_random(catalog, SAMPLE_MAX)),
        (2, "Something different", sample_random(catalog, SAMPLE_MAX)),
        (3, "Suggest next", catalog),
    ]
    for mode, label, chosen in modes:
        prompt = build_prompt(mode, current, chosen, recent if mode == 3 else [])
        payload = wrap(prompt)
        try:
            json.loads(payload.decode("utf-8"))
            ok = "ok"
        except json.JSONDecodeError as exc:
            ok = "FAIL %s" % exc
        print(
            "mode",
            mode,
            label,
            "candidates",
            len(chosen),
            "prompt_chars",
            len(prompt),
            "payload_bytes",
            len(payload),
            "json",
            ok,
        )
        if mode == 0:
            for b in chosen[:8]:
                print(
                    "  series sample",
                    b.get("index") or 0,
                    (b.get("title") or "")[:50],
                    "|",
                    (b.get("author") or "")[:30],
                )
            if current.get("author") and "fleming" in current.get("author", "").lower():
                print("  fleming_list", len(chosen))
        if mode == 3 and recent:
            print("  recent first", enrich_book(parse_name(recent[0]))["title"][:60])

    if not args.call:
        return 0
    if not os.path.isfile(KEY_FILE):
        print("no key file, skip --call")
        return 0
    key = open(KEY_FILE, "r", encoding="utf-8", errors="replace").read().strip()
    sample = wrap(build_prompt(1, current, catalog[:5], []))
    code, text = post(key, sample)
    print("--- sample call HTTP", code)
    try:
        msg = json.loads(text).get("error", {}).get("message") or text
        print(msg[:400])
    except Exception:
        print(text[:400])
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    sys.exit(main())
