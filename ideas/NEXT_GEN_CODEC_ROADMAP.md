# WK Next-Gen Codec Roadmap

This document organizes the target features of `WK` into a realistic sequence. The design can learn from `JPEG XL`, but the product direction remains WK’s own: modern, efficient, parallel-friendly, and open to new innovation.

## Positioning

Final targets of `WK`:

- strong for still image lossy and lossless
- ready for SDR, HDR, WCG, alpha, modern metadata, and mixed content
- has a clear progressive and streaming story
- has a deeper perceptual research path beyond simple global quantization

Core principles:

- do not stack advanced features on top of inefficient syntax
- quality tools come after a healthy bitstream foundation
- speed tools like SIMD come after kernel and syntax stability
- all major features must have clear dependencies

## Current State

What exists now:

- tiled still-image codec
- RGB to YCbCr pipeline
- 8x8 intra prediction + DCT/IDCT
- luma/chroma quantization with early perceptual tuning
- rANS entropy coding
- basic metadata import
- benchmark harness and compare viewer
- basic lossless path with predictor, palette branch, and rANS

Main issues:

- lossy syntax is still verbose
- entropy model is too expensive per tile and coefficient
- no strong control field for local visual decisions
- no post-filter integrated into bitrate-quality loop

## Phase 1: Syntax Efficiency Foundation

Goals:

- reduce size without harming current quality
- make bitstream compact before adding advanced tools

Main tasks:

- pack prediction mode stream
- redesign coefficient syntax:
  - significance signaling
  - zero-run or last-nonzero representation
  - more efficient sign and magnitude coding
- reduce entropy table overhead:
  - shared model bank
  - clustered tables
  - delta updates across tiles/groups
- reduce per-tile quant signaling if reconstructable
- refine lossless syntax:
  - efficient predictor signaling
  - stronger palette/backref path

Done criteria:

- q75 and q85 size reduction
- no meaningful visual regression
- strict decode correctness maintained

## Phase 2: Content-Aware Perceptual Control Field

Main differentiator.

Core idea:

- encoder builds a low/medium resolution field representing visual importance and content type
- influences more than just quantization

Guided decisions:

- local quant bias
- transform/block size preference
- tool selection and pruning
- filter strength
- grain/noise restoration policy

Dependency:

- efficient base syntax
- strong benchmark system

## Phase 3: Adaptive Quantization and Local Modeling

Tasks:

- quant map per superblock
- activity masking
- edge preservation bias
- content-specific bias (skin, sky, foliage, graphics)
- smarter chroma protection

Targets:

- better visual quality at same bitrate
- bitrate reduction where high fidelity not needed

## Phase 4: Multi-Scale Representation

Tasks:

- explicit LF image
- hierarchical DC/LF coding
- coarse-to-fine residual refinement
- progressive by resolution and precision

Benefits:

- fast preview
- streaming-ready
- stronger perceptual allocation

## Phase 5: Variable Transform and Partition System

Tasks:

- variable transform sizes
- square and rectangular transforms
- transform family selection
- richer partitioning
- transform skip or low-complexity paths

## Phase 6: Perceptual Post-Processing

Tasks:

- adaptive deblocking
- deringing
- edge-preserving restoration
- synthetic grain restoration
- encoder-aware tuning

## Phase 7: Non-Photo Tools

Tasks:

- palette-like mode
- reuse/patch tools
- text/UI-aware path
- line/curve-friendly mode
- mixed-content detection

## Phase 8: Modern Imaging

Tasks:

- HDR signaling
- WCG-aware coding
- ICC and gain-map pipeline
- alpha channel
- thumbnails
- auxiliary channels (depth, mask)

## Phase 9: Animation and Layering

Tasks:

- frame coding
- compositing
- inter-frame reference
- reuse mechanisms
- blending semantics

## Phase 10: ROI and Parallelism

Tasks:

- formal group structure
- parallel decode boundaries
- ROI decode
- cropped decode
- stream ordering

## Phase 11: Lossless Architecture

Tasks:

- stronger predictors
- improved transforms
- better backref/matching
- mixed-content support
- serious palette/modular path

## Priority Order

1. Syntax efficiency
2. Control field
3. Adaptive quant
4. Multi-scale
5. Transform/partition
6. Post-processing
7. Non-photo tools
8. HDR/WCG
9. ROI/parallel
10. Animation
11. Lossless
12. SIMD optimization

## Why SIMD Is Later

SIMD improves speed, not compression ratio. It should come after stable design.

## Current Interpretation

- long-term direction is clear
- daily work focuses on foundation
- each task must strengthen dependencies
