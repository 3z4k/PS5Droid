// PS5Droid - launcher + bitmap font tests.
#include "test_framework.h"

#include <cstring>

#include "platform/display.h"
#include "ui/bitmap_font.h"
#include "ui/launcher.h"

using ps5droid::ui::LauncherAction;
using ps5droid::ui::LauncherModel;

PS5DROID_TEST(launcher_navigation_wraps) {
    LauncherModel m;
    int count = static_cast<int>(m.items().size());
    CHECK_MSG(count >= 5, "expected a real menu");
    CHECK_MSG(m.selected() == 0, "initial selection");

    m.select(count - 1);
    m.move_down();
    CHECK_MSG(m.selected() == 0, "wrap at bottom");
    m.move_up();
    CHECK_MSG(m.selected() == count - 1, "wrap at top");
}

PS5DROID_TEST(launcher_actions) {
    LauncherModel m;
    m.select(0);
    CHECK_MSG(m.activate() == LauncherAction::kShowStatus, "status action");
    m.select(1);
    CHECK_MSG(m.activate() == LauncherAction::kShowProbes, "probes action");
    m.select(2);
    CHECK_MSG(m.activate() == LauncherAction::kShowCompatMatrix, "matrix action");
    // select(-1) and out-of-range are ignored
    m.select(-3);
    CHECK_MSG(m.selected() >= 0, "select must ignore invalid index");
    // Exit item exists
    bool has_exit = false;
    for (const auto& item : m.items()) {
        has_exit |= item.action == LauncherAction::kExit;
    }
    CHECK_MSG(has_exit, "Exit item required");
}

PS5DROID_TEST(font_charset_integrity) {
    // Every glyph: 8 rows, reserved low bits must be zero, every glyph has
    // a sane ink pixel count.
    for (int c = 32; c < 127; ++c) {
        const uint8_t* g = ps5droid::ui::font8x8_glyph(static_cast<char>(c));
        if (g == nullptr) {
            // Only characters outside the authored set may be missing.
            bool allowed_missing = strchr("!\"#$&'*,-.;<>?@\\^`{|}~", c) != nullptr;
            CHECK_MSG(allowed_missing, "unexpected gap in charset");
            continue;
        }
        int ink = 0;
        for (int row = 0; row < 8; ++row) {
            CHECK_MSG((g[row] & 0x07) == 0, "reserved bits set");
            for (int col = 0; col < 8; ++col) {
                if ((g[row] << col) & 0x80)
                    ++ink;
            }
        }
        if (c == ' ') {
            CHECK_MSG(ink == 0, "space must be blank");
        } else if (strchr(".:-_", c) != nullptr) {
            CHECK_MSG(ink >= 1 && ink <= 6, "thin glyph shape wrong");
        } else {
            CHECK_MSG(ink >= 6 && ink <= 40, "glyph density out of range");
        }
    }
}

PS5DROID_TEST(font_render_bounds_and_ink) {
    ps5droid::platform::Framebuffer fb(128, 64);
    fb.clear({0, 0, 0, 255});

    uint32_t ink = ps5droid::ui::render_text(&fb, 0, 0, "PS5DROID 0.1", {255, 255, 255, 255}, 1);
    CHECK_MSG(ink > 100, "expected substantial ink");

    // Verify specific known pixels: '-' glyph row 3 has a full bar at
    // columns 0-4 of the glyph cell (bit 7..3).
    const uint8_t* dash = ps5droid::ui::font8x8_glyph('-');
    CHECK_MSG(dash != nullptr && dash[3] == 0xF8, "dash row 3 wrong");

    // Clipping: text drawn past the right edge must not crash and must
    // report fewer set pixels.
    uint32_t clipped = ps5droid::ui::render_text(&fb, 120, 0, "OVERFLOW", {255, 0, 0, 255}, 1);
    CHECK_MSG(clipped < ink, "clipped render must set fewer pixels");
}

PS5DROID_TEST(font_centered_math) {
    ps5droid::platform::Framebuffer fb(200, 32);
    CHECK_MSG(ps5droid::ui::text_pixel_width("ABC", 2) == 48, "width math");
    uint32_t ink = ps5droid::ui::render_text_centered(&fb, 8, "ABC", {255, 255, 255, 255}, 2);
    CHECK_MSG(ink > 0, "centered render produced no ink");
}
