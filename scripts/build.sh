#!/bin/bash

WORK_DIR=$(pwd)
cd $WORK_DIR || exit 1

if [[ "$1" == "-I" ]];then
    echo "-- Installing Dependecies"
    sudo apt install cmake libglfw3-dev libgl1-mesa-dev pkg-config
fi

cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release || exit 1
cmake --build build -j$(nproc) || exit 1

if [[ "$1" == "-R" ]]; then
    echo "-- Running SoundMapper"
    cd $WORK_DIR/build/bin || exit 1
    ./soundmapper
    cd $WORK_DIR || exit 1
fi
