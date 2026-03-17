cmake_minimum_required(VERSION 3.22.1)
project(wxwork-tablet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(wxwork-tablet SHARED jni/main.cpp)

target_include_directories(wxwork-tablet PRIVATE jni)

target_link_libraries(wxwork-tablet log)   # __android_log_print
# libc（含 __system_property_get）由 NDK 自动链接，无需显式写

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
