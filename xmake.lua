add_rules("mode.debug", "mode.release")

-- The preloader SDK is the only mandatory dependency. It is pulled by version tag
-- and provides pl::mod, pl::modmenu, pl::input, pl::log and pl::memory.
add_requires("preloader", {system = false})
add_requires("nlohmann_json v3.11.3")

target("CameraFacing")
    set_kind("shared")
    set_languages("c++20")
    set_strip("all")
    add_files("src/main.cpp", "src/core/*.cpp", "src/platform/*.cpp", "src/game/*.cpp",
              "src/features/*.cpp", "data/1.26.62.cpp")
    add_includedirs("include", {public = true})
    add_includedirs("src")
    add_packages("preloader", "nlohmann_json")

    if is_plat("android") then
        add_cxflags("-fPIC", "-O2", "-ffunction-sections", "-fdata-sections", "-fexceptions", "-w")
        -- The mod itself never uses RTTI; the game's RTTI strings are data we scan,
        -- never types we depend on.
        add_cxxflags("-fno-rtti")
        add_shflags("-Wl,--gc-sections", "-Wl,--hash-style=gnu", "-Wl,-z,max-page-size=16384")
        add_links("android", "log", "EGL", "GLESv3")
    end

-- Host-side tests. These never touch a game binary: the resolver takes an
-- ImageSource, so a fixture buffer exercises the real code path (docs/SPEC.md NFR-4).
target("camface_tests")
    set_kind("binary")
    set_default(false)
    set_languages("c++20")
    add_files("tests/*.cpp", "src/core/Pattern.cpp", "src/core/Facts.cpp",
              "src/core/BuildRegistry.cpp", "data/1.26.62.cpp")
    add_includedirs("include")

    if is_plat("android") then
        set_default(false)  -- host only
    end
