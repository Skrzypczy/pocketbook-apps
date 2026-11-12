#!/usr/bin/env python3
"""Host checks for bar hit zones and clamp/rename behavior."""
import os
import tempfile


def clamp_int(v, lo, hi):
    return max(lo, min(hi, v))


def bar_compute(w, h, height=64):
    unit = w // 6
    return {
        "w": w,
        "h": h,
        "height": height,
        "clock_x": 0,
        "clock_w": unit,
        "pages_x": unit,
        "pages_w": unit,
        "options_x": unit * 2,
        "options_w": unit,
        "day_x": unit * 3,
        "day_w": unit,
        "light_x": unit * 4,
        "light_w": unit,
        "sleep_x": unit * 5,
        "sleep_w": w - unit * 5,
    }


def bar_hit(b, x, y):
    if y < 0 or y >= b["height"] or x < 0 or x >= b["w"]:
        return "NONE"
    if b["options_x"] <= x < b["options_x"] + b["options_w"]:
        return "OPTIONS"
    if b["pages_x"] <= x < b["pages_x"] + b["pages_w"]:
        return "PAGES"
    if b["day_x"] <= x < b["day_x"] + b["day_w"]:
        return "DAYNIGHT"
    if b["light_x"] <= x < b["light_x"] + b["light_w"]:
        return "LIGHT"
    if b["sleep_x"] <= x < b["sleep_x"] + b["sleep_w"]:
        return "SLEEP"
    return "NONE"


def test_clamp():
    assert clamp_int(5, 16, 72) == 16
    assert clamp_int(100, 16, 72) == 72
    assert clamp_int(32, 16, 72) == 32
    print("clamp OK")


def test_bar():
    b = bar_compute(1264, 1680)
    # Six equal columns: time | pages | options | night | light | sleep
    assert b["clock_w"] == b["pages_w"] == b["options_w"] == b["day_w"] == b["light_w"]
    assert b["clock_x"] < b["pages_x"] < b["options_x"] < b["day_x"] < b["light_x"] < b["sleep_x"]
    assert bar_hit(b, 10, 10) == "NONE"  # clock
    assert bar_hit(b, b["pages_x"] + 5, 10) == "PAGES"
    assert bar_hit(b, b["options_x"] + 5, 10) == "OPTIONS"
    assert bar_hit(b, b["day_x"] + 10, 10) == "DAYNIGHT"
    assert bar_hit(b, b["light_x"] + 10, 10) == "LIGHT"
    assert bar_hit(b, b["sleep_x"] + 10, 10) == "SLEEP"
    assert b["sleep_x"] + b["sleep_w"] == 1264
    assert bar_hit(b, -1, 10) == "NONE"
    b2 = bar_compute(1680, 1264)
    assert b2["sleep_x"] + b2["sleep_w"] == 1680
    assert b2["clock_x"] < b2["pages_x"] < b2["options_x"] < b2["day_x"] < b2["sleep_x"]
    print("bar hits OK")


def test_rename():
    with tempfile.TemporaryDirectory() as d:
        tmp = os.path.join(d, "cfg.tmp")
        dst = os.path.join(d, "cfg")
        with open(tmp, "w", encoding="utf-8") as f:
            f.write("font_size=32\npos./mnt/ext1/Books/a.epub=xpointer\n")
        os.replace(tmp, dst)
        with open(dst, encoding="utf-8") as f:
            text = f.read()
        assert "pos." in text
    print("atomic rename OK")


def parse_debug_line(line, state):
    """Mirror of debug.c apply_debug_kv for host checks."""
    line = line.strip()
    if not line or line.startswith("#") or "=" not in line:
        return state
    key, _, val = line.partition("=")
    key = key.strip()
    val = val.strip()
    if key == "render":
        state["render"] = val
    elif key == "draw_probe":
        state["draw_probe"] = int(val)
    elif key == "crash_test":
        state["crash_test"] = int(val)
    return state


def test_debug_cfg():
    st = {"render": "auto", "draw_probe": 6, "crash_test": 0}
    st = parse_debug_line("render=text", st)
    assert st["render"] == "text"
    st = parse_debug_line("draw_probe=2", st)
    assert st["draw_probe"] == 2
    st = parse_debug_line("# comment", st)
    st = parse_debug_line("crash_test=1", st)
    assert st["crash_test"] == 1
    print("debug.cfg OK")


def test_sentinel():
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "draw_inflight")
        with open(path, "w", encoding="utf-8") as f:
            f.write("sha=test\n")
        assert os.path.exists(path)
        os.remove(path)
        assert not os.path.exists(path)
    print("sentinel OK")


# Mirrors apps/Reader/extensions.c managed list + promote/rewrite rules.
_MANAGED = [
    ("epub", "@EPUB_file", "ICON_EPUB", "eink-reader_with_epub2.app"),
    ("fb2.zip", "@FB2_file", "ICON_FB2", "eink-reader.app"),
    ("fb2", "@FB2_file", "ICON_FB2", "eink-reader.app"),
    ("txt", "@Text_file", "ICON_TXT", "eink-reader.app"),
    ("html", "@HTML_file", "ICON_HTML", "eink-reader.app"),
    ("htm", "@HTML_file", "ICON_HTM", "eink-reader.app"),
    ("rtf", "@RTF_file", "ICON_RTF", "eink-reader.app"),
    ("pdf", "@PDF_file", "ICON_PDF", "eink-reader_with_rmsdk.app"),
]


def _is_naskilla(h: str) -> bool:
    if h == "Reader.app":
        return True
    hl = h.lower()
    return hl in ("naskilla reader.app", "naskilla.app") or "naskilla reader.app" in hl


def _is_stock_fixed(h: str) -> bool:
    hl = h.lower()
    return hl.startswith("eink-reader") or h == "reader.app"


def _find_managed(line: str):
    for ext, typ, icon, stock in _MANAGED:
        if line.startswith(ext + ":"):
            return ext, typ, icon, stock
    return None


def _promote(handlers, preferred, stock):
    if _is_naskilla(preferred):
        promoted = "Reader.app"
    elif _is_stock_fixed(preferred):
        promoted = stock
    else:
        promoted = preferred
    out = [promoted]
    for h in handlers:
        if _is_naskilla(h):
            if not any(_is_naskilla(x) for x in out):
                out.append("Reader.app")
            continue
        if h == promoted:
            continue
        out.append(h)
    if not _is_naskilla(promoted) and not any(_is_naskilla(x) for x in out):
        out.append("Reader.app")
    return out


def extcfg_rewrite_text(in_text: str, preferred: str) -> str:
    """Python mirror of extensions.c::extcfg_rewrite_text (no device access checks)."""
    seen = {m[0]: False for m in _MANAGED}
    lines_out = []
    for raw in (in_text or "").splitlines():
        line = raw.rstrip("\r")
        m = _find_managed(line)
        if not m:
            if line or raw.endswith("\n"):
                lines_out.append(line)
            continue
        ext, typ, icon, stock = m
        seen[ext] = True
        parts = line.split(":")
        if len(parts) >= 4:
            handlers = [h for h in parts[3].split(",") if h.strip()]
            handlers = _promote(handlers, preferred, stock)
            parts[3] = ",".join(handlers)
            lines_out.append(":".join(parts[:5] if len(parts) >= 5 else parts[:4]))
        else:
            handlers = _promote(["Reader.app", stock], preferred, stock)
            lines_out.append(f"{ext}:{typ}:1:{','.join(handlers)}:{icon}")
    for ext, typ, icon, stock in _MANAGED:
        if seen[ext]:
            continue
        handlers = _promote(["Reader.app", stock], preferred, stock)
        lines_out.append(f"{ext}:{typ}:1:{','.join(handlers)}:{icon}")
    return "\n".join(lines_out) + ("\n" if lines_out else "")


def _first_handlers(text: str):
    out = {}
    for line in text.splitlines():
        m = _find_managed(line)
        if not m:
            continue
        parts = line.split(":")
        if len(parts) >= 4 and parts[3]:
            out[m[0]] = parts[3].split(",")[0]
    return out


def test_extcfg_rewrite():
    sample = (
        "epub:@EPUB_file:1:eink-reader_with_epub2.app,koreader.app:ICON_EPUB\n"
        "fb2:@FB2_file:1:eink-reader.app:ICON_FB2\n"
        "fb2.zip:@FB2_file:1:eink-reader.app:ICON_FB2\n"
        "jpg:@JPEG_image:2:picviewer.app:ICON_JPG\n"
    )
    # Prefer NaSkilla — Reader.app first; fb2.zip matched as its own line.
    text = extcfg_rewrite_text(sample, "Reader.app")
    first = _first_handlers(text)
    assert first["epub"] == "Reader.app"
    assert first["fb2"] == "Reader.app"
    assert first["fb2.zip"] == "Reader.app"
    assert "jpg:@JPEG_image:2:picviewer.app:ICON_JPG" in text
    assert "koreader.app" in text

    # Prefer stock — epub uses epub2 stock name; pdf line created with rmsdk.
    text2 = extcfg_rewrite_text(sample, "eink-reader_with_epub2.app")
    first2 = _first_handlers(text2)
    assert first2["epub"] == "eink-reader_with_epub2.app"
    assert first2["pdf"] == "eink-reader_with_rmsdk.app"
    assert "Reader.app" in text2

    # Prefer KOReader; already-first stays first when promoted again.
    text3 = extcfg_rewrite_text(text2, "koreader.app")
    first3 = _first_handlers(text3)
    assert first3["epub"] == "koreader.app"
    text4 = extcfg_rewrite_text(text3, "koreader.app")
    assert _first_handlers(text4)["epub"] == "koreader.app"

    # Empty input creates all managed lines.
    text5 = extcfg_rewrite_text("", "Reader.app")
    first5 = _first_handlers(text5)
    assert first5["epub"] == "Reader.app"
    assert first5["fb2.zip"] == "Reader.app"
    assert first5["pdf"] == "Reader.app"
    print("extcfg rewrite OK")


if __name__ == "__main__":
    test_clamp()
    test_bar()
    test_rename()
    test_debug_cfg()
    test_sentinel()
    test_extcfg_rewrite()
    print("all host tests passed")
