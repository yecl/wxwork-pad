cmake_minimum_required(VERSION 3.22.1)
project(wxwork-tablet LANGUAGES CXX)

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
)

FetchContent_MakeAvailable(shadowhook lsplant)

add_library(wxwork-tablet SHARED jni/main.cpp)

target_include_directories(wxwork-tablet PRIVATE
    jni
    ${lsplant_SOURCE_DIR}/lsplant/src/main/jni/include
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
