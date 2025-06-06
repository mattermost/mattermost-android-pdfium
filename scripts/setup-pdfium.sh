#!/bin/bash

set -e

# Directory setup
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/.."
DOWNLOAD_DIR="$SCRIPT_DIR/pdfium_download"
TARGET_INCLUDE_DIR="$PROJECT_DIR/src/main/cpp/include"
TARGET_LIB_DIR="$PROJECT_DIR/src/main/jniLibs"
VERSION_FILE="$PROJECT_DIR/src/main/cpp/pdfium_version.txt"

PDFIUM_VERSION="$1"
PDFIUM_URL_BASE="https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F${PDFIUM_VERSION}"
ABI_MAPPING=(
    "armeabi-v7a pdfium-android-arm.tgz"
    "arm64-v8a pdfium-android-arm64.tgz"
    "x86_64 pdfium-android-x64.tgz"
    "x86 pdfium-android-x86.tgz"
)

usage() {
    echo "Usage: $0 <version> [--clean] [--check-updates]"
    echo "Example: $0 7215"
}

download_and_extract_pdfium() {
    mkdir -p "$DOWNLOAD_DIR"
    HEADER_COPIED=false

    for MAPPING in "${ABI_MAPPING[@]}"; do
        ABI=$(echo "$MAPPING" | awk '{print $1}')
        ARCHIVE_NAME=$(echo "$MAPPING" | awk '{print $2}')
        URL="${PDFIUM_URL_BASE}/${ARCHIVE_NAME}"
        ARCHIVE_PATH="$DOWNLOAD_DIR/$ARCHIVE_NAME"

        echo "Downloading $URL..."
        curl -L -o "$ARCHIVE_PATH" --fail "$URL" || { echo "Failed to download $URL"; exit 1; }

        mkdir -p "$DOWNLOAD_DIR/$ABI"
        tar -xzf "$ARCHIVE_PATH" -C "$DOWNLOAD_DIR/$ABI" || { echo "Failed to extract $ARCHIVE_NAME"; exit 1; }

        if [ "$HEADER_COPIED" = false ]; then
            echo "Copying headers from $ABI..."
            mkdir -p "$TARGET_INCLUDE_DIR"
            cp -r "$DOWNLOAD_DIR/$ABI/include/"* "$TARGET_INCLUDE_DIR/"
            HEADER_COPIED=true
        fi

        echo "Copying binary for $ABI..."
        mkdir -p "$TARGET_LIB_DIR/$ABI"
        cp "$DOWNLOAD_DIR/$ABI/lib/libpdfium.so" "$TARGET_LIB_DIR/$ABI/"
    done

    echo "Recording version $PDFIUM_VERSION to $VERSION_FILE"
    echo "$PDFIUM_VERSION" > "$VERSION_FILE"
}

clean() {
    echo "Cleaning up..."
    rm -rf "$DOWNLOAD_DIR"
}

clean_project() {
    echo "Cleaning project and downloaded files..."
    rm -rf "$DOWNLOAD_DIR"
    rm -rf "$TARGET_INCLUDE_DIR"
    rm -rf "$TARGET_LIB_DIR"
    rm -f "$VERSION_FILE"
}

check_updates() {
    echo "Checking for latest PDFium release..."
    CURRENT_VERSION="(not set)"

    if [[ -f "$VERSION_FILE" ]]; then
        CURRENT_VERSION=$(<"$VERSION_FILE")
    fi

    RAW_LATEST=$(curl -s "https://api.github.com/repos/bblanchon/pdfium-binaries/releases/latest" | grep tag_name | cut -d '"' -f 4)
    LATEST="${RAW_LATEST##*/}"

    echo "Current version: $CURRENT_VERSION"
    echo "Latest available: $LATEST"

    if [[ "$CURRENT_VERSION" == "(not set)" ]]; then
        echo "To get started: ./scripts/setup-pdfium.sh $LATEST"
    fi
}

case "$1" in
    --check-updates)
        check_updates
        exit 0
        ;;
    --clean)
        clean_project
        exit 0
        ;;
esac

if [ -z "$PDFIUM_VERSION" ]; then
    usage
    exit 1
fi

download_and_extract_pdfium
clean
echo "✅ PDFium setup complete for version $PDFIUM_VERSION."
