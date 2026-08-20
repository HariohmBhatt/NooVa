#!/usr/bin/env python3
"""Audit Variant D's rendered symbol contract against every bounded fixture."""

from playwright.sync_api import sync_playwright


BASE_URL = "http://127.0.0.1:4173/"
FIXTURES = (
    "healthy", "warning", "critical", "stale", "server-offline",
    "wifi-offline", "monitor-error", "setup", "max-fresh", "max-offline",
    "nulls",
)
FORBIDDEN_UI_SYMBOLS = set("✓×—?↻←→")


def tokens(page, selector: str) -> list[str]:
    return [text.strip() for text in page.locator(selector).all_inner_texts()]


with sync_playwright() as playwright:
    browser = playwright.chromium.launch()
    page = browser.new_page(viewport={"width": 720, "height": 620})

    for fixture in FIXTURES:
        for details in (False, True):
            suffix = "&page=details" if details else ""
            page.goto(f"{BASE_URL}?variant=D&fixture={fixture}{suffix}")
            rendered = page.locator("#device").inner_text()
            forbidden = sorted(set(rendered) & FORBIDDEN_UI_SYMBOLS)
            assert not forbidden, (fixture, details, forbidden)
            # The bounded audit fixtures intentionally use ASCII protocol copy.
            # A future UTF-8 protocol-copy fixture should whitelist that fixture
            # field explicitly rather than weakening the UI-symbol assertions.
            assert rendered.isascii(), (fixture, details, rendered)

            # Protocol strings may be UTF-8. These selectors cover only UI-owned
            # formatter/glyph output and therefore must remain strictly ASCII.
            selectors = (
                ".hybrid-glyph", ".hybrid-small-glyph", ".service-tile i",
                ".hybrid-node strong", ".compact-metrics b",
                ".hybrid-service-title b", "[data-page]", ".scroll-cue",
            )
            ui_tokens = [token for selector in selectors for token in tokens(page, selector)]
            assert all(token.isascii() for token in ui_tokens), (fixture, details, ui_tokens)
            assert set(tokens(page, ".service-tile i")) <= {"OK", "!", "X", "-"}
            assert set(tokens(page, ".hybrid-node strong")) <= {"OK", "X", "-"}
            if fixture == "setup" and not details:
                assert tokens(page, ".compact-metrics b") == ["Unavailable"] * 3
                assert tokens(page, ".hybrid-service-title b") == ["NO CHECKS"]
            if fixture == "nulls":
                assert tokens(page, ".metric-label strong")[:2] == ["Unavailable", "Unavailable"]

            hero = tokens(page, ".hybrid-glyph, .hybrid-small-glyph")
            expected = {
                "healthy": "OK", "warning": "!", "critical": "X",
                "stale": "", "server-offline": "X", "wifi-offline": "X",
                "monitor-error": "!", "setup": "+", "max-fresh": "X",
                "max-offline": "X", "nulls": "!",
            }[fixture]
            assert hero == [expected], (fixture, details, hero)

    browser.close()

print("Variant D symbol audit passed: 11 fixtures x Home/Details")
