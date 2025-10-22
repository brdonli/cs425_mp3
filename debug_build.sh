#!/bin/bash

# Debug script to check build status on a specific VM

NETID="rayans2"
MACHINE=${1:-01}  # Default to VM01 if no argument
BASE_HOST="fa25-cs425-16"
DOMAIN="cs.illinois.edu"
REMOTE_DIR="~/cs425_mp3"

echo "Checking build status on VM${MACHINE}..."
echo ""

ssh "${NETID}@${BASE_HOST}${MACHINE}.${DOMAIN}" << 'EOF'
cd ~/cs425_mp3

echo "=== Current directory ==="
pwd
echo ""

echo "=== Files in current directory ==="
ls -la
echo ""

echo "=== Build directory status ==="
if [ -d build ]; then
    echo "Build directory exists"
    echo "Contents:"
    ls -la build/
    echo ""

    if [ -f build/main ]; then
        echo "✓ build/main exists"
    else
        echo "✗ build/main does NOT exist"
        echo ""
        echo "Attempting to build..."
        module load cmake
        cmake -S . -B build
        make -C build -j4
    fi
else
    echo "✗ Build directory does NOT exist"
    echo ""
    echo "Creating build directory and building..."
    module load cmake
    cmake -S . -B build
    make -C build -j4
fi
EOF
