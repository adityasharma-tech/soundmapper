add_example_executable(soundmapper
    soundmapper.cpp
    routing_engine.h
    utilities/builders.h
    utilities/drawing.h
    utilities/widgets.h
    utilities/builders.cpp
    utilities/drawing.cpp
    utilities/widgets.cpp
)
target_include_directories(soundmapper PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
