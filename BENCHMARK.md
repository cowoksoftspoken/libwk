# WK Benchmark Notes

## Current Scope

This repo is currently validated as a still-image codec workflow, not a finalized benchmark report.
The supported day-to-day path in this tree is:

- Encode from `jpg`, `jpeg`, `png`, or `ppm` input on Windows.
- Decode to `png` or `ppm` for 8-bit output.
- Use `wkview <file.wk> [source.jpg]` for side-by-side visual comparison.
- Import EXIF into WKMETA through `wkenc --import-exif` or `wkmeta-edit --import-exif`.

## Smoke Workflow

Use the bundled sample photo in `photos/1230927884722.jpg` as the basic regression target.
A practical smoke sequence is:

```bash
./build/wkenc.exe --lossless photos/1230927884722.jpg photos/test_lossless.wk
./build/wkdec.exe photos/test_lossless.wk photos/decoded_lossless.png
./build/wkenc.exe --quality 75 photos/1230927884722.jpg photos/test_lossy.wk
./build/wkdec.exe photos/test_lossy.wk photos/decoded_lossy.png
./build/wkview.exe photos/test_lossy.wk photos/1230927884722.jpg
```

## What Is Verified

- Core codec tests cover `rANS`, `DCT/IDCT`, container parsing, prediction, metadata round-trip, and geometry metadata.
- Integration tests cover the real sample JPEG from `photos/` for both lossless and lossy encode/decode.
- Container parsing rejects malformed streams such as missing `FEND`, invalid tile payload sizes, and unknown required chunks.
- Lossy encode preserves alpha in the WK tile payload instead of dropping or forcing opacity.
- PNG output is intentionally limited to 8-bit decode output until a higher-bit-depth writer is added.

## What This File Does Not Claim Yet

- No finalized WebP or AVIF comparison numbers are published here.
- No Kodak dataset benchmark table is maintained yet.
- No HDR display benchmark is claimed for the viewer; HDR preview is a deterministic 8-bit downmap.

When comparative benchmarks are ready, replace this note with measured data and the exact command lines used to produce it.
