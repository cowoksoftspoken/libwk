# WK Web Codec Studio

WebAssembly-powered encoder and decoder for the WK image format. Runs entirely in-browser with no server-side processing.

## Features

- **Decode** `.wk` files and render them on an HTML Canvas
- **Encode** `.png` / `.jpg` images into `.wk` format
- Lossy and lossless mode selection with quality slider
- Luma histogram visualization
- File metadata display (dimensions, color space, file size)
- Download encoded `.wk` output
- Responsive layout (desktop and mobile)

## Prerequisites

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
- A local HTTP server (WASM files require proper MIME types)

## Building

### Windows (PowerShell)

```powershell
cd web
.\emsdk\emsdk_env.ps1
.\build.ps1
```

### Linux / macOS / MSYS2

```bash
cd web
source emsdk/emsdk_env.sh
./build.sh
```

Both scripts compile all WK codec source files and `wk_wasm.cpp` into `wk.js` and `wk.wasm`.

## Running

After building, start any local HTTP server from the `web/` directory:

```bash
python3 -m http.server 8000
```

Open `http://localhost:8000` in a browser.

## File Structure

| File           | Purpose                                      |
|----------------|----------------------------------------------|
| `index.html`   | Application shell and UI layout              |
| `style.css`    | Visual design (glassmorphism, responsive)     |
| `app.js`       | WASM integration, encode/decode logic, UI     |
| `wk_wasm.cpp`  | C++ bindings exposing WK codec API to WASM    |
| `build.ps1`    | PowerShell build script for Emscripten        |
| `build.sh`     | Bash build script for Emscripten              |
| `wk.js`        | Generated Emscripten JS glue (build output)   |
| `wk.wasm`      | Generated WebAssembly binary (build output)   |

## WASM API

The following C functions are exported to JavaScript via Emscripten:

### `wk_wasm_decode`

```c
int wk_wasm_decode(
    const uint8_t* data, size_t size,
    uint32_t* out_width, uint32_t* out_height,
    uint32_t* out_bpp, uint32_t* out_pixels_ptr
);
```

Decodes a `.wk` file buffer. Returns 0 on success. Output parameters are written via pointers allocated with `_malloc(4)` and read with `getValue(ptr, "i32")`.

### `wk_wasm_encode`

```c
int wk_wasm_encode(
    const uint8_t* pixels, uint32_t width, uint32_t height,
    uint8_t bpp, float quality, uint8_t lossless,
    uint32_t* out_size, uint32_t* out_data_ptr
);
```

Encodes raw pixel data into WK format. Returns 0 on success.

### `wk_wasm_alloc` / `wk_wasm_free`

Memory management helpers for passing data between JS and WASM.

### `wk_wasm_version`

Returns a pointer to the WK version string. Use `UTF8ToString()` to read it in JS.

## License

Apache License 2.0. See the root LICENSE file.
