cmake_minimum_required(VERSION 3.10)
project(ScootwareLoader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ─────────────────────────────────────────────────────────────────────────────
# Release-build hardening for MSVC
#
# These flags meaningfully raise the bar for static analysis (IDA / Ghidra /
# Binary Ninja) by stripping debug info, inlining aggressively, and folding
# functions/data so symbols become harder to recover. Combined with the
# compile-time string encryption in src/security/obf.h, the resulting binary
# exposes very little to a casual reverse engineer.
#
# Notes:
#   * Only applied for the Release configuration. Debug builds remain
#     unobfuscated and easy to step through.
#   * `/GL` (whole-program optimization) requires `/LTCG` at link time.
#   * `/Oi` enables intrinsic expansion (CRT calls disappear into the body).
#   * `/Gy` + `/OPT:REF /OPT:ICF` lets the linker drop unused functions and
#     fold identical functions/data, hiding the original code structure.
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(
        $<$<CONFIG:Release>:/O2>      # max speed
        $<$<CONFIG:Release>:/Ob2>     # aggressive inlining
        $<$<CONFIG:Release>:/Oi>      # intrinsic functions
        $<$<CONFIG:Release>:/Os>      # favor size where speed-equivalent
        $<$<CONFIG:Release>:/GL>      # whole-program optimization
        $<$<CONFIG:Release>:/Gy>      # function-level linking (enables ICF)
        $<$<CONFIG:Release>:/GS->     # disable buffer security cookies (smaller, harder to fingerprint)
        $<$<CONFIG:Release>:/Zc:inline>
        $<$<CONFIG:Release>:/MT>      # statically link CRT — no external CRT footprint
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>           # required for /GL
        $<$<CONFIG:Release>:/OPT:REF>        # strip unreferenced funcs/data
        $<$<CONFIG:Release>:/OPT:ICF=3>      # fold identical funcs AND read-only data
        $<$<CONFIG:Release>:/DEBUG:NONE>     # no PDB, no debug directory entries
    )
    # Ensure the static CRT (/MT) is honored across configs.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ImGui Library Path
set(IMGUI_DIR "imgui-master/imgui-master")

# Include directories
include_directories(
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# ImGui source files
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

add_executable(ScootwareLoader WIN32
    src/main.cpp
    src/hwid.cpp
    src/api/api.cpp
    src/api/session.cpp
    src/memory/runpe.cpp
    src/vmdetect.cpp
    src/image_loader.cpp
    src/security/handoff.cpp
    src/security/driver_bringup.cpp
    src/util/process_wait.cpp
    ${IMGUI_SOURCES}
)

# Make the project root visible so source files can `#include "security/obf.h"`
target_include_directories(ScootwareLoader PRIVATE src)

# Require administrator privileges (UAC elevation prompt)
set_target_properties(ScootwareLoader PROPERTIES
    VS_USER_PROPS ""
)
target_link_options(ScootwareLoader PRIVATE
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
)

target_link_libraries(ScootwareLoader d3d11 d3dcompiler dxgi user32 gdi32 crypt32 shell32 winhttp windowscodecs ole32)

# Minimal hollow host — console subsystem so GetStdHandle works for any payload.
# Named scootware.exe because that is what the kernel driver searches for.
add_executable(ScootwareHost
    src/host/host.cpp
)
set_target_properties(ScootwareHost PROPERTIES
    OUTPUT_NAME "scootware"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)
target_link_options(ScootwareHost PRIVATE
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
)
