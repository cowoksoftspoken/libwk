#pragma once


#include "common.h"
#include <wk/wk.hpp>
#include <string_view>

namespace wk::io {

Result<Image> load_image_file(std::string_view path);
Result<void> save_image_file(std::string_view path, const Image& image);

}
