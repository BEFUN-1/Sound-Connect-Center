#!/bin/sh
set -eu

if [ ! -f /src/Client/imgui/imgui.cpp ]; then
    echo 'Initialize the source checkout with: git submodule update --init --recursive' >&2
    exit 1
fi

build_dir=${BUILD_DIR:-/build}
cmake -S /src -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" \
    -DBUILD_TESTING=ON -DSONY_REQUIRE_QT=ON \
    -DENABLE_SANITIZERS="${ENABLE_SANITIZERS:-OFF}"
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --test-dir "$build_dir" --output-on-failure --no-tests=error "$@"
