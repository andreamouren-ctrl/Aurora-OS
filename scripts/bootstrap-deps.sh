#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

LIMINE_VERSION="12.9.0"
LIMINE_SHA256="84059c93b4ea03994af6d614654c7095291388850ea7b258d64f9263abde5557"
LIMINE_PROTOCOL_COMMIT="da65184e91f80fcb397270121b1e2515a11e01ee"

THIRD_PARTY="$ROOT/third_party"
CACHE="$ROOT/.cache"
PROTOCOL_DIR="$THIRD_PARTY/limine-protocol"
LIMINE_DIR="$CACHE/limine-$LIMINE_VERSION"
ARCHIVE="$CACHE/limine-binary-$LIMINE_VERSION.tar.gz"

mkdir -p "$THIRD_PARTY" "$CACHE"

if [ ! -d "$PROTOCOL_DIR/.git" ]; then
    git clone https://github.com/Limine-Bootloader/limine-protocol.git "$PROTOCOL_DIR"
fi

git -C "$PROTOCOL_DIR" fetch --quiet origin
git -C "$PROTOCOL_DIR" checkout --quiet "$LIMINE_PROTOCOL_COMMIT"

if [ ! -d "$LIMINE_DIR" ]; then
    URL="https://github.com/Limine-Bootloader/Limine/releases/download/v$LIMINE_VERSION/limine-binary.tar.gz"

    if [ ! -f "$ARCHIVE" ]; then
        curl -fL "$URL" -o "$ARCHIVE"
    fi

    ACTUAL_SHA256=$(sha256sum "$ARCHIVE" | awk '{print $1}')

    if [ "$ACTUAL_SHA256" != "$LIMINE_SHA256" ]; then
        echo "Limine archive checksum mismatch." >&2
        echo "Expected: $LIMINE_SHA256" >&2
        echo "Actual:   $ACTUAL_SHA256" >&2
        exit 1
    fi

    TMP_DIR="$CACHE/limine-extract-$$"
    rm -rf "$TMP_DIR"
    mkdir -p "$TMP_DIR"

    tar -xzf "$ARCHIVE" -C "$TMP_DIR"

    EXTRACTED=$(find "$TMP_DIR" -mindepth 1 -maxdepth 1 -type d | head -n 1)

    if [ -z "$EXTRACTED" ]; then
        echo "Could not locate extracted Limine directory." >&2
        exit 1
    fi

    mv "$EXTRACTED" "$LIMINE_DIR"
    rm -rf "$TMP_DIR"
fi

echo "Aurora OS bootstrap dependencies are ready."
