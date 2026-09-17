cmake_minimum_required(VERSION 3.12)

project(SoundMapper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NODE_EDITOR_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui-node-editor
)

set(IMGUI_DIR
    ${NODE_EDITOR_DIR}/external/imgui
)

add_subdirectory(${IMGUI_DIR} ${CMAKE_CURRENT_BINARY_DIR}/external/imgui)


add_library(imgui_node_editor STATIC
    ${NODE_EDITOR_DIR}/imgui_node_editor.cpp
    ${NODE_EDITOR_DIR}/imgui_node_editor_api.cpp
    ${NODE_EDITOR_DIR}/imgui_canvas.cpp
    ${NODE_EDITOR_DIR}/crude_json.cpp
)

target_include_directories(imgui_node_editor PUBLIC
    ${NODE_EDITOR_DIR}
)

target_link_libraries(imgui_node_editor PUBLIC
    imgui
)

add_executable(SoundMapper
    src/main.cpp
    src/tools.cpp
)

target_link_libraries(SoundMapper PRIVATE
    imgui_node_editor
)
