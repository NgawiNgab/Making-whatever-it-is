#pragma once
#include <pl/Mod.hpp>

namespace baritone {

class BaritoneMod {
public:
    static BaritoneMod& instance();

    BaritoneMod();
    bool load();
    bool enable();
    bool disable();
    bool unload();

private:
    ll::mod::NativeMod& mSelf;
    bool mEnabled = false;
};

}  // namespace baritone
