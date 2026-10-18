// PS5Droid - launcher model (implementation).
#include "ui/launcher.h"

namespace ps5droid {
namespace ui {

LauncherModel::LauncherModel() {
    items_ = {
        {"Status", LauncherAction::kShowStatus},
        {"Platform Probes", LauncherAction::kShowProbes},
        {"Linux ABI Matrix", LauncherAction::kShowCompatMatrix},
        {"Android Environment", LauncherAction::kShowAndroidEnv},
        {"About", LauncherAction::kShowAbout},
        {"Exit", LauncherAction::kExit},
    };
}

void LauncherModel::move_up() {
    if (items_.empty())
        return;
    selected_ = (selected_ - 1 + static_cast<int>(items_.size())) % static_cast<int>(items_.size());
}

void LauncherModel::move_down() {
    if (items_.empty())
        return;
    selected_ = (selected_ + 1) % static_cast<int>(items_.size());
}

void LauncherModel::select(int index) {
    if (index >= 0 && index < static_cast<int>(items_.size())) {
        selected_ = index;
    }
}

LauncherAction LauncherModel::activate() const {
    if (items_.empty())
        return LauncherAction::kNone;
    return items_[static_cast<size_t>(selected_)].action;
}

} // namespace ui
} // namespace ps5droid
