#pragma once


#include "common.h"
#include <wk/wkmeta.hpp>

namespace wk {



struct Chunk {
    char                  type[4]  = {};
    uint8_t               flags    = 0;
    std::vector<uint8_t>  payload;

    [[nodiscard]] bool is_type(const char t[4]) const {
        return std::memcmp(type, t, 4) == 0;
    }
};



struct WkFile {
    FrameHeader                    header;
    std::optional<meta::MetaBlock> metadata;
    std::vector<uint8_t>           icc_profile;
    std::vector<uint8_t>           c2pa_manifest;
    std::optional<AnimHeader>      animation;
    std::vector<Chunk>             tile_chunks;
};



[[nodiscard]] Result<WkFile> parse_container(std::span<const uint8_t> data);



[[nodiscard]] Result<std::vector<uint8_t>> write_container(const WkFile& file);



[[nodiscard]] std::vector<uint8_t> serialize_fhdr(const FrameHeader& hdr);
[[nodiscard]] Result<FrameHeader> parse_fhdr(std::span<const uint8_t> data);

[[nodiscard]] std::vector<uint8_t> serialize_anim(const AnimHeader& anim);
[[nodiscard]] Result<AnimHeader> parse_anim(std::span<const uint8_t> data);

[[nodiscard]] std::vector<uint8_t> serialize_tile_header(const TileHeader& tile);
[[nodiscard]] Result<TileHeader> parse_tile_header(std::span<const uint8_t> data);

}
