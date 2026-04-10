cmake_minimum_required(VERSION 3.10)
project(ScootwareLoader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# In a real scenario, adding CPR, libcurl, and JSON is necessary.
# For stub purposes we just use basic libraries.

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
    src/api/api.cpp
    src/memory/runpe.cpp
    ${IMGUI_SOURCES}
)

target_link_libraries(ScootwareLoader d3d11 d3dcompiler user32)
