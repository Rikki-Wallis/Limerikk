#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Verify emcc is available
if ! command -v emcc &>/dev/null; then
    echo "Error: emcc not found. Install Emscripten and run 'source emsdk_env.sh'."
    exit 1
fi

mkdir -p "$BUILD_DIR"

echo "==> Configuring..."
emcmake cmake "$SCRIPT_DIR" \
    -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release

echo "==> Building..."
emmake cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

echo "==> Copying outputs to repo root..."
WEBUI_DIR="$SCRIPT_DIR/.."
cp "$BUILD_DIR/limerikk_wasm.js"   "$WEBUI_DIR/limerikk_wasm.js"
cp "$BUILD_DIR/limerikk_wasm.wasm" "$WEBUI_DIR/limerikk_wasm.wasm"
cp "$BUILD_DIR/limerikk_wasm.worker.js" "$WEBUI_DIR/limerikk_wasm.worker.js" 2>/dev/null || true

echo ""
echo "Done. Serve with:"
echo "  python3 -m http.server 8080 --directory $WEBUI_DIR"
