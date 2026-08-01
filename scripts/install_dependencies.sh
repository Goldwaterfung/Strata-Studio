#!/bin/bash

# Strata Studio Dependency Installation Script
# Supports macOS, Ubuntu/Debian, and Fedora/RHEL

set -e

OS="$(uname -s)"
echo "Detected OS: $OS"

case "$OS" in
    Darwin*)
        echo "Installing dependencies for macOS..."

        # Check if Homebrew is installed (only for tools)
        if ! command -v brew &> /dev/null; then
            echo "Homebrew not found. Please install from https://brew.sh/ to install build tools."
            exit 1
        fi

        # Install build tools
        echo "Installing build tools..."
        brew install cmake pkg-config git

        # Setup vcpkg and third-party SDKs
        bash ./scripts/setup_vcpkg.sh
        bash ./scripts/setup_third_party.sh
        ;;

    Linux*)
        echo "Linux platform is not supported in Strata Studio."
        exit 1
        ;;

    MINGW*|CYGWIN*|MSYS*)
        echo "Windows detected via Git Bash/MSYS."
        bash ./scripts/setup_vcpkg.sh
        bash ./scripts/setup_third_party.sh
        ;;

    *)
        echo "Unsupported operating system: $OS"
        exit 1
        ;;
esac

echo ""
echo "To verify installation, run:"
echo "  cmake --version"
echo "  g++ --version   (or clang++ --version)"
