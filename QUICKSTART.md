# Quick Start Guide

## 1. Agentic One-Prompt Setup (Recommended)

Open your AI agent (**Claude Code**, **Codex**, **Cursor**, **Hermes**, **Gemini CLI**, **OpenCode**, etc.) and tell it:

```text
Build and package Strata Studio for me
```

*(Your agent will automatically execute `./scripts/install_dependencies.sh` and `./scripts/build.sh release --package`).*

---

## 2. Manual Command-Line Setup

### Install Dependencies

Run the dependency installation script to install build tools and bootstrap **vcpkg**:

```bash
./scripts/install_dependencies.sh
```

This script will:
1. Install `cmake`, `git`, and build tools.
2. Bootstrap **vcpkg** in the project root.
3. Automatically download and build `rtaudio`, `rtmidi`, `libsndfile`, `spdlog`, etc.

## 2. Build the Project

### Using the build script (recommended):
```bash
# Debug build
./scripts/build.sh debug

# Release build
./scripts/build.sh release

# Release build with tests
./scripts/build.sh release --test
```

### Manual build:
```bash
mkdir -p build/debug && cd build/debug
cmake -DCMAKE_BUILD_TYPE=Debug ../../
cmake --build . --parallel
```

## 3. Run the Application

```bash
cd build/debug
./bin/agent_based_daw
```

## 4. Run Tests

```bash
cd build/debug
ctest --output-on-failure
```

Or run specific test:
```bash
./tests/unit/unit_tests
```
