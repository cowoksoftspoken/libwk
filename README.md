# WK

WK is an experimental still-image codec and container with a custom `.wk` bitstream, metadata system, CLI tools, and a compare viewer.

This repo is currently focused on getting the still-image workflow correct and usable:

- `JPEG/PNG/PPM -> WK`
- `WK -> PNG/PPM`
- metadata import/edit/export through `WKMETA`
- side-by-side visual comparison with `wkview`

The project is not positioned yet as a finished replacement for JPEG, WebP, or AVIF. The codec core works, the tools are usable, and the viewer is buildable, but compression efficiency and feature parity with mature production codecs are still in progress.

## Current Status

What works well now:

- lossless still-image encode/decode
- lossy still-image encode/decode
- lossy alpha preserved inside the `WK` tile payload
- strict container parsing with explicit failure on malformed files
- EXIF import into `WKMETA`
- PNG decode output for 8-bit images
- compare viewer for `WK` vs source image
- C and C++ public APIs
- unit and integration tests passing in the current tree

What is intentionally not claimed as finished yet:

- full animation encode/decode, even though the spec and container have animation structures
- native `.webp` input/output support
- advanced HDR output writers
- full ICC-aware color-managed display in the viewer
- compression efficiency competitive with mature JPEG/WebP/AVIF encoders

## Features

### Container and bitstream

- little-endian `.wk` container with `FHDR`, `META`, `ICCP`, `PROV`, `ANIM`, `TILE`, and `FEND` chunks
- strict parser rules:
  - `FHDR` must be first
  - `FEND` must exist and be last
  - tile payload sizes are validated
  - unknown required chunks are rejected
  - unknown optional chunks are skipped safely
- tiled image layout with configurable tile size

### Codec core

- lossless path with predictor + rANS
- lossy path with:
  - RGB -> YCbCr conversion
  - YUV444 or YUV420 chroma
  - 8x8 DCT/IDCT
  - quantization
  - intra prediction
  - rANS entropy coding
- lossy alpha extension stored directly in the tile payload when alpha is present
- subsampling inferred from tile payload at decode time instead of hardcoded assumptions

### Metadata

- `WKMETA` structured metadata chunk
- CLI editing via `wkmeta-edit`
- EXIF import from JPEG/TIFF donor files
- metadata export as JSON

### Viewer

- `wkview <file.wk> [source.jpg]`
- decoded image preview
- side-by-side compare mode if a source image is provided
- file labels rendered directly on the canvas:
  - `WK: ...`
  - `SOURCE: ...`
- fit-to-window rendering
- `Esc` to exit

## Platform Notes

The repo currently has the best day-to-day experience on Windows.

### Windows

- JPEG and PNG image I/O use Windows WIC
- viewer is supported and buildable
- MinGW runtime DLLs are copied next to built executables automatically in the current CMake setup

### Non-Windows

- the core library builds more portably than the image I/O layer
- current shared image I/O only guarantees `PPM` outside Windows
- JPEG/PNG convenience paths in this repo are effectively Windows-first right now

## Build

### Requirements

- CMake `>= 3.28`
- a C++23 compiler
- on Windows, the repo is currently set up and tested with MSYS2 UCRT64 + MinGW
- Ninja is recommended but not strictly required

### Windows example

If you use MSYS2 UCRT64, make sure its `bin` directory is on `PATH`.

```powershell
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH
cmake -S . -B build -G Ninja
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Dependencies are fetched automatically by CMake:

- `minifb` for the viewer
- `googletest` for tests

## Quick Start

The bundled sample photo is:

- `photos/1230927884722.jpg`

### Encode lossless

```powershell
.\build\wkenc.exe --lossless photos\1230927884722.jpg photos\test_lossless.wk
```

### Encode lossy

```powershell
.\build\wkenc.exe --quality 75 photos\1230927884722.jpg photos\test_lossy.wk
```

### Decode

```powershell
.\build\wkdec.exe photos\test_lossy.wk photos\decoded_lossy.png
```

### Inspect info

```powershell
.\build\wkdec.exe --info photos\test_lossy.wk
```

### Compare visually

```powershell
.\build\wkview.exe photos\test_lossy.wk photos\1230927884722.jpg
```

### Smoke script

A basic end-to-end workflow is also scripted in:

- `test_codec.sh`

## CLI Tools

### `wkenc`

Usage:

```text
wkenc [options] <input.{jpg,jpeg,png,ppm}> [output.wk]
```

Main options:

- `--quality N` for lossy quality `0..100`
- `--lossless`
- `--tile-size N`
- `--threads N`
- `--target-ssimulacra2 N`
- `--yuv444`
- `--yuv420`
- `--import-exif FILE`

### `wkdec`

Usage:

```text
wkdec [options] <input.wk> [output.{png,ppm}]
```

Main options:

- `--info`
- `--export-meta FILE`

Notes:

- `.png` output is only available for 8-bit decoded images
- higher bit depths should use `.ppm` for now, or fail clearly if unsupported

### `wkmeta-dump`

Prints `WKMETA` as JSON.

### `wkmeta-edit`

Usage:

```text
wkmeta-edit [options] <input.wk>
```

Main options:

- `--set NS.TAG VALUE`
- `--delete NS.TAG`
- `--import-exif FILE`
- `--export-json`
- `-o, --output FILE`

### `wkview`

Usage:

```text
wkview <file.wk> [source.{jpg,jpeg,png,ppm}]
```

Behavior:

- with only `.wk`, it shows the decoded image
- with a second image path, it shows side-by-side compare
- the left panel is the decoded `WK`
- the right panel is the source image

## Public API

### C++ API

Headers:

- `include/wk/wk.hpp`
- `include/wk/wkmeta.hpp`

Main entry points:

- `wk::encode(...)`
- `wk::decode(...)`
- `wk::get_info(...)`

### C API

Header:

- `include/wk/wk.h`

Main entry points:

- `wk_encode(...)`
- `wk_decode(...)`
- `wk_get_info(...)`
- `wk_free(...)`
- `wk_version(...)`

## Compression Reality Check

The codec is functionally working, but byte-size competitiveness is not the strongest part yet.

On the bundled sample JPEG `photos/1230927884722.jpg`, the measured sizes in the current tree were:

- source JPEG: `12,462` bytes
- `WK` lossless: `74,855` bytes
- `WK` lossy quality 75: `20,382` bytes
- `WK` lossy quality 90: `32,576` bytes

That means:

- visual quality is already decent for still-image testing
- container correctness is in a good place
- current `WK` output is still larger than the original JPEG on this sample

Also note that the encoder's printed ratio is against raw raster size, not against the input JPEG file size.

## Tests

Current test targets include:

- `rans_test`
- `dct_test`
- `roundtrip_test`
- `container_test`
- `predict_test`
- `wkmeta_test`
- `geo_roundtrip_test`

The current validated path includes:

- lossless round-trip
- lossy round-trip
- lossy alpha round-trip
- malformed entropy stream rejection
- malformed container rejection
- metadata round-trip
- JPEG integration on Windows using the sample photo in `photos/`

Run everything with:

```powershell
ctest --test-dir build --output-on-failure
```

## `wkview` Issue That Happened and the Fix

A Windows runtime problem previously affected `wkview`.

### Symptom

Launching the viewer could show this popup:

```text
LoadLibrary failed with error 1114: A dynamic link library (DLL) initialization routine failed.
```

This often appeared when running from VS Code or Explorer even if the binary had built successfully.

### Root causes

There were two separate problems:

1. The executable could depend on MinGW runtime DLLs that were available in the build shell but not beside the executable when launched from another environment.
2. `MiniFB` could try to initialize its OpenGL backend on Windows, and that backend path could fail on some systems during window creation.

### Fix in this repo

The current CMake setup fixes both issues:

- MinGW runtime DLLs are copied next to built executables after build.
- On Windows, `MiniFB` is forced to use the non-OpenGL backend instead of the problematic OpenGL path.

Relevant implementation is in `CMakeLists.txt`:

- runtime DLL copy helper for Windows executables
- `MINIFB_USE_OPENGL_API OFF` on Windows before building `wkview`

### What to do if you still see the popup

- make sure you are running the newest binary from `build\wkview.exe`
- close any old `wkview` process that may still be open
- rebuild once:

```powershell
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH
cmake --build build --parallel
```

- run the viewer again from the updated `build` directory

## Metadata Docs and Format Docs

For deeper reference, see:

- `SPEC.md` for the bitstream/container format
- `META_GUIDE.md` for metadata usage
- `BENCHMARK.md` for the current benchmark/testing notes

## Repository Layout

```text
include/   public headers
src/       codec, container, metadata, image I/O
tools/     CLI tools and viewer
tests/     unit and integration tests
photos/    sample/testing assets
```

## Known Limitations

- native `.webp` input/output is not implemented
- animation structures exist in the format, but animation workflow is not complete yet
- JPEG/PNG convenience I/O is Windows-first in this repo
- PNG writing is 8-bit only
- viewer HDR preview is a deterministic 8-bit preview, not full color-managed HDR display
- compression still needs significant tuning to compete on file size

## Suggested Next Steps

If development continues, the highest-impact next work items are:

1. improve compression efficiency for lossy still images
2. finish animation encode/decode if animated parity is a goal
3. add wider cross-platform image I/O
4. add native WebP input/output if interchange with `.webp` is desired
5. improve HDR and ICC handling end-to-end
