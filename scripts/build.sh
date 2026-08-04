#!/bin/bash

# Strata Studio Build Script
# Usage: ./build.sh [debug|release|profile] [options]

set -e

# Default configuration
BUILD_TYPE="Debug"
BUILD_TESTS="OFF"
ENABLE_SIMD="ON"
PARALLEL_JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
RUN_TESTS="OFF"
RUN_PACKAGE=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_help() {
    echo "Strata Studio Build Script"
    echo ""
    echo "Usage: ./build.sh [CONFIGURATION] [OPTIONS]"
    echo ""
    echo "Configurations:"
    echo "  debug        Debug build (default, with symbols)"
    echo "  release      Release build (optimized)"
    echo "  profile      Profile build (optimized with profiling info)"
    echo ""
    echo "Options:"
    echo "  --no-tests   Skip building tests"
    echo "  --test       Build and run tests"
    echo "  --package    Build installer package (.pkg/.dmg on macOS, .exe on Windows)"
    echo "  --no-simd    Disable SIMD optimizations"
    echo "  --asan       Enable Address Sanitizer"
    echo "  --tsan       Enable Thread Sanitizer"
    echo "  --clean      Clean build directory first"
    echo "  --help       Show this help message"
    echo ""
    echo "Examples:"
    echo "  ./build.sh release --test        # Build release and run tests"
    echo "  ./build.sh release --package     # Build release and create installer package"
    echo "  ./build.sh debug --asan          # Debug build with address sanitizer"
    echo "  ./build.sh --clean --no-simd     # Clean build without SIMD"
}

# Parse arguments
BUILD_DIR=""
CLEAN_BUILD=0
USE_ASAN=0
USE_TSAN=0

while [[ $# -gt 0 ]]; do
    case $1 in
        debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        release)
            BUILD_TYPE="Release"
            shift
            ;;
        profile)
            BUILD_TYPE="RelWithDebInfo"
            shift
            ;;
        --no-tests)
            BUILD_TESTS="OFF"
            shift
            ;;
        --test)
            BUILD_TESTS="ON"
            RUN_TESTS="ON"
            shift
            ;;
        --package)
            RUN_PACKAGE=1
            shift
            ;;
        --no-simd)
            ENABLE_SIMD="OFF"
            shift
            ;;
        --asan)
            USE_ASAN=1
            shift
            ;;
        --tsan)
            USE_TSAN=1
            shift
            ;;
        --clean)
            CLEAN_BUILD=1
            shift
            ;;
        --help)
            print_help
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            print_help
            exit 1
            ;;
    esac
done

# Set build directory
BUILD_DIR="build/$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"

echo -e "${GREEN}Building Strata Studio${NC}"
echo "=================================="
echo "Configuration: $BUILD_TYPE"
echo "Build directory: $BUILD_DIR"
echo "Parallel jobs: $PARALLEL_JOBS"
echo "Tests: $BUILD_TESTS"
echo "SIMD: $ENABLE_SIMD"

if [ $USE_ASAN -eq 1 ]; then
    echo "Address Sanitizer: ON"
fi

if [ $USE_TSAN -eq 1 ]; then
    echo "Thread Sanitizer: ON"
fi

echo ""

# Clean build if requested
if [ $CLEAN_BUILD -eq 1 ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo -e "${GREEN}Configuring with CMake...${NC}"

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DBUILD_TESTS="$BUILD_TESTS"
    -DENABLE_SIMD="$ENABLE_SIMD"
)

if [ $USE_ASAN -eq 1 ]; then
    CMAKE_ARGS+=(-DUSE_ASAN=ON)
fi

if [ $USE_TSAN -eq 1 ]; then
    CMAKE_ARGS+=(-DUSE_TSAN=ON)
fi

cmake "${CMAKE_ARGS[@]}" ../..

# Build
echo ""
echo -e "${GREEN}Building...${NC}"
cmake --build . --parallel "$PARALLEL_JOBS"

# Run tests if requested
if [ "$RUN_TESTS" = "ON" ] && [ "$BUILD_TESTS" = "ON" ]; then
    echo ""
    echo -e "${GREEN}Running tests...${NC}"
    ctest --output-on-failure
elif [ "$BUILD_TESTS" = "ON" ]; then
    echo ""
    echo -e "${YELLOW}Tests built but not run. Use --test to run them.${NC}"
fi

# Run CPack if requested
if [ $RUN_PACKAGE -eq 1 ]; then
    echo ""
    echo -e "${GREEN}Packaging installer with CPack...${NC}"
    cpack -C "$BUILD_TYPE"
    
    if [ "$(uname)" = "Darwin" ]; then
        echo -e "${YELLOW}Patching macOS package to prevent relocation...${NC}"
        # Find the generated .pkg file
        PKG_FILE=$(find . -maxdepth 1 -name "*.pkg" -print -quit)
        if [ -n "$PKG_FILE" ]; then
            "../../scripts/packaging/patch_pkg.sh" "$PKG_FILE"
        fi
    fi
fi


# Print summary
echo ""
echo -e "${GREEN}Build complete!${NC}"
echo "Executable: $BUILD_DIR/bin/strata_studio"
if [ $RUN_PACKAGE -eq 1 ]; then
    echo "Packages created in: $BUILD_DIR/"
fi
echo ""
echo "To run the application:"
echo "  cd $BUILD_DIR"
echo "  ./bin/strata_studio"

