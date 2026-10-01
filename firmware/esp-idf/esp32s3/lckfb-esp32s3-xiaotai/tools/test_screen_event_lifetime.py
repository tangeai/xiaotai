#!/usr/bin/env python3
"""Keep the long-lived screen callback outside repeated page rendering."""

from pathlib import Path

from repository import find_repository_root


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


root = Path(__file__).resolve().parents[1]
product_path = (
    find_repository_root(root)
    / "platforms/esp-idf/components/starter_product/src/starter_product.c"
)
product = product_path.read_text(encoding="utf-8")
render = function_body(product, "static void render_page(void)\n")
init = function_body(product, "static esp_err_t lvgl_init(void)\n")
registration = (
    "lv_obj_add_event_cb(lv_scr_act(), on_screen_event, LV_EVENT_ALL, NULL);"
)

assert registration not in render, "render_page must not register a screen callback"
assert init.count(registration) == 1, "lvgl_init must register exactly one screen callback"
assert init.index(registration) < init.index("render_page();"), (
    "screen callback must be installed once before the first page render"
)
assert "lv_obj_clean(screen);" in render, "page render must rebuild only screen children"

print("PASS: page rebuild keeps one long-lived screen event registration")
