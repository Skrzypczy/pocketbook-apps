#!/usr/bin/env python3
"""Host checks for bar hit zones and clamp/rename behavior."""
import os
import tempfile


def clamp_int(v, lo, hi):
    return max(lo, min(hi, v))


def bar_compute(w, h, height=64):
    slot = w // 4
    return {
        "w": w,
        "h": h,
        "height": height,
        "day_x": slot,
        "day_w": slot,
        "light_x": slot * 2,
        "light_w": slot,
        "pages_x": slot * 3,
        "pages_w": w - slot * 3,
    }


def bar_hit(b, x, y):
    if y < 0 or y >= b["height"] or x < 0 or x >= b["w"]:
        return "NONE"
    if b["day_x"] <= x < b["day_x"] + b["day_w"]:
        return "DAYNIGHT"
    if b["light_x"] <= x < b["light_x"] + b["light_w"]:
        return "LIGHT"
    if b["pages_x"] <= x < b["pages_x"] + b["pages_w"]:
        return "PAGES"
    return "NONE"


def test_clamp():
    assert clamp_int(5, 16, 72) == 16
    assert clamp_int(100, 16, 72) == 72
    assert clamp_int(32, 16, 72) == 32
    print("clamp OK")


def test_bar():
    b = bar_compute(1264, 1680)
    assert bar_hit(b, 10, 10) == "NONE"
    assert bar_hit(b, 400, 10) == "DAYNIGHT"
    assert bar_hit(b, 700, 10) == "LIGHT"
    assert bar_hit(b, 1100, 10) == "PAGES"
    assert bar_hit(b, -1, 10) == "NONE"
    b2 = bar_compute(1680, 1264)
    assert b2["pages_x"] + b2["pages_w"] == 1680
    assert bar_hit(b2, 500, 10) == "DAYNIGHT"
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


if __name__ == "__main__":
    test_clamp()
    test_bar()
    test_rename()
    print("all host tests passed")
