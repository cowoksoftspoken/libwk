#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

export PATH="/c/msys64/ucrt64/bin:$PATH"

find_sample_photo() {
    local preferred=(
        "photos/people/ember-7f3a.jpg"
        "photos/ember-7f3a.jpg"
    )
    local candidate
    for candidate in "${preferred[@]}"; do
        if [ -f "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    find photos -type f \( -iname '*.jpg' -o -iname '*.jpeg' \) | sort | head -n 1
}

INPUT_JPG="$(find_sample_photo)"
if [ -z "$INPUT_JPG" ] || [ ! -f "$INPUT_JPG" ]; then
    echo "ERROR: missing input photo under photos/"
    exit 1
fi

INPUT_DIR="$(dirname -- "$INPUT_JPG")"
INPUT_STEM="${INPUT_JPG%.*}"
WK_LOSSLESS="${INPUT_STEM}_lossless.wk"
WK_LOSSY="${INPUT_STEM}_lossy.wk"
DECODED_LOSSLESS="${INPUT_DIR}/decoded_lossless.png"
DECODED_LOSSY="${INPUT_DIR}/decoded_lossy.png"
META_JSON="${INPUT_DIR}/metadata.json"

echo "=== Input ==="
echo "$INPUT_JPG"

echo "=== Versions ==="
./build/wkenc.exe --version

echo "=== Lossless JPEG -> WK ==="
./build/wkenc.exe --lossless "$INPUT_JPG" "$WK_LOSSLESS"
./build/wkdec.exe --info "$WK_LOSSLESS"
./build/wkdec.exe "$WK_LOSSLESS" "$DECODED_LOSSLESS"

echo "=== Lossy JPEG -> WK ==="
./build/wkenc.exe --quality 75 "$INPUT_JPG" "$WK_LOSSY"
./build/wkdec.exe --info "$WK_LOSSY"
./build/wkdec.exe "$WK_LOSSY" "$DECODED_LOSSY"
./build/wkmetric.exe "$INPUT_JPG" "$WK_LOSSY"

echo "=== Metadata Edit / Export ==="
./build/wkmeta-edit.exe --set CONTENT.TITLE "en:Test Photo" --set GEO.LAT 1.23 "$WK_LOSSY"
./build/wkdec.exe --export-meta "$META_JSON" --info "$WK_LOSSY" >/dev/null

echo "=== Outputs ==="
ls -lh "$WK_LOSSLESS" "$WK_LOSSY" "$DECODED_LOSSLESS" "$DECODED_LOSSY" "$META_JSON"
echo "Done."