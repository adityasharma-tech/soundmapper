cmake_minimum_required(VERSION 3.16)
project(SoundMapper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()


find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)


set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${GLFW_INCLUDE_DIRS}
)

target_link_libraries(imgui PUBLIC ${GLFW_LIBRARIES} OpenGL::GL)
target_compile_options(imgui PUBLIC ${GLFW_CFLAGS_OTHER})


add_executable(SoundMapper src/main.cpp)
target_link_libraries(SoundMapper PRIVATE imgui)
