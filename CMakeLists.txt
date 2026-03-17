cmake_minimum_required(VERSION 3.22.1)
project(wxwork-tablet LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ─── LSPlant — ART method hook library ───────────────────────────────────────
FetchContent_Declare(
    lsplant
    GIT_REPOSITORY https://github.com/LSPosed/LSPlant.git
    GIT_TAG        v6.4
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES "lsplant/src/main/jni/external/dex_builder"  # skip private test submodules
    SOURCE_SUBDIR  lsplant/src/main/jni   # actual CMakeLists.txt location
)

# ─── ShadowHook — inline hook backend ────────────────────────────────────────
FetchContent_Declare(
    shadowhook
    GIT_REPOSITORY https://github.com/bytedance/android-inline-hook.git
    GIT_TAG        v1.1.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  shadowhook/src/main/cpp   # actual CMakeLists.txt location
)

# Fix lsplant's post-build debug-symbol step: set a valid output directory so
# it doesn't try to create the invalid absolute path "/${ANDROID_ABI}"
set(DEBUG_SYMBOLS_PATH "${CMAKE_BINARY_DIR}/symbols" CACHE STRING "" FORCE)

FetchContent_MakeAvailable(shadowhook lsplant)

# Suppress all warnings in shadowhook (third-party, not our code)
target_compile_options(shadowhook PRIVATE -w)

# Prevent shadowhook's -std=c17 and internal include paths from leaking into our C++ target
set_target_properties(shadowhook PROPERTIES
    INTERFACE_COMPILE_OPTIONS ""
    INTERFACE_INCLUDE_DIRECTORIES ""
)

add_library(wxwork-tablet SHARED jni/main.cpp)

target_include_directories(wxwork-tablet PRIVATE
    jni
    ${lsplant_SOURCE_DIR}/lsplant/src/main/jni/include
    ${shadowhook_SOURCE_DIR}/shadowhook/src/main/cpp/include
)

target_link_libraries(wxwork-tablet
    lsplant
    shadowhook
    log
)

target_compile_options(wxwork-tablet PRIVATE
    -O2
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
)

target_link_options(wxwork-tablet PRIVATE
    -Wl,--gc-sections
    -Wl,--strip-all
    -Wl,--exclude-libs,ALL
)
