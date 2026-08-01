#!/bin/bash

# Strata Studio Third-Party SDK Downloader Script
# Automatically fetches required third-party C++ libraries and plugin SDKs into src/third_party

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_DIR="$PROJECT_ROOT/src/third_party"

mkdir -p "$THIRD_PARTY_DIR"

echo "Setting up third-party SDKs in $THIRD_PARTY_DIR..."

clone_repo() {
    local folder_name="$1"
    local repo_url="$2"
    local target_dir="$THIRD_PARTY_DIR/$folder_name"

    if [ -d "$target_dir" ] && [ "$(ls -A "$target_dir")" ]; then
        echo "✓ $folder_name already exists, skipping."
    else
        echo "--> Cloning $folder_name from $repo_url..."
        git clone --depth 1 --recursive "$repo_url" "$target_dir"
        echo "✓ $folder_name setup complete."
    fi
}

# 1. RtAudio (Audio Driver HAL)
clone_repo "rtaudio" "https://github.com/thestk/rtaudio.git"

# 2. RtMidi (MIDI Driver HAL)
clone_repo "rtmidi" "https://github.com/thestk/rtmidi.git"

# 3. VST3 SDK (Steinberg VST3 Host/Plugin SDK)
clone_repo "VST3_SDK" "https://github.com/steinbergmedia/vst3sdk.git"

# 4. CLAP SDK (CLever Audio Plugin SDK)
clone_repo "CLAP_SDK" "https://github.com/free-audio/clap.git"

# 5. Eigen (Linear Algebra & FFT)
clone_repo "eigen-5.0.0" "https://gitlab.com/libeigen/eigen.git"

# 6. Rubber Band (Pitch Shifting & Time Stretching)
clone_repo "rubberband-default" "https://github.com/breakfastquay/rubberband.git"

# 7. SoundTouch (Time Stretching & Jitter Buffer)
clone_repo "soundtouch" "https://codeberg.org/soundtouch/soundtouch.git"

# 8. ASIO SDK (Windows Low-Latency Audio Driver)
clone_repo "ASIOSDK" "https://github.com/steinbergmedia/asiosdk.git"

echo ""
echo "✓ All third-party SDKs are successfully configured in $THIRD_PARTY_DIR!"
