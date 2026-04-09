#!/bin/bash
set -euo pipefail

export PATH="/c/msys64/ucrt64/bin:$PATH"

INPUT_JPG="photos/1230927884722.jpg"
WK_LOSSLESS="photos/1230927884722_lossless.wk"
WK_LOSSY="photos/1230927884722_lossy.wk"
DECODED_LOSSLESS="photos/decoded_lossless.png"
DECODED_LOSSY="photos/decoded_lossy.png"
META_JSON="photos/metadata.json"

if [ ! -f "$INPUT_JPG" ]; then
    echo "ERROR: missing input photo: $INPUT_JPG"
    exit 1
fi

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

echo "=== Metadata Edit / Export ==="
./build/wkmeta-edit.exe --set CONTENT.TITLE "en:Test Photo" --set GEO.LAT 1.23 "$WK_LOSSY"
./build/wkdec.exe --export-meta "$META_JSON" --info "$WK_LOSSY" >/dev/null

echo "=== Outputs ==="
ls -lh "$WK_LOSSLESS" "$WK_LOSSY" "$DECODED_LOSSLESS" "$DECODED_LOSSY" "$META_JSON"
echo "Done."
