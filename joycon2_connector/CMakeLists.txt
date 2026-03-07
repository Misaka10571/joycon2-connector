cmake_minimum_required(VERSION 3.20)
project(joycon2_connector VERSION 1.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options("/Zc:char8_t-")
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Generate version header from template
configure_file(
  "${CMAKE_SOURCE_DIR}/src/version.h.in"
  "${CMAKE_BINARY_DIR}/generated/version.h"
  @ONLY
)

# Generate embedded language data header from languages/*.json
file(GLOB LANG_JSON_FILES "${CMAKE_SOURCE_DIR}/languages/*.json")
add_custom_command(
  OUTPUT "${CMAKE_BINARY_DIR}/generated/lang_data.h"
  COMMAND ${CMAKE_COMMAND}
    -DLANG_DIR="${CMAKE_SOURCE_DIR}/languages"
    -DOUTPUT_FILE="${CMAKE_BINARY_DIR}/generated/lang_data.h"
    -P "${CMAKE_SOURCE_DIR}/cmake/embed_languages.cmake"
  DEPENDS ${LANG_JSON_FILES} "${CMAKE_SOURCE_DIR}/cmake/embed_languages.cmake"
  COMMENT "Embedding language files into lang_data.h"
)
add_custom_target(generate_lang_data DEPENDS "${CMAKE_BINARY_DIR}/generated/lang_data.h")

include_directories(
  src
  src/imgui
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_BINARY_DIR}/generated
)

# Core application sources
set(APP_SOURCES
  src/App.cpp
  src/JoyConDecoder.cpp
)

# Resource file (DPI manifest)
set(RC_SOURCES
  resources/app.rc
)

# Dear ImGui sources
set(IMGUI_SOURCES
  src/imgui/imgui.cpp
  src/imgui/imgui_draw.cpp
  src/imgui/imgui_tables.cpp
  src/imgui/imgui_widgets.cpp
  src/imgui/imgui_impl_win32.cpp
  src/imgui/imgui_impl_dx11.cpp
)

add_executable(joycon2_connector WIN32 ${APP_SOURCES} ${IMGUI_SOURCES} ${RC_SOURCES})
add_dependencies(joycon2_connector generate_lang_data)

target_link_directories(joycon2_connector PRIVATE ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(joycon2_connector
    PRIVATE
        setupapi
        hid
        ViGEmClient
        windowsapp
        d3d11
        dxgi
        d3dcompiler
        dwmapi
)

if(MSVC)
  target_compile_options(joycon2_connector PRIVATE /W3 /permissive- /utf-8)
  # Disable auto-generated manifest — we embed our own via app.rc
  target_link_options(joycon2_connector PRIVATE /MANIFEST:NO)
else()
  target_compile_options(joycon2_connector PRIVATE -Wall -Wextra -pedantic)
endif()

# Copy resources to output directory
add_custom_command(TARGET joycon2_connector POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:joycon2_connector>/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/resources/app.manifest"
        "$<TARGET_FILE_DIR:joycon2_connector>/resources/app.manifest"
)
