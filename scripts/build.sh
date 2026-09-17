#!/bin/bash

cd /home/$USER/soundmapper || exit 1

cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j$(nproc)

if [[ "$1" == "-R" ]]; then
    echo "-- Running SoundMapper"
    ./build/SoundMapper
fi
