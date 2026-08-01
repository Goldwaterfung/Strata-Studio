#!/bin/bash

# Setup vcpkg for the project
# This script clones vcpkg if it doesn't exist and bootstraps it.

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_DIR="$PROJECT_ROOT/vcpkg"

echo "Setting up vcpkg in $VCPKG_DIR..."

if [ ! -d "$VCPKG_DIR" ]; then
    echo "Cloning vcpkg..."
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
fi

if [ ! -f "$VCPKG_DIR/vcpkg" ]; then
    echo "Bootstrapping vcpkg..."
    cd "$VCPKG_DIR"
    ./bootstrap-vcpkg.sh
    cd "$PROJECT_ROOT"
fi

echo "✓ vcpkg setup complete!"
