# WK Benchmark Notes

## Current Scope

This repo now has a repeatable measurement path for still-image work.
The current benchmarking surface is still focused on still images on Windows, with:

- `wkenc` for producing `.wk`
- `wkmetric` for comparing a source image against a decoded `.wk` or another image
- `utils/benchmark_corpus.ps1` for running the recursive corpus under `photos/`
- `wkview` for visual confirmation after the numeric pass

This is still not a finalized public benchmark report against JPEG, WebP, or AVIF.
It is the measurement harness we use to keep codec changes honest.

## Quick Measurement

Measure a single sample image against a `.wk` file:

```powershell
.\build\wkmetric.exe photos\people\ember-7f3a.jpg photos\people\ember-7f3a_clean.wk
```

Typical output includes:

- source and candidate file sizes
- `MAE`
- `MSE`
- `PSNR`
- `SSIM`
- RGB per-channel breakdown, Y/Cb/Cr breakdown, weighted chroma artifact metrics, and reference/candidate luminance statistics

You can also emit JSON for automation:

```powershell
.\build\wkmetric.exe --json photos\people\ember-7f3a.jpg photos\people\ember-7f3a_clean.wk
```

## Corpus Workflow

Run the recursive photo corpus in `photos/` with the current default lossy profile:

```powershell
.\utils\benchmark_corpus.ps1 -Quality 75 -Subsampling 444
```

Run an explicit `4:2:0` comparison pass:

```powershell
.\utils\benchmark_corpus.ps1 -Quality 75 -Subsampling 420
```

Run a lossless corpus pass:

```powershell
.\utils\benchmark_corpus.ps1 -Lossless
```

Run a JPEG-signature-only pass when you want to exclude disguised inputs:

```powershell
.\utils\benchmark_corpus.ps1 -Quality 75 -Subsampling 444 -FormatFilter jpeg
```

The script walks `photos/people/` and `photos/scenery/` recursively, writes encoded outputs under `benchmark/`, and produces both row-level summaries and rollups.
Benchmark filenames flatten the recursive path into the stem, for example `benchmark/people_mira-5b8d_q85_yuv444.wk`.

The row JSON files include:

- `relative_path`
- `scene_group`
- `lighting_bucket`
- `declared_format`
- `detected_format`
- `source_mean_luma`
- `source_luma_stddev`
- `source_mean_chroma`
- `y_psnr`
- `chroma_psnr`
- `weighted_chroma_mae`
- `max_abs_error`

The rollup JSON files aggregate the same benchmark by:

- overall corpus
- `scene_group`
- `lighting_bucket`
- `scene_group + lighting_bucket`

## Current Snapshot In This Tree

The current checked-in summaries are:

- `benchmark/summary_q75_yuv444.json`
- `benchmark/summary_q85_yuv444.json`
- `benchmark/rollup_q75_yuv444.json`
- `benchmark/rollup_q85_yuv444.json`
- `benchmark/summary_q75_yuv444_srcjpeg.json`
- `benchmark/summary_q85_yuv444_srcjpeg.json`
- `benchmark/rollup_q75_yuv444_srcjpeg.json`
- `benchmark/rollup_q85_yuv444_srcjpeg.json`

Across the current six-image recursive corpus, the mixed-extension averages are:

| Profile | Total WK bytes | Avg PSNR | Avg SSIM |
| --- | ---: | ---: | ---: |
| `q75_yuv444` | `1,168,901` | `35.5446` | `0.948752` |
| `q85_yuv444` | `1,650,520` | `38.4155` | `0.968006` |

Important corpus note:

- `photos/people/solis-2c9e.jpg` has a WebP signature
- `photos/scenery/horizon-4e2a.jpg` has a PNG signature
- the main `photos/` run is therefore a mixed extension corpus, not a pure JPEG corpus

The rollups make the current strengths and weaknesses clearer:

- `people` at `q75`: `92,659` bytes total, `PSNR 36.2101`, `SSIM 0.969126`
- `people` at `q85`: `129,732` bytes total, `PSNR 39.1216`, `SSIM 0.982421`
- `scenery` at `q75`: `1,076,242` bytes total, `PSNR 34.8792`, `SSIM 0.928377`
- `scenery` at `q85`: `1,520,788` bytes total, `PSNR 37.7094`, `SSIM 0.953590`
- the brightest scenery case `photos/scenery/horizon-4e2a.jpg` is the hardest current stress sample: `q75 SSIM 0.883296`, `q85 SSIM 0.909543`

The JPEG-signature-only subset keeps the comparison cleaner when you want to remove those disguised files:

| Profile | Total WK bytes | Avg PSNR | Avg SSIM |
| --- | ---: | ---: | ---: |
| `q75_yuv444_srcjpeg` | `911,543` | `35.3594` | `0.959927` |
| `q85_yuv444_srcjpeg` | `1,246,442` | `38.6156` | `0.979705` |

Example from the current tree:

- `photos/people/mira-5b8d.jpg`: `6,072` bytes
- `benchmark/people_mira-5b8d_q75_yuv444.wk`: `14,865` bytes, `PSNR 36.8016`, `SSIM 0.972798`
- `benchmark/people_mira-5b8d_q85_yuv444.wk`: `19,160` bytes, `PSNR 39.5566`, `SSIM 0.985293`

## Smoke Workflow

Use `photos/people/ember-7f3a.jpg` as the preferred regression target.
A practical smoke sequence is:

```bash
./build/wkenc.exe --lossless photos/people/ember-7f3a.jpg photos/people/ember-7f3a_lossless.wk
./build/wkdec.exe photos/people/ember-7f3a_lossless.wk photos/people/decoded_lossless.png
./build/wkenc.exe --quality 75 photos/people/ember-7f3a.jpg photos/people/ember-7f3a_lossy.wk
./build/wkdec.exe photos/people/ember-7f3a_lossy.wk photos/people/decoded_lossy.png
./build/wkmetric.exe photos/people/ember-7f3a.jpg photos/people/ember-7f3a_lossy.wk
./build/wkview.exe photos/people/ember-7f3a_lossy.wk photos/people/ember-7f3a.jpg
```

There is also a helper smoke script at:

- `utils/test_codec.sh`

That script now resolves the repo root, prefers the `people` sample, and falls back to the first recursive JPEG it finds under `photos/`.

## What Is Verified

- core codec tests cover `rANS`, `DCT/IDCT`, container parsing, prediction, metadata round-trip, geometry metadata, and metric correctness
- integration tests cover the real sample JPEGs from the recursive `photos/` corpus for lossless and lossy encode/decode
- lossy photo regression now has objective thresholds for both `PSNR` and `SSIM`, with additional chroma-sensitive inspection through `wkmetric`
- container parsing rejects malformed streams such as missing `FEND`, invalid tile payload sizes, and unknown required chunks
- lossy encode preserves alpha in the `WK` tile payload instead of dropping or forcing opacity
- PNG output is intentionally limited to 8-bit decode output until a higher-bit-depth writer is added

## How To Read The Numbers

Use the metrics this way:

- `PSNR` is a fast sanity metric and should not be the only judge
- `SSIM` is the better default guard for visible structure changes
- `MAE` is useful for spotting broad color drift
- `chroma_psnr` and `weighted_chroma_mae` are the faster checks for small but annoying color artifacts
- `scene_group`, `lighting_bucket`, and `source_mean_luma` help separate portrait-friendly cases from hard scenery cases
- `wkview` remains the final human check for ringing, chroma artifacts, and local damage

## Viewer Reminder

If you want to inspect a benchmark result visually, use the full benchmark filename:

```powershell
.\build\wkview.exe .\benchmark\people_mira-5b8d_q85_yuv444.wk .\photos\people\mira-5b8d.jpg
```

If you accidentally omit the profile suffix, the current viewer tries to suggest nearby filenames instead of only failing with a generic open error.

## What This File Does Not Claim Yet

- no finalized WebP or AVIF comparison table is published here
- no external SSIMULACRA2 runner is bundled in this repo yet
- no HDR display benchmark is claimed for the viewer; HDR preview is still a deterministic 8-bit downmap
- no cross-platform JPEG/PNG corpus harness is claimed outside the current Windows-first image I/O path

When broader comparative benchmarks are ready, extend this note with exact command lines, corpus definitions, and frozen result tables.