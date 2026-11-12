# Local checks for Continue-a-series matching (fuzzy person/series + filter).
# Uses D:\ when the reader is plugged in; otherwise runs fixture-only tests.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_suggest_payload as t

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

PASS = 0
FAIL = 0


def check(label, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print("PASS", label)
    else:
        FAIL += 1
        print("FAIL", label, detail)


def with_defaults(book):
    b = dict(book)
    b.setdefault("author_alt", "")
    b.setdefault("hashed", False)
    b.setdefault("year", 0)
    b.setdefault("tier", 0)
    return b


def test_author_words():
    cases = [
        ("Ian Fleming", "Ian Fleming", True),
        ("Ian Fleming", "Fleming Ian", True),
        ("Fleming, Ian", "Ian Fleming", True),
        ("Fleming Ian", "Ian Fleming", True),
        ("James S. A. Corey", "Corey James S. A.", True),
        ("James S. A. Corey", "James S.A. Corey", True),
        ("Ian Fleming", "Ian Rankin", False),
        ("Ian Fleming", "", False),
        ("Bond Michael", "Michael Bond", True),
        ("Katarzyna Bonda", "Bonda Katarzyna", True),
        ("Antologia S.F.", "Antologia SF", False),
    ]
    print("--- author_words_match (exact) ---")
    for a, b, expect in cases:
        got = t.author_words_match(a, b)
        check("%r ~ %r -> %s" % (a, b, expect), got == expect, "got %s" % got)


def test_hash_basename():
    print("--- hashed basename without extension ---")
    # Mirrors cat_add: detect hash from basename without .epub
    name = "jk5ksdO3PotJLQb8bjpgoE.epub"
    base = t.strip_ext(name)
    check("hash without ext", t.looks_like_hash(base), "base=%s" % base)
    check("hash with .epub is NOT a hash", not t.looks_like_hash(name), name)
    # Select must keep unmatched hashes before enrich
    books = [
        with_defaults(
            {
                "path": r"D:\SendToPb\jk5ksdO3PotJLQb8bjpgoE.epub",
                "title": "jk5ksdO3PotJLQb8bjpgoE",
                "author": "",
                "hashed": True,
            }
        ),
        with_defaults(
            {
                "path": r"D:\SendToPb\jk5ksdO3PotJLQb8bjpgoE.epub.bad",
                "title": "jk5ksdO3PotJLQb8bjpgoE.epub",  # bug: title kept extension
                "author": "",
                "hashed": False,
            }
        ),
    ]
    current = with_defaults(
        {"title": "Zyj", "author": "Ian Fleming", "series": "", "index": 0}
    )
    selected = t.select_series(books, current, keep_unmatched_hash=True)
    check(
        "select keeps hashed SendToPb before enrich",
        any(b.get("hashed") for b in selected),
        "got %s" % [(b["title"], b.get("hashed")) for b in selected],
    )


def test_person_match_near():
    print("--- person_match near / reject ---")
    cases = [
        ("Ian Flemming", "Fleming Ian", 1),
        ("Ian Fleming", "Fleming Ian", 2),
        ("James Corey", "James S. A. Corey", 2),  # initials dropped
        ("James Corey", "Corey James S. A.", 2),
        ("Michael Bond", "Katarzyna Bonda", 0),
        ("Michael Bond", "Michael Jackson", 0),
        ("Fleming", "Flemming", 1),
        ("Bond", "Bonda", 0),  # length 4 < 5, no edit-distance
    ]
    for a, b, expect in cases:
        got = t.person_match(a, b)
        check("person %r ~ %r -> %s" % (a, b, expect), got == expect, "got %s" % got)


def fixture_catalog():
    """Named library-style books plus hashed SendToPb-style Fleming set."""
    books = [
        {"path": r"D:\Library\a\Casino Royale - Ian Fleming.epub",
         "title": "Casino Royale", "author": "Ian Fleming", "series": "", "index": 0},
        {"path": r"D:\Library\a\Goldfinger - Ian Fleming.epub",
         "title": "Goldfinger", "author": "Ian Fleming", "series": "", "index": 0},
        {"path": r"D:\Library\a\Moonraker - Fleming Ian.epub",
         "title": "Moonraker", "author": "Fleming Ian", "series": "James Bond", "index": 3},
        {"path": r"D:\Library\a\Doktor No - Ian Fleming.epub",
         "title": "Doktor No", "author": "Ian Fleming", "series": "", "index": 0},
        {"path": r"D:\Library\b\James Bond 01 - Casino Royale.epub",
         "title": "Casino Royale", "author": "", "series": "James Bond", "index": 1},
        {"path": r"D:\Library\b\James Bond 02 - Live and Let Die.epub",
         "title": "Live and Let Die", "author": "", "series": "James Bond", "index": 2},
        {"path": r"D:\Library\b\James Bond 04 - Diamonds Are Forever.epub",
         "title": "Diamonds Are Forever", "author": "", "series": "James Bond", "index": 4},
        {"path": r"D:\Library\c\01. Leviathan Wakes - James S. A. Corey.epub",
         "title": "Leviathan Wakes", "author": "James S. A. Corey", "series": "", "index": 1},
        {"path": r"D:\Library\c\02. Caliban's War - James S. A. Corey.epub",
         "title": "Caliban's War", "author": "James S. A. Corey", "series": "", "index": 2},
        {"path": r"D:\Library\c\03. Abaddon's Gate - Corey James S. A..epub",
         "title": "Abaddon's Gate", "author": "Corey James S. A.", "series": "", "index": 3},
        {"path": r"D:\Library\d\Paddington - Bond Michael.epub",
         "title": "Paddington", "author": "Bond Michael", "series": "", "index": 0},
        {"path": r"D:\Library\d\Paddington abroad - Michael Bond.epub",
         "title": "Paddington abroad", "author": "Michael Bond", "series": "", "index": 0},
        {"path": r"D:\Library\e\Do przerwy 0-1 - Bahdaj Adam.epub",
         "title": "Do przerwy 0-1", "author": "Bahdaj Adam", "series": "", "index": 0},
        {"path": r"D:\Library\e\Gdzie twoj dom Telemachu - Bahdaj Adam.epub",
         "title": "Gdzie twoj dom Telemachu", "author": "Bahdaj Adam", "series": "", "index": 0},
        {"path": r"D:\Library\f\Pride and Prejudice - Jane Austen.epub",
         "title": "Pride and Prejudice", "author": "Jane Austen", "series": "", "index": 0},
        # Typo in file name; correct author in Calibre folder
        {"path": r"D:\Library\_Categories\Thriller\Fleming Ian\Moonraker Typo (99)\Moonraker Typo - Flemming Ian.epub",
         "title": "Moonraker Typo", "author": "Flemming Ian",
         "author_alt": "Fleming Ian", "series": "", "index": 0},
        # Hashed SendToPb-style
        {"path": r"D:\SendToPb\jk5ksdO3PotJLQb8bjpgoE.epub",
         "title": "jk5ksdO3PotJLQb8bjpgoE", "author": "", "series": "", "index": 0,
         "hashed": True},
    ]
    return [with_defaults(b) for b in books]


def test_series_normalize():
    print("--- series_match ---")
    cases = [
        ("James Bond", "James Bond", 2),
        ("James Bond", "James Bond series", 2),
        ("James Bond series", "James Bond", 2),
        ("James Bond", "james-bond", 2),
        # "jams" is only 4 letters; edit-distance-1 requires length >= 5
        ("James Bond", "Jams Bond", 0),
        ("James Bond", "Jamess Bond", 1),
        ("James Bond", "Bond", 0),
        ("Miś zwany Paddington", "Mis zwany Paddington", 0),
    ]
    for a, b, expect in cases:
        got = t.series_match(a, b)
        check("%r ~ %r -> %s" % (a, b, expect), got == expect, "got %s" % got)


def test_fixtures():
    print("--- fixture series / author filter ---")
    catalog = fixture_catalog()

    # Typo open author still finds Fleming peers via near match
    current = with_defaults({
        "path": r"D:\Library\a\Zyj - Ian Flemming.epub",
        "title": "Zyj i pozwol umrzec",
        "author": "Ian Flemming",
        "series": "",
        "index": 0,
    })
    others = [b for b in catalog if b["path"] != current["path"]]
    got = t.filter_series(others, current)
    titles = [b["title"] for b in got]
    check("typo Flemming -> includes Moonraker", "Moonraker" in titles, "got %s" % titles)
    if "Moonraker" in titles:
        check(
            "typo Flemming -> Moonraker #3 sorts ahead of unnumbered",
            titles.index("Moonraker") < titles.index("Casino Royale"),
            "order=%s" % titles,
        )

    # Folder author_alt corrects a bad file-name author
    current = with_defaults({
        "path": r"D:\Library\a\open.epub",
        "title": "Open",
        "author": "Ian Fleming",
        "series": "",
        "index": 0,
    })
    got = t.filter_series(others, current)
    check(
        "author_alt Flemming Ian matched via folder",
        any(b["title"] == "Moonraker Typo" for b in got),
        "got %s" % [b["title"] for b in got],
    )

    # Middle initials: James Corey finds James S. A. Corey peers
    current = with_defaults({
        "path": r"D:\x\open.epub",
        "title": "Open",
        "author": "James Corey",
        "series": "",
        "index": 0,
    })
    got = t.filter_series(catalog, current)
    check(
        "James Corey -> S.A. Corey peers",
        sorted(b["title"] for b in got)
        == sorted(["Leviathan Wakes", "Caliban's War", "Abaddon's Gate"]),
        "got %s" % [b["title"] for b in got],
    )

    # No first-name false positives
    current = with_defaults({
        "path": r"D:\Library\d\Paddington - Bond Michael.epub",
        "title": "Paddington",
        "author": "Bond Michael",
        "series": "",
        "index": 0,
    })
    others = [b for b in catalog if b["path"] != current["path"]]
    got = t.filter_series(others, current)
    check(
        "Michael Bond does not pull Bonda or strangers",
        len(got) == 1 and got[0]["title"] == "Paddington abroad",
        "got %s" % [b["title"] for b in got],
    )

    # Overflow: many hashed + many exact; exact Moonraker survives cap
    fat = list(catalog)
    for i in range(100):
        fat.append(
            with_defaults(
                {
                    "path": r"D:\SendToPb\hash%02dabcdefghijklmnop.epub" % i,
                    "title": "hash%02dabcdefghijklmnop" % i,
                    "author": "",
                    "hashed": True,
                }
            )
        )
    for i in range(50):
        fat.append(
            with_defaults(
                {
                    "path": r"D:\Library\a\Extra %d - Ian Fleming.epub" % i,
                    "title": "Extra %d" % i,
                    "author": "Ian Fleming",
                    "year": 1950 + i,
                }
            )
        )
    current = with_defaults({
        "path": r"D:\Library\a\Doktor No - Ian Fleming.epub",
        "title": "Doktor No",
        "author": "Ian Fleming",
        "series": "",
        "index": 0,
    })
    # select keeps hashes; finalize after "enrich" drops unmatched hashes
    selected = t.select_series(
        [b for b in fat if b["path"] != current["path"]], current, keep_unmatched_hash=True
    )
    # Simulate enrich filling only the real Moonraker hash
    for b in selected:
        if b["title"].startswith("jk5"):
            b["title"] = "Moonraker"
            b["author"] = "Fleming Ian"
            b["series"] = "James Bond"
            b["index"] = 3
            b["hashed"] = True
    got = t.filter_series(selected, current)
    check("overflow keeps at most 40", len(got) <= 40, "got %d" % len(got))
    check(
        "overflow keeps Moonraker among exact",
        any(b["title"] == "Moonraker" for b in got),
        "got %s" % [b["title"] for b in got[:8]],
    )
    check(
        "overflow prefers exact over unmatched near noise",
        all((b.get("tier") or 0) >= 1 for b in got),
        "tiers=%s" % [b.get("tier") for b in got[:5]],
    )

    # Open Żyj: Moonraker #3 in payload
    current = with_defaults({
        "path": r"D:\Library\a\Zyj i pozwol umrzec - Ian Fleming.epub",
        "title": "Zyj i pozwol umrzec",
        "author": "Ian Fleming",
        "series": "",
        "index": 0,
    })
    others = [b for b in catalog if b["path"] != current["path"]]
    got = t.filter_series(others, current)
    titles = [b["title"] for b in got]
    check("Zyj -> includes Moonraker", "Moonraker" in titles, "got %s" % titles)
    moon = next(b for b in got if b["title"] == "Moonraker")
    check("Moonraker keeps #3", moon["index"] == 3, "index=%s" % moon["index"])
    payload_line = t.format_list([moon], mode=0)
    check(
        "payload line has James Bond #3",
        "James Bond #3" in payload_line,
        "line=%s" % payload_line,
    )
    prompt = t.build_prompt(0, current, got, [])
    check(
        "prompt asks for reading order and misspellings",
        "reading order" in prompt
        and "3rd of James Bond" in prompt
        and "misspelled" in prompt,
        "prompt missing instructions",
    )

    # Open Doktor No
    current = with_defaults({
        "path": r"D:\Library\a\Doktor No - Ian Fleming.epub",
        "title": "Doktor No",
        "author": "Ian Fleming",
        "series": "",
        "index": 0,
    })
    others = [b for b in catalog if b["path"] != current["path"]]
    got = t.filter_series(others, current)
    titles = sorted(b["title"] for b in got)
    check(
        "Doktor No includes Fleming peers",
        "Moonraker" in titles and "Casino Royale" in titles,
        "got %s" % titles,
    )

    # Series string differs by the word "series"
    current = with_defaults({
        "path": r"D:\Library\b\James Bond 02 - Live and Let Die.epub",
        "title": "Live and Let Die",
        "author": "",
        "series": "James Bond series",
        "index": 2,
    })
    others = [b for b in catalog if b["path"] != current["path"]]
    got = t.filter_series(others, current)
    titles = [b["title"] for b in got]
    check(
        "James Bond series #2 -> later volumes",
        "Moonraker" in titles and "Diamonds Are Forever" in titles,
        "got %s" % titles,
    )
    check("Bond #2 does not return earlier Casino vol1", titles.count("Casino Royale") == 0
          or all(b.get("index", 0) != 1 for b in got if b["title"] == "Casino Royale"),
          "got %s" % titles)

    # Bahdaj / Austen
    current = with_defaults({
        "path": r"D:\Library\e\Do przerwy 0-1 - Bahdaj Adam.epub",
        "title": "Do przerwy 0-1",
        "author": "Bahdaj Adam",
        "series": "",
        "index": 0,
    })
    others = [b for b in catalog if b["path"] != current["path"]]
    got = t.filter_series(others, current)
    check(
        "Bahdaj Adam -> one peer",
        len(got) == 1 and "Telemachu" in got[0]["title"],
        "got %s" % [b["title"] for b in got],
    )

    current = with_defaults({
        "path": r"D:\Library\f\Pride and Prejudice - Jane Austen.epub",
        "title": "Pride and Prejudice",
        "author": "Jane Austen",
        "series": "",
        "index": 0,
    })
    others = [b for b in catalog if b["path"] != current["path"]]
    got = t.filter_series(others, current)
    check("Jane Austen alone -> empty", got == [], "got %s" % [b["title"] for b in got])


def test_device_library():
    print("--- device library ---")
    if not os.path.isdir(r"D:\Library") and not os.path.isdir(r"D:\SendToPb"):
        print("SKIP device not connected")
        return
    books = t.collect_books()
    print("books", len(books))

    # Payload size for Continue a series on current/recent Fleming book
    recent = t.load_recent()
    current = t.pick_current(books, recent)
    others = [b for b in books if b["path"] != current["path"]]
    selected = t.select_series(others, current, keep_unmatched_hash=True)
    # Device books are already enriched in collect_books
    got = t.filter_series(selected, current)
    prompt = t.build_prompt(0, current, got, recent)
    payload = t.wrap(prompt)
    check(
        "device mode 0 payload under 3KB",
        len(payload) < 3000,
        "bytes=%d candidates=%d" % (len(payload), len(got)),
    )
    print(
        "  current",
        (current.get("title") or "")[:40],
        "|",
        (current.get("author") or "")[:30],
        "candidates",
        len(got),
        "payload",
        len(payload),
    )
    for b in got[:6]:
        print(
            "  ",
            (b.get("title") or "")[:40],
            "|",
            (b.get("author") or "")[:24],
            "| tier",
            b.get("tier"),
            "| #",
            b.get("index"),
        )

    doktor = [
        b
        for b in books
        if "doktor no" in (b.get("title") or "").lower()
        or "doctor no" in (b.get("title") or "").lower()
    ]
    if doktor:
        current = doktor[0]
        others = [b for b in books if b["path"] != current["path"]]
        got = t.filter_series(others, current)
        check(
            "device Doktor No finds Fleming peers incl Moonraker",
            any(b["title"] == "Moonraker" for b in got) and len(got) >= 5,
            "got %d titles=%s" % (len(got), [b["title"][:30] for b in got[:6]]),
        )


def main():
    test_author_words()
    test_hash_basename()
    test_person_match_near()
    test_series_normalize()
    test_fixtures()
    test_device_library()
    print("---")
    print("passed", PASS, "failed", FAIL)
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
