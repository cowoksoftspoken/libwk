
#include <wk/wkmeta.hpp>
#include "../src/common.h"
#include "../src/container.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: wkmeta-dump <input.wk>\n");
        return 1;
    }

    const char* input_path = argv[1];

    std::ifstream in(input_path, std::ios::binary | std::ios::ate);
    if (!in) {
        std::fprintf(stderr, "Error: cannot open %s\n", input_path);
        return 1;
    }

    size_t file_size = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<uint8_t> data(file_size);
    in.read(reinterpret_cast<char*>(data.data()), file_size);
    in.close();

    auto file = wk::parse_container(data);
    if (!file) {
        std::fprintf(stderr, "Error: %s\n", file.error().message.c_str());
        return 1;
    }

    if (!file->metadata) {
        std::fprintf(stderr, "No WKMETA chunk found in %s\n", input_path);
        return 0;
    }

    std::printf("%s", file->metadata->to_json().c_str());

    return 0;
}
