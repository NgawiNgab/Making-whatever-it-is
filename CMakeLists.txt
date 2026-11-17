cmake_minimum_required(VERSION 3.22)
project(making_whatever_it_is LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static host libraries" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(REFERENCE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/reference/baritone-android/src")
set(ENTT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/entt" CACHE PATH "EnTT restored by scripts/bootstrap.sh")
set(FMT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fmt" CACHE PATH "fmt restored by scripts/bootstrap.sh")
set(PRELOADER_ANDROID_DIR "${CMAKE_CURRENT_SOURCE_DIR}/reference/preloader-android" CACHE PATH "preloader headers")

if(NOT EXISTS "${REFERENCE_SRC}/baritone/Baritone.cpp")
    message(FATAL_ERROR "reference core sources are missing under ${REFERENCE_SRC}")
endif()

find_package(Threads REQUIRED)

# The vendored C++ core is compiled as a static library for host tests/CLI and
# reused by the Android mod target. It has no Android/JNI dependencies.
file(GLOB_RECURSE BARITONE_CORE_SOURCES CONFIGURE_DEPENDS
    "${REFERENCE_SRC}/baritone/*.cpp")
add_library(baritone_core STATIC ${BARITONE_CORE_SOURCES})
target_include_directories(baritone_core PUBLIC "${REFERENCE_SRC}")
target_link_libraries(baritone_core PUBLIC Threads::Threads)

# Small platform helpers needed by the first-party simulation. They do not hook
# a live process and are safe to compile on the host.
add_library(baritone_sim_support STATIC
    "${REFERENCE_SRC}/platform/BlockClassifier.cpp"
    "${REFERENCE_SRC}/sdk/Nbt.cpp")
target_include_directories(baritone_sim_support PUBLIC "${REFERENCE_SRC}")
target_link_libraries(baritone_sim_support PUBLIC Threads::Threads)

file(GLOB SIM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/sim/*.cpp")
add_library(baritone_sim_engine STATIC ${SIM_SOURCES})
target_include_directories(baritone_sim_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${REFERENCE_SRC}")
target_link_libraries(baritone_sim_engine PUBLIC baritone_core baritone_sim_support)

add_executable(baritone-sim "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/main.cpp")
target_link_libraries(baritone-sim PRIVATE baritone_sim_engine)

# Regression harness links the same static libraries produced above.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/sim_harness.cpp")
    add_executable(sim_harness "${CMAKE_CURRENT_SOURCE_DIR}/tests/sim_harness.cpp")
    target_include_directories(sim_harness PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${REFERENCE_SRC}"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(sim_harness PRIVATE baritone_sim_engine)
    set_target_properties(sim_harness PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# Android packaging uses the vendored mod layer plus the shared core target.
if(ANDROID)
    if(NOT EXISTS "${PRELOADER_ANDROID_DIR}/include/pl/Mod.hpp")
        message(FATAL_ERROR "preloader headers are missing under ${PRELOADER_ANDROID_DIR}")
    endif()
    if(NOT EXISTS "${ENTT_DIR}/src/entt/entt.hpp")
        message(FATAL_ERROR "run scripts/bootstrap.sh before an Android build (missing ${ENTT_DIR})")
    endif()
    if(NOT EXISTS "${FMT_DIR}/include/fmt/format.h")
        message(FATAL_ERROR "run scripts/bootstrap.sh before an Android build (missing ${FMT_DIR})")
    endif()
    add_subdirectory(reference/baritone-android)
endif()
