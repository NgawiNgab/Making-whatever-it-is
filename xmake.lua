add_rules("mode.debug", "mode.release")
set_policy("package.requires_lock", true)

-- The preloader SDK is not in xmake-repo, so it is declared as a local package that
-- builds from source with CMake. This mirrors the reference build (BedrockTools
-- xmake.lua): declaring it via add_requires alone fails with
-- "package(preloader) not found".
package("preloader")
    set_homepage("https://github.com/LiteLDev/preloader-android")
    set_description("Preloader Android SDK")
    add_urls("https://github.com/LiteLDev/preloader-android.git")
    add_versions("main", "main")
    add_deps("cmake")
    on_install("android", function (package)
        import("package.tools.cmake").install(package)
    end)
package_end()

-- Both dependencies are Android-build-only, and the guard is load-bearing: a root
-- scope add_requires is resolved for *every* target, so requiring the Android-only
-- preloader (its install hook is on_install("android", ...)) makes the host test
-- target fail with "unsupported on linux/x86_64" -- a target that never touches it.
if is_plat("android") then
    add_requires("preloader")
    -- pl/Logger.hpp includes <fmt/format.h>, so fmt is part of the SDK's public
    -- interface even though the preloader package only exports include/pl. Unpinned,
    -- exactly as the reference requires it.
    add_requires("fmt")
end

-- Guard rails: an Android build without an NDK fails deep inside package
-- resolution with "no any matched platform for this package(preloader)", which
-- reads like a preloader problem rather than a missing toolchain. Say the real
-- reason instead.
if is_plat("android") and not os.getenv("ANDROID_NDK_HOME") and not os.getenv("ANDROID_NDK_ROOT") then
    -- Neither `raise` nor `warning` is callable at root scope in xmake 3.x. `print`
    -- is, and the intent here is only to explain the confusing error that follows
    -- ("no any matched platform for this package(preloader)"), which points at the
    -- package rather than at the missing toolchain.
    print("note: ANDROID_NDK_HOME is unset. This project targets NDK r28c; the Android")
    print("      build will fail to resolve package(preloader) until it is set.")
end

-- Deliberately no set_version(): on a shared library xmake then emits
-- libCameraFacing.so.0.1.0 plus symlinks, and scripts/package_levipack.py rejects any
-- library whose filename is not libCameraFacing.so. The version lives in
-- include/camface/Version.hpp, which the packager parses into the manifest; the
-- reference does not call set_version either.

target("CameraFacing")
    set_kind("shared")
    set_languages("c++20")
    set_strip("all")

    add_files("src/main.cpp")
    add_files("src/core/*.cpp", "src/platform/*.cpp", "src/game/*.cpp", "src/features/*.cpp")
    add_files("data/1.26.62.cpp")

    add_includedirs("include", {public = true})
    add_includedirs("src")

    -- fmt belongs here, not only in add_requires: a root-scope add_requires just
    -- resolves the package, while add_packages is what puts its include dirs on the
    -- compile line and links it. Referencing it only in add_requires is what produced
    -- "'fmt/format.h' file not found" for six CI runs.
    if is_plat("android") then
        add_packages("preloader", "fmt")
    end

    if is_plat("android") then
        add_cxflags("-fPIC", "-O2", "-ffunction-sections", "-fdata-sections", "-fexceptions", "-w")
        -- The mod never uses RTTI: the game's RTTI strings are data we scan for,
        -- never types we depend on.
        add_cxxflags("-fno-rtti")
        add_shflags("-Wl,--gc-sections", "-Wl,--hash-style=gnu", "-Wl,-z,max-page-size=16384")
        add_links("android", "log", "EGL", "GLESv3", "GLESv2")
    end

    after_build(function (target)
        if not target:is_plat("android") then return end
        import("lib.detect.find_tool")
        local python = find_tool("python3") or find_tool("python")
        local args = {}
        if not python then
            python = find_tool("py")
            if python then table.insert(args, "-3") end
        end
        assert(python, "Python 3 is required to package CameraFacing.levipack")
        table.insert(args, path.join(os.projectdir(), "scripts", "package_levipack.py"))
        table.insert(args, "--library");      table.insert(args, target:targetfile())
        table.insert(args, "--icon");         table.insert(args, path.join(os.projectdir(), "assets", "icon.png"))
        table.insert(args, "--version-header"); table.insert(args, path.join(os.projectdir(), "include", "camface", "Version.hpp"))
        table.insert(args, "--output");       table.insert(args, path.join(target:targetdir(), "CameraFacing.levipack"))
        os.vrunv(python.program, args)
    end)

-- Host-side tests. These never touch a game binary: the resolver takes an
-- ImageSource, so a fixture buffer exercises the real code path (docs/SPEC.md NFR-4).
target("camface_tests")
    set_kind("binary")
    set_default(false)
    set_languages("c++20")
    add_files("tests/*.cpp")
    add_files("src/core/Pattern.cpp", "src/core/Facts.cpp", "src/core/BuildRegistry.cpp")
    add_files("data/1.26.62.cpp")
    add_includedirs("include")
