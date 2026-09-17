#!/bin/bash

cd /home/$USER/soundmapper || exit 1

if [[ "$1" == "-I" ]];then
    echo "-- Installing Dependecies"
    sudo apt install cmake libglfw3-dev libgl1-mesa-dev pkg-config
fi

cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

if [[ "$1" == "-R" ]]; then
    echo "-- Running SoundMapper"
    ./build/SoundMapper
fi
