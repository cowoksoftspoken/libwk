# WK Benchmark Notes

## Current Scope

This repo now has a repeatable measurement path for still-image work.
The current benchmarking surface is still focused on still images on Windows, with:

- `wkenc` for producing `.wk`
- `wkmetric` for comparing a source image against a decoded `.wk` or another image
- `utils/benchmark_corpus.ps1` for running the bundled photo corpus in `photos/`
- `wkview` for visual confirmation after the numeric pass

This is still not a finalized public benchmark report against JPEG, WebP, or AVIF.
It is the measurement harness we use to keep codec changes honest.

## Quick Measurement

Measure a single sample image against a `.wk` file:

```powershell
.\build\wkmetric.exe photos\1230927884722.jpg photos\1230927884722_clean.wk
```

Typical output includes:

- source and candidate file sizes
- `MAE`
- `MSE`
- `PSNR`
- `SSIM`
- per-channel breakdown for `R`, `G`, `B`, and `A` when alpha is compared

You can also emit JSON for automation:

```powershell
.\build\wkmetric.exe --json photos\1230927884722.jpg photos\1230927884722_clean.wk
```

## Corpus Workflow

Run the bundled photo corpus in `photos/` with the current default lossy profile:

```powershell
powershell -ExecutionPolicy Bypass -File .\utils\benchmark_corpus.ps1 -Quality 75 -Subsampling 444
```

Run an explicit `4:2:0` comparison pass:

```powershell
powershell -ExecutionPolicy Bypass -File .\utils\benchmark_corpus.ps1 -Quality 75 -Subsampling 420
```

Run a lossless corpus pass:

```powershell
powershell -ExecutionPolicy Bypass -File .\utils\benchmark_corpus.ps1 -Lossless
```

The script writes encoded outputs and a JSON summary under `benchmark/`.
Benchmark filenames include the full profile suffix, for example `benchmark/2334937028374_q85_yuv444.wk`.
The JSON rows now include `declared_format` and `detected_format`, and the runner warns when a file extension does not match the file signature.

## Current Snapshot In This Tree

The current checked-in summaries are:

- `benchmark/summary_q75_yuv444.json`
- `benchmark/summary_q85_yuv444.json`

Across the bundled three-image photo corpus, the current averages are:

In the current tree, `20981203812.jpg` has a JPEG extension but a WebP signature, so the checked-in corpus should be read as a mixed photo corpus, not a pure JPEG corpus.

| Profile | Total WK bytes | Avg PSNR | Avg SSIM |
| --- | ---: | ---: | ---: |
| `q75_yuv444` | `91,849` | `36.1589` | `0.968494` |
| `q85_yuv444` | `128,567` | `39.0992` | `0.982096` |

A practical reading of those numbers:

- `q75` is the more size-aware baseline profile
- `q85` is visibly cleaner and structurally stronger
- `q85` still costs substantially more bytes, so rate-distortion tuning remains active work

Example from the current tree:

- `photos/2334937028374.jpg`: `6,072` bytes
- `benchmark/2334937028374_q75_yuv444.wk`: `14,771` bytes, `PSNR 36.7899`, `SSIM 0.972625`
- `benchmark/2334937028374_q85_yuv444.wk`: `18,870` bytes, `PSNR 39.5723`, `SSIM 0.984899`

## Smoke Workflow

Use the bundled sample photo in `photos/1230927884722.jpg` as the basic regression target.
A practical smoke sequence is:

```bash
./build/wkenc.exe --lossless photos/1230927884722.jpg photos/test_lossless.wk
./build/wkdec.exe photos/test_lossless.wk photos/decoded_lossless.png
./build/wkenc.exe --quality 75 photos/1230927884722.jpg photos/test_lossy.wk
./build/wkdec.exe photos/test_lossy.wk photos/decoded_lossy.png
./build/wkmetric.exe photos/1230927884722.jpg photos/test_lossy.wk
./build/wkview.exe photos/test_lossy.wk photos/1230927884722.jpg
```

There is also a helper smoke script at:

- `utils/test_codec.sh`

That script now resolves the repo root before running, so it can be launched from outside the repo root without breaking its file paths.

## What Is Verified

- core codec tests cover `rANS`, `DCT/IDCT`, container parsing, prediction, metadata round-trip, geometry metadata, and metric correctness
- integration tests cover the real sample JPEGs from `photos/` for both lossless and lossy encode/decode
- lossy photo regression now has objective thresholds for both `PSNR` and `SSIM`
- container parsing rejects malformed streams such as missing `FEND`, invalid tile payload sizes, and unknown required chunks
- lossy encode preserves alpha in the `WK` tile payload instead of dropping or forcing opacity
- PNG output is intentionally limited to 8-bit decode output until a higher-bit-depth writer is added

## How To Read The Numbers

Use the metrics this way:

- `PSNR` is a fast sanity metric and should not be the only judge
- `SSIM` is the better default guard for visible structure changes
- `MAE` is useful for spotting broad color drift
- `wkview` remains the final human check for ringing, chroma artifacts, and local damage

## Viewer Reminder

If you want to inspect a benchmark result visually, use the full benchmark filename:

```powershell
.\build\wkview.exe .\benchmark\2334937028374_q85_yuv444.wk .\photos\2334937028374.jpg
```

If you accidentally omit the profile suffix, the current viewer tries to suggest nearby filenames instead of only failing with a generic open error.

## What This File Does Not Claim Yet

- no finalized WebP or AVIF comparison table is published here
- no external SSIMULACRA2 runner is bundled in this repo yet
- no HDR display benchmark is claimed for the viewer; HDR preview is still a deterministic 8-bit downmap
- no cross-platform JPEG/PNG corpus harness is claimed outside the current Windows-first image I/O path

When broader comparative benchmarks are ready, extend this note with exact command lines, corpus definitions, and frozen result tables.