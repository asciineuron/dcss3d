#!/bin/bash

cd "${BASH_SOURCE%/*}"

cmake --build build -v -j8 && cd build && ./dcss3d
