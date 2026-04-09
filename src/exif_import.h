#pragma once


#include "common.h"
#include <wk/wkmeta.hpp>

namespace wk::meta {

std::expected<MetaBlock, MetaError> parse_exif_blob(std::span<const uint8_t> exif_blob);
std::expected<std::vector<uint8_t>, MetaError> extract_exif_blob_from_bytes(std::span<const uint8_t> file_bytes);

}
