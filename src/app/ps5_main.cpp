// PS5Droid - PS5 payload entry point (native Orbis homebrew).
//
// STATUS: implemented against API usage verified in ps5-payload-dev/sdk
// (sce_stubs + samples) and ps5-payload-dev/SDL, but NEVER EXECUTED ON A
// CONSOLE. It is compiled only when the ps5-payload-sdk toolchain drives
// the build (PS5DROID_PLATFORM_PS5). Until a console run is documented,
// this binary must be treated as experimental and untested.
//
// What it does:
//   1. hides the splash screen,
//   2. initializes a 1920x1080 double-buffered video-out surface and the
//      DualSense pad,
//   3. runs the platform capability probes and shows the results in a
//      pad-navigable launcher screen,
//   4. exits cleanly (releasing direct memory) on Exit/Options.
#include <cstdio>
#include <cstring>

#include "android/runtime_manager.h"
#include "platform/display.h"
#include "platform/platform.h"
#include "platform/ps5/sce_decls.h"
#include "ui/bitmap_font.h"
#include "ui/launcher.h"

using namespace ps5droid;

namespace {

const platform::Rgba kBackground{16, 18, 26, 255};
const platform::Rgba kTitleColor{90, 200, 250, 255};
const platform::Rgba kTextColor{220, 225, 235, 255};
const platform::Rgba kSelectColor{255, 210, 80, 255};

constexpr int kMaxLines = 14;

void show_screen(platform::Framebuffer* fb, const char* title, const char* const* lines,
                 int line_count, int selected) {
    fb->clear(kBackground);
    ui::render_text_centered(fb, 40, title, kTitleColor, 4);
    ui::render_text_centered(fb, 140, "NATIVE PS5 RESEARCH RUNTIME - EXPERIMENTAL", kTextColor, 2);

    int y = 240;
    for (int i = 0; i < line_count && i < kMaxLines; ++i) {
        bool is_sel = (i == selected);
        ui::render_text(fb, 200, y, is_sel ? ">" : " ", is_sel ? kSelectColor : kTextColor, 2);
        ui::render_text(fb, 240, y, lines[i], is_sel ? kSelectColor : kTextColor, 2);
        y += 56;
    }
    ui::render_text(fb, 200, 1020,
                    "DPAD: NAVIGATE  CROSS: SELECT  CIRCLE: BACK  "
                    "OPTIONS: EXIT",
                    kTextColor, 2);
}

void run_launcher(platform::DisplayBackend* display, platform::InputBackend* input) {
    ui::LauncherModel launcher;
    android::RuntimeManager manager;
    android::CapabilityReport report = manager.probe();

    int screen = 0; // 0 = launcher menu, 1 = probe results, 2 = about
    bool running = true;
    platform::PadState pad{};
    bool prev_cross = false;
    bool prev_options = false;
    bool prev_circle = false;
    bool prev_up = false;
    bool prev_down = false;

    while (running) {
        platform::Framebuffer* fb = display->front_buffer();
        if (fb == nullptr)
            break;

        if (screen == 0) {
            const char* lines[kMaxLines];
            int n = 0;
            for (const auto& item : launcher.items()) {
                if (n >= kMaxLines)
                    break;
                lines[n++] = item.label.c_str();
            }
            show_screen(fb, "PS5DROID", lines, n, launcher.selected());
        } else if (screen == 1) {
            // Probe results are staged through static buffers because the
            // renderer takes plain C strings.
            static char buf[kMaxLines][48];
            const char* lines[kMaxLines];
            int n = 0;
            lines[n++] = "PLATFORM PROBES:";
            for (const auto& r : report.results) {
                if (n >= kMaxLines)
                    break;
                snprintf(buf[n], sizeof(buf[0]), "%-16s %s %s", r.name.c_str(),
                         r.passed ? "[OK]" : "[FAIL]", r.detail.c_str());
                lines[n++] = buf[n];
            }
            show_screen(fb, "PROBES", lines, n, -1);
        } else {
            static const char* kAbout[] = {
                "PS5DROID IS A RESEARCH PROJECT",
                "GOAL: NATIVE ANDROID RUNTIME STUDY",
                "STATUS: NO ANDROID APPS RUN YET",
                "SEE DOCS AND README FOR EVIDENCE",
            };
            show_screen(fb, "ABOUT", kAbout, 4, -1);
        }

        display->present();

        if (input->poll(&pad)) {
            bool edge_up = pad.up && !prev_up;
            bool edge_down = pad.down && !prev_down;
            bool edge_cross = pad.cross && !prev_cross;
            bool edge_circle = pad.circle && !prev_circle;
            bool edge_options = pad.options && !prev_options;

            if (screen == 0) {
                if (edge_up)
                    launcher.move_up();
                if (edge_down)
                    launcher.move_down();
                if (edge_cross) {
                    ui::LauncherAction action = launcher.activate();
                    switch (action) {
                    case ui::LauncherAction::kExit:
                        running = false;
                        break;
                    case ui::LauncherAction::kShowProbes:
                        screen = 1;
                        break;
                    case ui::LauncherAction::kShowAbout:
                        screen = 2;
                        break;
                    default:
                        screen = 2; // remaining screens: honest about
                        break;
                    }
                }
            } else if (edge_circle || edge_cross) {
                screen = 0;
            }

            if (edge_options)
                running = false;
            prev_up = pad.up;
            prev_down = pad.down;
            prev_cross = pad.cross;
            prev_circle = pad.circle;
            prev_options = pad.options;
        }

        platform::sleep_ns(1000000); // 1 ms; flips are vsync-locked
    }
}

} // namespace

int main() {
    ps5::sceSystemServiceHideSplashScreen();

    platform::DisplayBackend* display = platform::create_ps5_display();
    platform::InputBackend* input = platform::create_ps5_input();

    uint32_t w = 0, h = 0;
    if (!display->init(&w, &h)) {
        ps5::SceNotificationRequest req;
        memset(&req, 0, sizeof(req));
        strncpy(req.message, "PS5Droid: video-out init failed", sizeof(req.message) - 1);
        ps5::sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
        delete display;
        delete input;
        return 1;
    }

    if (!input->init()) {
        ps5::SceNotificationRequest req;
        memset(&req, 0, sizeof(req));
        strncpy(req.message, "PS5Droid: pad init failed", sizeof(req.message) - 1);
        ps5::sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
        display->shutdown();
        delete display;
        delete input;
        return 2;
    }

    run_launcher(display, input);

    display->shutdown();
    delete display;
    delete input;
    return 0;
}
