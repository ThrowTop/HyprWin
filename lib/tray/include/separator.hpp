#pragma once
#include "entry.hpp"
#include <functional>

namespace Tray {
    class Separator : public TrayEntry {
    public:
        Separator();
        ~Separator() override = default;
    };
} // namespace Tray