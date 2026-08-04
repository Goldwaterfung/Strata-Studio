#!/bin/bash
# Script to patch a CPack-generated macOS .pkg to disable relocation.

set -e

PKG_FILE="$1"

if [ -z "$PKG_FILE" ] || [ ! -f "$PKG_FILE" ]; then
    echo "Usage: $0 <path_to_pkg>"
    exit 1
fi

TMP_DIR=$(mktemp -d)
PKG_NAME=$(basename "$PKG_FILE")
EXPANDED_DIR="$TMP_DIR/$PKG_NAME.expanded"

echo "Expanding $PKG_FILE..."
pkgutil --expand "$PKG_FILE" "$EXPANDED_DIR"

echo "Patching PackageInfo files..."
# Find all PackageInfo files in the expanded directory and replace the relocate tag
find "$EXPANDED_DIR" -name "PackageInfo" -type f | while read -r pinfo; do
    # Replace the <relocate>...</relocate> block with an empty <relocate/>
    # This prevents the installer from trying to find existing installations
    sed -i '' '/<relocate>/,/<\/relocate>/c\
<relocate/>' "$pinfo"
done

# If there is a Distribution file (which usually exists in top-level pkgs),
# there might also be bundle location keys there depending on CPack version,
# but usually patching PackageInfo is sufficient.

echo "Flattening back to $PKG_FILE..."
rm -f "$PKG_FILE"
pkgutil --flatten "$EXPANDED_DIR" "$PKG_FILE"

rm -rf "$TMP_DIR"
echo "Done patching $PKG_FILE"
