# Content-Aware Perceptual Control Field

This document describes WK’s key differentiator: a content-aware control field that guides encoder decisions globally.

## Core Idea

Encoder builds a low/medium resolution representation containing:

- importance
- content type
- edge strength
- texture level
- flatness
- color sensitivity
- restoration preference

This field controls:

- local quant bias
- transform/block selection
- tool pruning
- post-filter strength
- grain/noise policy

## Why It Matters

Unlike typical codecs, WK uses one unified field to coordinate multiple subsystems.

Benefits:

- consistent decisions
- easier tuning
- better global coherence

## Candidate Regions

- text/UI
- skin
- sky
- foliage
- flat graphics
- edges
- fine texture
- dark/highlight detail
- noisy source

## Field Layout

- grid per 16x16 or 32x32
- stored as low-res map
- interpolated during encoding

Channels:

- importance
- activity
- edge strength
- texture strength
- content biases (skin, sky, graphics, grain)

## Encoder Inputs

- luma/chroma variance
- gradients
- edge coherence
- color distribution
- saturation
- dark/highlight ratio
- palette similarity
- repetition score

## Encoder Outputs

### Quant Bias
- important regions: finer quant
- less important: aggressive quant

### Transform Decisions
- small blocks for edges/text
- large blocks for smooth areas

### Tool Pruning
- avoid expensive tools where unnecessary

### Post-Filtering
- stronger in smooth areas
- weaker near edges

### Grain Policy
- reintroduce noise selectively

## Rollout

### Stage 1
- encoder-only
- visualize field
- basic quant bias

### Stage 2
- affects quant, mode search, blocks

### Stage 3
- integrated into syntax/control

## Risks

- too complex without gain
- wrong heuristics harming quality
- bitstream overhead

## Mitigation

- start encoder-side only
- measure impact
- commit only if proven useful

## Success Criteria

- bitrate reduction at same quality OR
- quality improvement at same bitrate
- fewer artifacts
