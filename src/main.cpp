#include "core/Runtime.hpp"

#include <pl/Mod.hpp>

// The preloader looks for exactly this registration symbol. Lifecycle functions
// are dispatched by concept, so any subset of load/enable/disable/unload works;
// this mod implements all four.
class CameraFacingMod {
public:
    static CameraFacingMod& instance() {
        static CameraFacingMod mod;
        return mod;
    }

    bool load(pl::mod::ModContext& context) { return camface::core::Runtime::get().load(context); }
    bool enable(pl::mod::ModContext& context) { return camface::core::Runtime::get().enable(context); }
    bool disable(pl::mod::ModContext& context) {
        return camface::core::Runtime::get().disable(context);
    }
    bool unload(pl::mod::ModContext& context) {
        return camface::core::Runtime::get().unload(context);
    }
};

PL_REGISTER_MOD(CameraFacingMod, CameraFacingMod::instance())
