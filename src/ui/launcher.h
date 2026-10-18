// PS5Droid - launcher UI model (portable, testable).
//
// The launcher is a plain state machine so the same logic drives the host
// CLI, the unit tests and the PS5 pad-driven screen. Rendering is separate
// (see bitmap_font.h and the PS5 display backend).
#pragma once

#include <string>
#include <vector>

namespace ps5droid {
namespace ui {

enum class LauncherAction {
    kNone,
    kShowStatus,
    kShowProbes,
    kShowCompatMatrix,
    kShowAndroidEnv,
    kShowAbout,
    kExit,
};

class LauncherModel {
  public:
    struct Item {
        std::string label;
        LauncherAction action;
    };

    LauncherModel();

    void move_up();
    void move_down(); // wraps around
    void select(int index);
    LauncherAction activate() const;

    int selected() const { return selected_; }
    const std::vector<Item>& items() const { return items_; }

  private:
    std::vector<Item> items_;
    int selected_ = 0;
};

} // namespace ui
} // namespace ps5droid
