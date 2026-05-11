# Graph Report - src  (2026-05-11)

## Corpus Check
- Large corpus: 10808 files · ~9,296,688 words. Semantic extraction will be expensive (many Claude tokens). Consider running on a subfolder, or use --no-semantic to run AST-only.

## Summary
- 469 nodes · 892 edges · 41 communities (38 shown, 3 thin omitted)
- Extraction: 85% EXTRACTED · 15% INFERRED · 0% AMBIGUOUS · INFERRED: 137 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Viewer Utilities|Viewer Utilities]]
- [[_COMMUNITY_Lossy Quality & Analysis|Lossy Quality & Analysis]]
- [[_COMMUNITY_Coefficient Context Analysis|Coefficient Context Analysis]]
- [[_COMMUNITY_Color Space Conversion|Color Space Conversion]]
- [[_COMMUNITY_Metadata Parsing & Editing|Metadata Parsing & Editing]]
- [[_COMMUNITY_Coefficient Tables|Coefficient Tables]]
- [[_COMMUNITY_Coefficient Presence|Coefficient Presence]]
- [[_COMMUNITY_Coefficient Spans|Coefficient Spans]]
- [[_COMMUNITY_Exif Import|Exif Import]]
- [[_COMMUNITY_Container Format Parser|Container Format Parser]]
- [[_COMMUNITY_Image IO|Image I/O]]
- [[_COMMUNITY_Lossless Decorrelation|Lossless Decorrelation]]
- [[_COMMUNITY_RANS Entropy Engine|RANS Entropy Engine]]
- [[_COMMUNITY_Metrics & Utilities|Metrics & Utilities]]
- [[_COMMUNITY_Coefficient Signs|Coefficient Signs]]
- [[_COMMUNITY_Image Quality Metrics|Image Quality Metrics]]
- [[_COMMUNITY_Prediction Modes|Prediction Modes]]
- [[_COMMUNITY_wkenc CLI|wkenc CLI]]
- [[_COMMUNITY_wkmeta-edit CLI|wkmeta-edit CLI]]
- [[_COMMUNITY_wkdec CLI|wkdec CLI]]
- [[_COMMUNITY_C++ API Types|C++ API Types]]
- [[_COMMUNITY_Thread Pool|Thread Pool]]
- [[_COMMUNITY_Metrics Tests|Metrics Tests]]
- [[_COMMUNITY_Wkmeta API|Wkmeta API]]

## God Nodes (most connected - your core abstractions)
1. `encode_lossy_tile()` - 22 edges
2. `TEST()` - 21 edges
3. `encode_lossy_plane_payload()` - 17 edges
4. `encode_lossy_chroma_payload()` - 17 edges
5. `compose_window()` - 13 edges
6. `encode()` - 12 edges
7. `TEST()` - 12 edges
8. `main()` - 12 edges
9. `read_coefficient_table_bank()` - 11 edges
10. `decode()` - 11 edges

## Surprising Connections (you probably didn't know these)
- `TEST()` --calls--> `unpack_coefficient_sign_modes()`  [INFERRED]
  tests/roundtrip_test.cpp → src/coeff_sign_stream.cpp
- `TEST()` --calls--> `read_packed_coefficient_signs()`  [INFERRED]
  tests/roundtrip_test.cpp → src/coeff_sign_stream.cpp
- `TEST()` --calls--> `read_packed_coefficient_spans()`  [INFERRED]
  tests/roundtrip_test.cpp → src/coeff_span_stream.cpp
- `TEST()` --calls--> `read_adaptive_coefficient_spans()`  [INFERRED]
  tests/roundtrip_test.cpp → src/coeff_span_stream.cpp
- `TEST()` --calls--> `read_coefficient_table_bank()`  [INFERRED]
  tests/roundtrip_test.cpp → src/coeff_table_bank_stream.cpp

## Communities (41 total, 3 thin omitted)

### Community 0 - "Viewer Utilities"
Cohesion: 0.07
Nodes (69): add_unique_text(), best_fit_rect(), blit_image(), bool_text(), build_match_keys(), build_playlist(), build_source_matcher(), build_window_title() (+61 more)

### Community 1 - "Lossy Quality & Analysis"
Cohesion: 0.1
Nodes (24): adaptive_chroma_quality_for_tile(), adaptive_luma_quality_for_tile(), analyze_tile_chroma_complexity(), bytes_per_pixel(), collect_quantized_blocks(), compute_quality_score(), encode(), encode_lossy_tile() (+16 more)

### Community 2 - "Coefficient Context Analysis"
Cohesion: 0.16
Nodes (30): analyze_coeff_context(), build_chroma_counts(), build_plane_counts(), coefficient_limit(), count_active_blocks(), decode_elided_single_symbol_stream(), decode_lossy_chroma_payload(), decode_lossy_plane_payload() (+22 more)

### Community 3 - "Color Space Conversion"
Cohesion: 0.1
Nodes (21): get_rgb_to_ycbcr(), get_ycbcr_to_rgb(), rgb_to_ycbcr(), subsample_420(), upsample_420(), ycbcr_to_rgb(), alpha(), dct_1d_forward() (+13 more)

### Community 4 - "Metadata Parsing & Editing"
Cohesion: 0.11
Nodes (21): find_entry(), find_entry_mut(), from_xmp(), get(), get_capture_ts(), get_geo_lat(), get_geo_lon(), get_license() (+13 more)

### Community 5 - "Coefficient Tables"
Cohesion: 0.16
Nodes (23): build_table_bank(), invalid_bank_error(), materialize_bank_tables(), pack_fixed_width_indices(), packed_index_bytes(), read_bank_entries(), read_coefficient_table_bank(), serialize_table() (+15 more)

### Community 6 - "Coefficient Presence"
Cohesion: 0.19
Nodes (16): all_presence_values(), invalid_presence_error(), pack_coefficient_presence(), read_adaptive_coefficient_presence(), unpack_coefficient_presence(), write_adaptive_coefficient_presence(), TEST(), encoded_coefficient_span_stream_bytes() (+8 more)

### Community 7 - "Coefficient Spans"
Cohesion: 0.26
Nodes (15): adaptive_coefficient_span_stream_bytes(), compute_coefficient_span(), encode_adaptive_coefficient_spans(), invalid_span_error(), invalid_span_stream_error(), pack_coefficient_spans(), pack_rle_coefficient_spans(), read_adaptive_coefficient_spans() (+7 more)

### Community 8 - "Exif Import"
Cohesion: 0.19
Nodes (14): extract_exif_blob_from_bytes(), find_entry(), gps_coordinate(), gps_timestamp(), meta(), parse_exif_blob(), rational_to_double(), TiffReader (+6 more)

### Community 9 - "Container Format Parser"
Cohesion: 0.25
Nodes (15): parse_anim(), parse_container(), parse_fhdr(), parse_tile_header(), read_chunk(), serialize_anim(), serialize_fhdr(), serialize_tile_header() (+7 more)

### Community 10 - "Image I/O"
Cohesion: 0.21
Nodes (14): format_supports_transparency(), load_image_file(), load_ppm(), load_wic_image(), lowercase_extension(), next_ppm_token(), read_file_bytes(), save_image_file() (+6 more)

### Community 11 - "Lossless Decorrelation"
Cohesion: 0.18
Nodes (10): apply_palette(), apply_subtract_green(), build_palette(), lossless_decode(), lossless_encode(), lz_decode_indices(), lz_encode_indices(), predict_pixel() (+2 more)

### Community 12 - "RANS Entropy Engine"
Cohesion: 0.12
Nodes (16): ContextRansTables<10>, ContextRansTables<11>, ContextRansTables<12>, ContextRansTables<14>, RansDecoder<10>, RansDecoder<11>, RansDecoder<12>, RansDecoder<14> (+8 more)

### Community 13 - "Metrics & Utilities"
Cohesion: 0.3
Nodes (13): channel_label(), file_size_or_zero(), json_escape(), load_visual_input(), lowercase_extension(), main(), plane_label(), print_image_statistics_json() (+5 more)

### Community 14 - "Coefficient Signs"
Cohesion: 0.31
Nodes (10): invalid_sign_error(), invalid_sign_mode_error(), pack_coefficient_sign_modes(), pack_coefficient_signs(), read_packed_coefficient_signs(), unpack_coefficient_sign_modes(), unpack_coefficient_signs(), wk() (+2 more)

### Community 15 - "Image Quality Metrics"
Cohesion: 0.36
Nodes (12): build_ycbcr_planes(), channel_count(), compare_images(), compute_artifact_metrics(), compute_channel_metrics(), compute_channel_ssim(), compute_image_statistics(), compute_plane_metrics() (+4 more)

### Community 16 - "Prediction Modes"
Cohesion: 0.38
Nodes (7): invalid_mode_error(), pack_prediction_modes(), read_packed_prediction_modes(), unpack_prediction_modes(), wk(), write_packed_prediction_modes(), TEST()

### Community 17 - "wkenc CLI"
Cohesion: 0.42
Nodes (8): apply_exif_metadata(), default_output_path(), is_power_of_two(), main(), merge_metadata(), parse_tile_size(), print_usage(), read_file_bytes()

### Community 18 - "wkmeta-edit CLI"
Cohesion: 0.42
Nodes (8): main(), merge_metadata(), parse_key(), parse_namespace(), parse_tag(), parse_value_str(), print_usage(), read_file_bytes()

### Community 19 - "wkdec CLI"
Cohesion: 0.52
Nodes (6): default_output_path(), main(), print_info(), print_usage(), read_file_bytes(), write_text_file()

### Community 22 - "Metrics Tests"
Cohesion: 0.83
Nodes (3): make_color_edge_image(), make_rgb_image(), TEST()

## Knowledge Gaps
- **18 isolated node(s):** `MetaBlock`, `RansTable<10>`, `RansTable<11>`, `RansTable<12>`, `RansTable<14>` (+13 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **3 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `TEST()` connect `Coefficient Presence` to `Lossy Quality & Analysis`, `Color Space Conversion`, `Coefficient Tables`, `Coefficient Spans`, `Container Format Parser`, `Coefficient Signs`, `Prediction Modes`?**
  _High betweenness centrality (0.090) - this node is a cross-community bridge._
- **Why does `encode()` connect `Lossy Quality & Analysis` to `Color Space Conversion`, `Container Format Parser`, `Lossless Decorrelation`, `Coefficient Presence`?**
  _High betweenness centrality (0.082) - this node is a cross-community bridge._
- **Why does `encode_lossy_tile()` connect `Lossy Quality & Analysis` to `Prediction Modes`, `Coefficient Context Analysis`, `Color Space Conversion`, `Coefficient Spans`?**
  _High betweenness centrality (0.076) - this node is a cross-community bridge._
- **Are the 15 inferred relationships involving `encode_lossy_tile()` (e.g. with `quality_to_qp()` and `qp_to_lambda()`) actually correct?**
  _`encode_lossy_tile()` has 15 INFERRED edges - model-reasoned connections that need verification._
- **Are the 14 inferred relationships involving `TEST()` (e.g. with `encode()` and `decode()`) actually correct?**
  _`TEST()` has 14 INFERRED edges - model-reasoned connections that need verification._
- **Are the 4 inferred relationships involving `encode_lossy_plane_payload()` (e.g. with `encode_lossy_tile()` and `write_coefficient_table_bank()`) actually correct?**
  _`encode_lossy_plane_payload()` has 4 INFERRED edges - model-reasoned connections that need verification._
- **Are the 4 inferred relationships involving `encode_lossy_chroma_payload()` (e.g. with `encode_lossy_tile()` and `write_coefficient_table_bank()`) actually correct?**
  _`encode_lossy_chroma_payload()` has 4 INFERRED edges - model-reasoned connections that need verification._