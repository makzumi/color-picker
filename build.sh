#!/bin/bash
set -e

# Set build directory
BUILD_DIR="build"

# Clean build directory
echo "Cleaning build directory..."
rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake for Release
echo "Configuring project with CMake (Release mode)..."
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build the project
echo "Building the project..."
make -j$(nproc)

sudo make install
echo "Build and install complete."