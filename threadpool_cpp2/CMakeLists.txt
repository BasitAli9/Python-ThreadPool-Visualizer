cmake_minimum_required(VERSION 3.16)
project(ThreadPoolServer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Sources ────────────────────────────────────────────────────────────────
add_executable(ThreadPoolServer WIN32
    src/main.cpp
)

target_include_directories(ThreadPoolServer PRIVATE include)

# ── Link Windows system libraries (always available with MinGW / MSVC) ────
target_link_libraries(ThreadPoolServer PRIVATE
    comctl32
    gdi32
    user32
)

# ── Compiler flags ────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(ThreadPoolServer PRIVATE /W3 /O2 /utf-8)
else()
    # MinGW / GCC
    target_compile_options(ThreadPoolServer PRIVATE -Wall -Wextra -O2)
    target_link_options(ThreadPoolServer PRIVATE -mwindows)
endif()
