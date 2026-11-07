# Replay the NaSkilla Gemini suggestion request on this PC.
# Reads the key from the reader and never prints it.
import json
import os
import sys
import urllib.error
import urllib.request

KEY_FILE = r"D:\.ai_api_key"
ROOTS = [r"D:\Library", r"D:\Books", r"D:\SendToPb", r"D:\SendToPocketBook", r"D:\Send to PocketBook"]
EXTS = (".epub", ".fb2", ".fb2.zip", ".txt", ".html", ".htm", ".rtf", ".pdf")
URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key="
HEAD = '{"contents":[{"parts":[{"text":"'
TAIL = '"}]}],"generationConfig":{"thinkingConfig":{"thinkingBudget":0}}}'
TAIL_PLAIN = '"}]}]}'


def json_escape(src):
    """Same rules as suggest.c json_escape, over Unicode code points."""
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


def load_key():
    with open(KEY_FILE, "r", encoding="utf-8", errors="replace") as fh:
        key = fh.read().strip()
    if len(key) < 20 or key.upper().startswith("YOUR"):
        raise SystemExit("API key file is missing or still a placeholder")
    return key


def collect_names():
    names = []
    odd = []
    for root in ROOTS:
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if not d.startswith(".")]
            for fn in filenames:
                if not is_book(fn):
                    continue
                base = fn
                for ext in (".fb2.zip", ".epub", ".fb2", ".txt", ".html", ".htm", ".rtf", ".pdf"):
                    if base.lower().endswith(ext):
                        base = base[: -len(ext)]
                        break
                if len(names) < 8000:
                    names.append(base if base else fn)
                for ch in base:
                    o = ord(ch)
                    if ch in '"\\' or o < 32 or o in (0x2028, 0x2029) or o > 0xFFFF:
                        odd.append((base[:80], hex(o)))
                        break
    return names, odd


def build_prompt(names):
    lines = []
    for i, name in enumerate(names, 1):
        lines.append("%d. %s" % (i, name))
    body = (
        "You recommend books that are already on this e-reader. "
        "Choose only from the numbered list. Do not invent titles.\n\n"
        "Request: Continue the series. Pick the next books in that series.\n"
        "Current book: Casino Royale — Ian Fleming\n\n"
        "Books on this reader:\n"
        + "\n".join(lines)
        + "\n\nReply with at most 5 lines. Each line starts with the list number, "
        "then a vertical bar, then a short reason.\n"
        "Example: 4 | Next in the series\n"
        "If none fit, reply NONE.\n"
    )
    return body


def wrap(prompt, tail):
    raw = HEAD + json_escape(prompt) + tail
    return raw.encode("utf-8")


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
        msg = str(exc).replace(key, "<key>")
        return 0, msg
    text = body.decode("utf-8", errors="replace")
    return code, text


def reply_text(doc):
    best = ""
    for cand in doc.get("candidates") or []:
        content = cand.get("content") or {}
        for part in content.get("parts") or []:
            if part.get("thought"):
                continue
            piece = part.get("text") or ""
            if piece.strip():
                best = piece
    return best


def show(label, code, text):
    print("---", label, "HTTP", code, "bytes", len(text))
    try:
        doc = json.loads(text)
    except json.JSONDecodeError:
        print(text[:800])
        return
    if "error" in doc:
        msg = (doc["error"].get("message") or "")[:600]
        print(msg)
        return
    got = reply_text(doc)
    print(got[:800] if got else "(no text part)")


def main():
    key = load_key()
    names, odd = collect_names()
    print("books", len(names), "odd_names", len(odd))
    for sample in odd[:8]:
        print(" odd", sample[1], sample[0])
    prompt = build_prompt(names)
    full = wrap(prompt, TAIL)
    plain = wrap(prompt, TAIL_PLAIN)
    print("payload_bytes", len(full), "plain_bytes", len(plain))
    try:
        json.loads(full.decode("utf-8"))
        print("local_json", "ok")
    except json.JSONDecodeError as exc:
        print("local_json", "FAIL", exc)
        return 1
    small = wrap(build_prompt(names[:5] if names else ["Casino Royale"]), TAIL)
    code, text = post(key, small)
    show("sample-5 thinkingConfig", code, text)
    if code != 200:
        code2, text2 = post(key, wrap(build_prompt(names[:5] if names else ["Casino Royale"]), TAIL_PLAIN))
        show("sample-5 no thinkingConfig", code2, text2)
    code, text = post(key, full)
    show("full-library thinkingConfig", code, text)
    if code != 200:
        code2, text2 = post(key, plain)
        show("full-library no thinkingConfig", code2, text2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
