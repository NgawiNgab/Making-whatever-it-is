# Android mod packaging for Minecraft Bedrock 1.26.62.1 / arm64-v8a.
# This target is device-only and is NOT built by the verified host workflow.
cmake_minimum_required(VERSION 3.22)

if(NOT ANDROID)
    message(FATAL_ERROR "The Baritone Android mod must be built with an Android toolchain")
endif()
if(NOT TARGET baritone_core)
    message(FATAL_ERROR "Configure the top-level CMakeLists.txt so baritone_core is defined first")
endif()

set(MOD_ID "baritone" CACHE STRING "LeviLauncher mod package directory name")
set(MOD_NAME "Baritone" CACHE STRING "LeviLauncher mod display name")
set(MOD_AUTHOR "baritone-bedrock" CACHE STRING "LeviLauncher mod author")
set(MOD_VERSION "0.1.12" CACHE STRING "LeviLauncher mod version")
set(MOD_LIBRARY_NAME "baritone" CACHE STRING "Native library name without lib prefix or .so suffix")
# The offsets/signatures in this tree were recovered for one exact build. Do not
# advertise the package against a broad version family.
set(MOD_MINECRAFT_VERSIONS "[\"1.26.62.1\"]" CACHE STRING "Raw JSON array for manifest minecraft_versions")
set(MOD_ICON "" CACHE STRING "Optional package-relative icon path")

set(PRELOADER_ANDROID_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../preloader-android"
    CACHE PATH "Path to preloader-android headers providing include/pl")
set(ENTT_DIR "${CMAKE_SOURCE_DIR}/third_party/entt"
    CACHE PATH "EnTT restored by scripts/bootstrap.sh")
set(FMT_DIR "${CMAKE_SOURCE_DIR}/third_party/fmt"
    CACHE PATH "fmt restored by scripts/bootstrap.sh")

if(NOT EXISTS "${PRELOADER_ANDROID_DIR}/include/pl/Mod.hpp")
    message(FATAL_ERROR "preloader-android headers not found under ${PRELOADER_ANDROID_DIR}")
endif()
if(NOT EXISTS "${ENTT_DIR}/src/entt/entt.hpp")
    message(FATAL_ERROR "EnTT not found under ${ENTT_DIR}; run scripts/bootstrap.sh")
endif()
if(NOT EXISTS "${FMT_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "fmt not found under ${FMT_DIR}; run scripts/bootstrap.sh")
endif()

if(NOT TARGET fmt::fmt)
    set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)
    set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
    set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install rules" FORCE)
    add_subdirectory("${FMT_DIR}" "${CMAKE_BINARY_DIR}/fmt-build")
endif()

# All mod-layer translation units except src/baritone (already in baritone_core).
file(GLOB MOD_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compat/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/hooks/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/*.cpp")

add_library(${MOD_LIBRARY_NAME} SHARED ${MOD_SOURCES})
target_include_directories(${MOD_LIBRARY_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${PRELOADER_ANDROID_DIR}/include"
        "${ENTT_DIR}/src"
        "${FMT_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(${MOD_LIBRARY_NAME}
    PRIVATE
        baritone_core
        fmt::fmt
        log)
target_compile_options(${MOD_LIBRARY_NAME}
    PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-fvisibility=hidden -fvisibility-inlines-hidden>)
target_link_options(${MOD_LIBRARY_NAME}
    PRIVATE
        "-Wl,-z,max-page-size=16384"
        "-Wl,--unresolved-symbols=ignore-all")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/manifest.json"
    @ONLY)

set(BARITONE_PACKAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/package")
set(BARITONE_PACKAGE_DIR "${BARITONE_PACKAGE_ROOT}/${MOD_ID}")
add_custom_target(baritone_package
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${BARITONE_PACKAGE_ROOT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BARITONE_PACKAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${MOD_LIBRARY_NAME}>"
            "${BARITONE_PACKAGE_DIR}/lib${MOD_LIBRARY_NAME}.so"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_BINARY_DIR}/manifest.json"
            "${BARITONE_PACKAGE_DIR}/manifest.json"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${BARITONE_PACKAGE_ROOT}"
            "${CMAKE_COMMAND}" -E tar "cf"
            "${CMAKE_CURRENT_BINARY_DIR}/${MOD_ID}-${MOD_VERSION}-${CMAKE_ANDROID_ARCH_ABI}.levipack"
            --format=zip "${MOD_ID}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS ${MOD_LIBRARY_NAME}
    COMMENT "Packaging ${MOD_ID} for LeviLauncher"
    VERBATIM)
