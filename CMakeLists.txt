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
    src/hwid.cpp
    src/api/api.cpp
    src/api/session.cpp
    src/memory/runpe.cpp
    ${IMGUI_SOURCES}
)

# Require administrator privileges (UAC elevation prompt)
set_target_properties(ScootwareLoader PROPERTIES
    VS_USER_PROPS ""
)
target_link_options(ScootwareLoader PRIVATE
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
)

target_link_libraries(ScootwareLoader d3d11 d3dcompiler dxgi user32 crypt32 shell32)

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

add_executable(HelloWorldTest
    ${CMAKE_SOURCE_DIR}/../test_exe/test_helloworld.cpp
)
set_target_properties(HelloWorldTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../test_exe"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/../test_exe"
)
