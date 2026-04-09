
#include <wk/wk.hpp>
#include <wk/wkmeta.hpp>
#include "../src/common.h"
#include "../src/container.h"
#include "../src/exif_import.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

wk::meta::Namespace parse_namespace(const std::string& ns_str) {
    if (ns_str == "CAPTURE")  return wk::meta::Namespace::Capture;
    if (ns_str == "GEO")      return wk::meta::Namespace::Geo;
    if (ns_str == "TIME")     return wk::meta::Namespace::Time;
    if (ns_str == "RIGHTS")   return wk::meta::Namespace::Rights;
    if (ns_str == "CONTENT")  return wk::meta::Namespace::Content;
    if (ns_str == "ANIM")     return wk::meta::Namespace::Anim;
    if (ns_str == "REGION")   return wk::meta::Namespace::Region;
    if (ns_str == "DEVICE")   return wk::meta::Namespace::Device;
    if (ns_str == "RATING")   return wk::meta::Namespace::Rating;
    if (ns_str == "CUSTOM")   return wk::meta::Namespace::Custom;
    if (ns_str == "PROV_REF") return wk::meta::Namespace::ProvRef;
    return wk::meta::Namespace::Custom;
}

uint16_t parse_tag(const std::string& ns_str, const std::string& tag_str) {
    using namespace wk::meta;

    if (ns_str == "GEO") {
        if (tag_str == "LAT")        return geo::LAT;
        if (tag_str == "LON")        return geo::LON;
        if (tag_str == "ALT")        return geo::ALT;
        if (tag_str == "COUNTRY")    return geo::COUNTRY;
        if (tag_str == "CITY")       return geo::CITY;
        if (tag_str == "PLACE_NAME") return geo::PLACE_NAME;
    }
    if (ns_str == "CONTENT") {
        if (tag_str == "TITLE")       return content::TITLE;
        if (tag_str == "DESCRIPTION") return content::DESCRIPTION;
        if (tag_str == "ALT_TEXT")    return content::ALT_TEXT;
        if (tag_str == "KEYWORDS")    return content::KEYWORDS;
    }
    if (ns_str == "RIGHTS") {
        if (tag_str == "CREATOR")      return rights::CREATOR;
        if (tag_str == "COPYRIGHT")    return rights::COPYRIGHT;
        if (tag_str == "LICENSE")      return rights::LICENSE_SPDX;
        if (tag_str == "LICENSE_SPDX") return rights::LICENSE_SPDX;
        if (tag_str == "LICENSE_URL")  return rights::LICENSE_URL;
    }
    if (ns_str == "RATING") {
        if (tag_str == "STARS")         return rating::STARS;
        if (tag_str == "AI_GENERATED")  return rating::AI_GENERATED;
        if (tag_str == "QUALITY_SCORE") return rating::QUALITY_SCORE;
    }
    if (ns_str == "CAPTURE") {
        if (tag_str == "MAKE")     return capture::MAKE;
        if (tag_str == "MODEL")    return capture::MODEL;
        if (tag_str == "ISO")      return capture::ISO;
        if (tag_str == "SOFTWARE") return capture::SOFTWARE;
    }

    if (tag_str.starts_with("0x")) {
        return static_cast<uint16_t>(std::strtoul(tag_str.c_str() + 2, nullptr, 16));
    }
    return static_cast<uint16_t>(std::atoi(tag_str.c_str()));
}

bool parse_key(const std::string& key, std::string& ns_out, std::string& tag_out) {
    const auto dot = key.find('.');
    if (dot == std::string::npos) {
        return false;
    }
    ns_out = key.substr(0, dot);
    tag_out = key.substr(dot + 1);
    return true;
}

wk::meta::Value parse_value_str(const std::string& ns_str,
                                const std::string& tag_str,
                                const std::string& value_str) {
    using namespace wk::meta;

    if (ns_str == "GEO" && (tag_str == "LAT" || tag_str == "LON")) {
        return static_cast<double>(std::atof(value_str.c_str()));
    }
    if (ns_str == "GEO" && tag_str == "ALT") {
        return static_cast<float>(std::atof(value_str.c_str()));
    }
    if (ns_str == "RATING" && (tag_str == "STARS" || tag_str == "AI_GENERATED")) {
        return static_cast<uint8_t>(std::atoi(value_str.c_str()));
    }
    if (ns_str == "RATING" && tag_str == "QUALITY_SCORE") {
        return static_cast<float>(std::atof(value_str.c_str()));
    }

    const auto colon = value_str.find(':');
    if (colon != std::string::npos && colon < 5 &&
        (ns_str == "CONTENT" || ns_str == "RIGHTS" || ns_str == "ANIM" || ns_str == "REGION")) {
        return wk::meta::LocalizedString{value_str.substr(0, colon), value_str.substr(colon + 1)};
    }

    return value_str;
}

wk::Result<std::vector<uint8_t>> read_file_bytes(std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected(wk::Error{wk::ErrorCode::IoError, "failed to open file"});
    }

    const std::streamsize size = file.tellg();
    if (size < 0) {
        return std::unexpected(wk::Error{wk::ErrorCode::IoError, "failed to determine file size"});
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return std::unexpected(wk::Error{wk::ErrorCode::IoError, "failed to read file"});
    }
    return bytes;
}

void merge_metadata(wk::meta::MetaBlock& destination, const wk::meta::MetaBlock& source) {
    for (const auto& entry : source.entries) {
        if (entry.is_opaque) {
            destination.remove(entry.ns, entry.tag);
            destination.entries.push_back(entry);
            continue;
        }

        auto set_result = destination.set(entry.ns, entry.tag, entry.value);
        if (!set_result) {
            destination.remove(entry.ns, entry.tag);
            destination.entries.push_back(entry);
        }
    }
}

void print_usage() {
    std::cerr
        << "Usage: wkmeta-edit [options] <input.wk>\n\n"
        << "Options:\n"
        << "  --set NS.TAG VALUE   Set metadata field (example: --set GEO.LAT 1.234)\n"
        << "  --delete NS.TAG      Delete metadata field\n"
        << "  --import-exif FILE   Import EXIF from JPEG/TIFF donor file\n"
        << "  --export-json        Print WKMETA as JSON to stdout\n"
        << "  --version            Print version\n"
        << "  -o, --output FILE    Output file (default: in-place edit)\n"
        << "  -h, --help           Show this help\n";
}

}

int main(int argc, char* argv[]) {
    struct SetOp {
        std::string key;
        std::string value;
    };

    std::vector<SetOp> set_ops;
    std::vector<std::string> delete_ops;
    std::string input_path;
    std::string output_path;
    std::string exif_source_path;
    bool export_json = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--set") {
            if (i + 2 >= argc) {
                std::cerr << "Error: --set requires NS.TAG and VALUE\n";
                return 1;
            }
            set_ops.push_back({argv[++i], argv[++i]});
        } else if (arg == "--delete") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --delete requires NS.TAG\n";
                return 1;
            }
            delete_ops.push_back(argv[++i]);
        } else if (arg == "--import-exif") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --import-exif requires a donor file\n";
                return 1;
            }
            exif_source_path = argv[++i];
        } else if (arg == "--export-json") {
            export_json = true;
        } else if (arg == "--version") {
            std::cout << wk::version() << '\n';
            return 0;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --output requires a file path\n";
                return 1;
            }
            output_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Error: unknown option " << arg << '\n';
            print_usage();
            return 1;
        } else if (input_path.empty()) {
            input_path = arg;
        } else {
            std::cerr << "Error: unexpected extra argument " << arg << '\n';
            return 1;
        }
    }

    if (input_path.empty()) {
        print_usage();
        return 1;
    }

    auto input_bytes = read_file_bytes(input_path);
    if (!input_bytes) {
        std::cerr << "Error: " << input_bytes.error().message << '\n';
        return 1;
    }

    auto file = wk::parse_container(*input_bytes);
    if (!file) {
        std::cerr << "Error: " << file.error().message << '\n';
        return 1;
    }

    if (!file->metadata) {
        file->metadata = wk::meta::MetaBlock{};
    }

    for (const auto& operation : set_ops) {
        std::string ns_str;
        std::string tag_str;
        if (!parse_key(operation.key, ns_str, tag_str)) {
            std::cerr << "Error: invalid metadata key " << operation.key << " (expected NS.TAG)\n";
            return 1;
        }

        auto set_result = file->metadata->set(parse_namespace(ns_str),
                                              parse_tag(ns_str, tag_str),
                                              parse_value_str(ns_str, tag_str, operation.value));
        if (!set_result) {
            std::cerr << "Error: failed to set " << operation.key << ": " << set_result.error().message << '\n';
            return 1;
        }
    }

    for (const auto& key : delete_ops) {
        std::string ns_str;
        std::string tag_str;
        if (!parse_key(key, ns_str, tag_str)) {
            std::cerr << "Error: invalid metadata key " << key << " (expected NS.TAG)\n";
            return 1;
        }
        file->metadata->remove(parse_namespace(ns_str), parse_tag(ns_str, tag_str));
    }

    if (!exif_source_path.empty()) {
        auto donor_bytes = read_file_bytes(exif_source_path);
        if (!donor_bytes) {
            std::cerr << "Error: " << donor_bytes.error().message << '\n';
            return 1;
        }

        auto exif_blob = wk::meta::extract_exif_blob_from_bytes(*donor_bytes);
        if (!exif_blob) {
            std::cerr << "Error: EXIF import failed: " << exif_blob.error().message << '\n';
            return 1;
        }

        auto imported = wk::meta::MetaBlock::from_exif(*exif_blob);
        if (!imported) {
            std::cerr << "Error: EXIF parse failed: " << imported.error().message << '\n';
            return 1;
        }

        merge_metadata(*file->metadata, *imported);
    }

    if (export_json) {
        std::cout << file->metadata->to_json();
        if (set_ops.empty() && delete_ops.empty() && exif_source_path.empty()) {
            return 0;
        }
    }

    const bool metadata_empty = !file->metadata || file->metadata->entries.empty();
    if (metadata_empty) {
        file->metadata.reset();
        file->header.flags = static_cast<uint16_t>(file->header.flags & static_cast<uint16_t>(~wk::FHDR_FLAG_HAS_WKMETA));
    } else {
        file->header.flags |= wk::FHDR_FLAG_HAS_WKMETA;
    }

    auto output_bytes = wk::write_container(*file);
    if (!output_bytes) {
        std::cerr << "Error: " << output_bytes.error().message << '\n';
        return 1;
    }

    if (output_path.empty()) {
        output_path = input_path;
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        std::cerr << "Error: failed to open output file\n";
        return 1;
    }
    output.write(reinterpret_cast<const char*>(output_bytes->data()), static_cast<std::streamsize>(output_bytes->size()));
    if (!output) {
        std::cerr << "Error: failed to write output file\n";
        return 1;
    }

    std::cerr << "Written " << output_path << " (" << output_bytes->size() << " bytes)\n";
    return 0;
}
