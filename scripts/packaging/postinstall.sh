#!/bin/bash

# Post-install script for Strata Studio (.pkg)
# This script runs automatically with root privileges after the user installs the .pkg
# It creates a symlink in /usr/local/bin to the daw-cli executable.

# Create the bin directory if it doesn't exist
mkdir -p /usr/local/bin

# The exact name of the application bundle
APP_NAME="Strata Studio.app"
ALT_APP_NAME="strata_studio.app"

CLI_PATH_1="/Applications/$APP_NAME/Contents/MacOS/daw-cli"
CLI_PATH_2="/Applications/$ALT_APP_NAME/Contents/MacOS/daw-cli"
CLI_PATH_3="/Applications/bin/daw-cli"

TARGET_SYMLINK="/usr/local/bin/daw-cli"

# Remove any existing symlink or old executable
rm -f "$TARGET_SYMLINK"

# Find the installed CLI and symlink it
if [ -f "$CLI_PATH_1" ]; then
    ln -s "$CLI_PATH_1" "$TARGET_SYMLINK"
    echo "Symlinked daw-cli from $CLI_PATH_1 to $TARGET_SYMLINK"
elif [ -f "$CLI_PATH_2" ]; then
    ln -s "$CLI_PATH_2" "$TARGET_SYMLINK"
    echo "Symlinked daw-cli from $CLI_PATH_2 to $TARGET_SYMLINK"
elif [ -f "$CLI_PATH_3" ]; then
    # If CMake dumped it in Applications/bin, move it into the app and symlink
    if [ -d "/Applications/$APP_NAME/Contents/MacOS" ]; then
        mv "$CLI_PATH_3" "/Applications/$APP_NAME/Contents/MacOS/daw-cli"
        ln -s "$CLI_PATH_1" "$TARGET_SYMLINK"
    else
        ln -s "$CLI_PATH_3" "$TARGET_SYMLINK"
    fi
else
    echo "Could not find daw-cli to symlink. Installation may be incomplete."
    exit 0
fi

# Set correct permissions
chmod 755 "$TARGET_SYMLINK"

exit 0
