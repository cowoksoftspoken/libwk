
#include <wk/wkmeta.hpp>
#include "common.h"
#include "exif_import.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>
#include <charconv>

namespace wk::meta
{

    std::string Timestamp::to_iso8601() const
    {
        time_t secs = static_cast<time_t>(microseconds / 1000000ULL);
        uint32_t usec = static_cast<uint32_t>(microseconds % 1000000ULL);

        struct tm utc_tm;
#ifdef _WIN32
        gmtime_s(&utc_tm, &secs);
#else
        gmtime_r(&secs, &utc_tm);
#endif

        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &utc_tm);

        if (usec > 0)
        {
            char usec_buf[16];
            std::snprintf(usec_buf, sizeof(usec_buf), ".%06u", usec);
            std::strncat(buf, usec_buf, sizeof(buf) - std::strlen(buf) - 1);
        }
        std::strncat(buf, "Z", sizeof(buf) - std::strlen(buf) - 1);
        return buf;
    }

    Timestamp Timestamp::from_iso8601(std::string_view s)
    {
        Timestamp ts;

        if (s.size() < 19)
            return ts;

        struct tm tm_val = {};
        int year, month, day, hour, minute, second;
        if (std::sscanf(s.data(), "%d-%d-%dT%d:%d:%d",
                        &year, &month, &day, &hour, &minute, &second) < 6)
        {
            return ts;
        }

        tm_val.tm_year = year - 1900;
        tm_val.tm_mon = month - 1;
        tm_val.tm_mday = day;
        tm_val.tm_hour = hour;
        tm_val.tm_min = minute;
        tm_val.tm_sec = second;

#ifdef _WIN32
        time_t t = _mkgmtime(&tm_val);
#else
        time_t t = timegm(&tm_val);
#endif

        ts.microseconds = static_cast<uint64_t>(t) * 1000000ULL;

        auto dot_pos = s.find('.');
        if (dot_pos != std::string_view::npos)
        {
            auto end_pos = s.find('Z', dot_pos);
            if (end_pos == std::string_view::npos)
                end_pos = s.size();
            std::string frac_str(s.substr(dot_pos + 1, end_pos - dot_pos - 1));
            while (frac_str.size() < 6)
                frac_str += '0';
            frac_str = frac_str.substr(0, 6);
            uint32_t usec = 0;
            std::from_chars(frac_str.data(), frac_str.data() + frac_str.size(), usec);
            ts.microseconds += usec;
        }

        return ts;
    }

    std::string Uuid::to_string() const
    {
        char buf[37];
        std::snprintf(buf, sizeof(buf),
                      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                      bytes[0], bytes[1], bytes[2], bytes[3],
                      bytes[4], bytes[5], bytes[6], bytes[7],
                      bytes[8], bytes[9], bytes[10], bytes[11],
                      bytes[12], bytes[13], bytes[14], bytes[15]);
        return buf;
    }

    Uuid Uuid::from_string(std::string_view s)
    {
        Uuid uuid;
        if (s.size() < 36)
            return uuid;

        int idx = 0;
        for (size_t i = 0; i < s.size() && idx < 16; i++)
        {
            if (s[i] == '-')
                continue;
            if (i + 1 >= s.size())
                break;

            char hex[3] = {s[i], s[i + 1], '\0'};
            uuid.bytes[idx++] = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
            i++;
        }
        return uuid;
    }

    Uuid Uuid::generate()
    {

        Uuid uuid;
        static uint64_t counter = 0;
        uint64_t seed = static_cast<uint64_t>(std::time(nullptr)) ^ (++counter);

        for (int i = 0; i < 16; i++)
        {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            uuid.bytes[i] = static_cast<uint8_t>(seed >> 32);
        }

        uuid.bytes[6] = (uuid.bytes[6] & 0x0F) | 0x40;

        uuid.bytes[8] = (uuid.bytes[8] & 0x3F) | 0x80;
        return uuid;
    }

    bool Struct::operator==(const Struct &other) const
    {
        return fields == other.fields;
    }

    static TypeId type_id_from_value(const Value &v)
    {
        return std::visit([](const auto &val) -> TypeId
                          {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, uint8_t>) return TypeId::UINT8;
        else if constexpr (std::is_same_v<T, uint16_t>) return TypeId::UINT16;
        else if constexpr (std::is_same_v<T, uint32_t>) return TypeId::UINT32;
        else if constexpr (std::is_same_v<T, uint64_t>) return TypeId::UINT64;
        else if constexpr (std::is_same_v<T, int32_t>) return TypeId::INT32;
        else if constexpr (std::is_same_v<T, float>) return TypeId::FLOAT32;
        else if constexpr (std::is_same_v<T, double>) return TypeId::FLOAT64;
        else if constexpr (std::is_same_v<T, Rational>) return TypeId::RATIONAL;
        else if constexpr (std::is_same_v<T, LocalizedString>) return TypeId::LSTR;
        else if constexpr (std::is_same_v<T, std::string>) return TypeId::STR;
        else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) return TypeId::BYTES;
        else if constexpr (std::is_same_v<T, std::shared_ptr<ValueArray>>) return TypeId::ARRAY;
        else if constexpr (std::is_same_v<T, Struct>) return TypeId::STRUCT;
        else if constexpr (std::is_same_v<T, Timestamp>) return TypeId::TS64;
        else if constexpr (std::is_same_v<T, Uuid>) return TypeId::UUID;
        else return TypeId::BYTES; }, v);
    }

    static void serialize_value(wk::ByteWriter &w, const Value &v);

    static void serialize_value(wk::ByteWriter &w, const Value &v)
    {
        std::visit([&w](const auto &val)
                   {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, uint8_t>) {
            w.write_u8(val);
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            w.write_u16(val);
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            w.write_u32(val);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            w.write_u64(val);
        } else if constexpr (std::is_same_v<T, int32_t>) {
            w.write_i32(val);
        } else if constexpr (std::is_same_v<T, float>) {
            w.write_f32(val);
        } else if constexpr (std::is_same_v<T, double>) {
            w.write_f64(val);
        } else if constexpr (std::is_same_v<T, Rational>) {
            w.write_i32(val.numerator);
            w.write_i32(val.denominator);
        } else if constexpr (std::is_same_v<T, LocalizedString>) {

            w.write_u8(static_cast<uint8_t>(val.lang.size()));
            w.write_str(val.lang);
            w.write_str(val.text);
        } else if constexpr (std::is_same_v<T, std::string>) {
            w.write_str(val);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            w.write_bytes(val);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueArray>>) {
            if (val) {
                w.write_u16(static_cast<uint16_t>(val->elements.size()));
                if (!val->elements.empty()) {
                    w.write_u8(static_cast<uint8_t>(type_id_from_value(val->elements[0])));
                    for (const auto& elem : val->elements) {
                        serialize_value(w, elem);
                    }
                } else {
                    w.write_u8(static_cast<uint8_t>(TypeId::UINT8));
                }
            } else {
                w.write_u16(0);
                w.write_u8(static_cast<uint8_t>(TypeId::UINT8));
            }
        } else if constexpr (std::is_same_v<T, Struct>) {
            w.write_u16(static_cast<uint16_t>(val.fields.size()));
            for (const auto& field : val.fields) {
                w.write_u8(static_cast<uint8_t>(field.ns));
                w.write_u16(field.tag);
                TypeId tid = type_id_from_value(field.value);
                w.write_u8(static_cast<uint8_t>(tid));


                wk::ByteWriter val_w;
                serialize_value(val_w, field.value);
                auto val_data = val_w.finish();
                w.write_u32(static_cast<uint32_t>(val_data.size()));
                w.write_bytes(val_data);
            }
        } else if constexpr (std::is_same_v<T, Timestamp>) {
            w.write_u64(val.microseconds);
        } else if constexpr (std::is_same_v<T, Uuid>) {
            w.write_bytes({val.bytes, 16});
        } }, v);
    }

    static std::expected<Value, MetaError> parse_value(
        wk::ByteReader &r, TypeId type, uint32_t value_size);

    static std::expected<Value, MetaError> parse_value(
        wk::ByteReader &r, TypeId type, uint32_t value_size)
    {

        switch (type)
        {
        case TypeId::UINT8:
        {
            auto v = r.read_u8();
            if (!v)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{*v};
        }
        case TypeId::UINT16:
        {
            auto v = r.read_u16();
            if (!v)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{*v};
        }
        case TypeId::UINT32:
        {
            auto v = r.read_u32();
            if (!v)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{*v};
        }
        case TypeId::UINT64:
        {
            auto v = r.read_u64();
            if (!v)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{*v};
        }
        case TypeId::INT32:
        {
            auto v = r.read_i32();
            if (!v)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{*v};
        }
        case TypeId::FLOAT32:
        {
            auto v = r.read_f32();
            if (!v)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{*v};
        }
        case TypeId::FLOAT64:
        {
            auto v = r.read_f64();
            if (!v)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{*v};
        }
        case TypeId::RATIONAL:
        {
            auto n = r.read_i32();
            auto d = r.read_i32();
            if (!n || !d)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{Rational{*n, *d}};
        }
        case TypeId::LSTR:
        {
            auto lang_len = r.read_u8();
            if (!lang_len)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            auto lang = r.read_str(*lang_len);
            if (!lang)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            uint32_t text_len = value_size - 1 - *lang_len;
            auto text = r.read_str(text_len);
            if (!text)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{LocalizedString{std::move(*lang), std::move(*text)}};
        }
        case TypeId::STR:
        {
            auto s = r.read_str(value_size);
            if (!s)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{std::move(*s)};
        }
        case TypeId::BYTES:
        {
            auto b = r.read_bytes(value_size);
            if (!b)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{std::vector<uint8_t>(b->begin(), b->end())};
        }
        case TypeId::ARRAY:
        {
            auto count = r.read_u16();
            if (!count)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            auto elem_type = r.read_u8();
            if (!elem_type)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});

            auto arr = std::make_shared<ValueArray>();
            arr->elements.reserve(*count);

            TypeId et = static_cast<TypeId>(*elem_type);
            uint32_t remaining = value_size - 3;
            uint32_t per_elem = (*count > 0) ? remaining / *count : 0;

            for (uint16_t i = 0; i < *count; i++)
            {
                auto elem = parse_value(r, et, per_elem);
                if (!elem)
                    return std::unexpected(elem.error());
                arr->elements.push_back(std::move(*elem));
            }
            return Value{std::move(arr)};
        }
        case TypeId::STRUCT:
        {
            auto field_count = r.read_u16();
            if (!field_count)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});

            Struct s;
            for (uint16_t i = 0; i < *field_count; i++)
            {
                auto ns = r.read_u8();
                auto tag = r.read_u16();
                auto tid = r.read_u8();
                auto vsize = r.read_u32();
                if (!ns || !tag || !tid || !vsize)
                {
                    return std::unexpected(MetaError{MetaErrorCode::ParseError});
                }

                auto val = parse_value(r, static_cast<TypeId>(*tid), *vsize);
                if (!val)
                {
                    return std::unexpected(val.error());
                }

                Entry entry;
                entry.ns = static_cast<Namespace>(*ns);
                entry.tag = *tag;
                entry.value = std::move(*val);
                s.fields.push_back(std::move(entry));
            }
            return Value{std::move(s)};
        }
        case TypeId::TS64:
        {
            auto v = r.read_u64();
            if (!v)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            return Value{Timestamp{*v}};
        }
        case TypeId::UUID:
        {
            auto b = r.read_bytes(16);
            if (!b)
                return std::unexpected(MetaError{MetaErrorCode::ParseError});
            Uuid uuid;
            std::memcpy(uuid.bytes, b->data(), 16);
            return Value{uuid};
        }
        default:
            return std::unexpected(MetaError{MetaErrorCode::UnknownType,
                                             "unknown type id " + std::to_string(static_cast<int>(type))});
        }
    }

    const Entry *MetaBlock::find_entry(Namespace ns, uint16_t tag) const
    {
        for (const auto &e : entries)
        {
            if (e.ns == ns && e.tag == tag)
                return &e;
        }
        return nullptr;
    }

    Entry *MetaBlock::find_entry_mut(Namespace ns, uint16_t tag)
    {
        for (auto &e : entries)
        {
            if (e.ns == ns && e.tag == tag)
                return &e;
        }
        return nullptr;
    }

    auto MetaBlock::get(Namespace ns, uint16_t tag) const
        -> std::expected<const Value *, MetaError>
    {
        auto *e = find_entry(ns, tag);
        if (!e)
            return std::unexpected(MetaError{MetaErrorCode::TagNotFound});
        return &e->value;
    }

    auto MetaBlock::set(Namespace ns, uint16_t tag, Value v)
        -> std::expected<void, MetaError>
    {
        auto *e = find_entry_mut(ns, tag);
        if (e)
        {
            e->value = std::move(v);
        }
        else
        {
            entries.push_back({ns, tag, std::move(v), {}, false});
        }
        return {};
    }

    auto MetaBlock::remove(Namespace ns, uint16_t tag) -> bool
    {
        auto it = std::remove_if(entries.begin(), entries.end(),
                                 [ns, tag](const Entry &e)
                                 { return e.ns == ns && e.tag == tag; });
        if (it != entries.end())
        {
            entries.erase(it, entries.end());
            return true;
        }
        return false;
    }

    auto MetaBlock::get_geo_lat() const -> std::expected<double, MetaError>
    {
        auto r = get(Namespace::Geo, geo::LAT);
        if (!r)
            return std::unexpected(r.error());
        if (auto *v = std::get_if<double>(*r))
            return *v;
        return std::unexpected(MetaError{MetaErrorCode::TypeMismatch, "expected FLOAT64"});
    }

    auto MetaBlock::get_geo_lon() const -> std::expected<double, MetaError>
    {
        auto r = get(Namespace::Geo, geo::LON);
        if (!r)
            return std::unexpected(r.error());
        if (auto *v = std::get_if<double>(*r))
            return *v;
        return std::unexpected(MetaError{MetaErrorCode::TypeMismatch, "expected FLOAT64"});
    }

    auto MetaBlock::get_capture_ts() const -> std::expected<Timestamp, MetaError>
    {
        auto r = get(Namespace::Time, time_tags::CAPTURE_UTC);
        if (!r)
        {
            r = get(Namespace::Geo, geo::CAPTURE_TS);
        }
        if (!r)
            return std::unexpected(r.error());
        if (auto *v = std::get_if<Timestamp>(*r))
            return *v;
        return std::unexpected(MetaError{MetaErrorCode::TypeMismatch, "expected TS64"});
    }

    auto MetaBlock::get_title(std::string_view lang_bcp47) const
        -> std::expected<std::string, MetaError>
    {
        auto r = get(Namespace::Content, content::TITLE);
        if (!r)
            return std::unexpected(r.error());

        if (auto *v = std::get_if<LocalizedString>(*r))
        {
            if (lang_bcp47.empty() || v->lang == lang_bcp47)
            {
                return v->text;
            }
        }
        if (auto *v = std::get_if<std::string>(*r))
        {
            return *v;
        }
        return std::unexpected(MetaError{MetaErrorCode::TagNotFound, "title not found for lang"});
    }

    auto MetaBlock::get_regions() const -> std::vector<RegionAnnotation>
    {
        std::vector<RegionAnnotation> regions;

        for (const auto &e : entries)
        {
            if (e.ns == Namespace::Region)
            {
                if (auto *s = std::get_if<Struct>(&e.value))
                {
                    RegionAnnotation ann;
                    for (const auto &f : s->fields)
                    {
                        if (f.tag == region::NAME)
                        {
                            if (auto *ls = std::get_if<LocalizedString>(&f.value))
                                ann.name = ls->text;
                        }
                        else if (f.tag == region::TYPE)
                        {
                            if (auto *v = std::get_if<uint8_t>(&f.value))
                                ann.type = *v;
                        }
                        else if (f.tag == region::CONFIDENCE)
                        {
                            if (auto *v = std::get_if<float>(&f.value))
                                ann.confidence = *v;
                        }
                        else if (f.tag == region::PERSON_NAME)
                        {
                            if (auto *ls = std::get_if<LocalizedString>(&f.value))
                                ann.person_name = ls->text;
                        }
                        else if (f.tag == region::OBJECT_CLASS)
                        {
                            if (auto *v = std::get_if<std::string>(&f.value))
                                ann.object_class = *v;
                        }
                    }
                    regions.push_back(std::move(ann));
                }
            }
        }

        return regions;
    }

    auto MetaBlock::get_license() const -> std::expected<std::string, MetaError>
    {
        auto r = get(Namespace::Rights, rights::LICENSE_SPDX);
        if (!r)
            return std::unexpected(r.error());
        if (auto *v = std::get_if<std::string>(*r))
            return *v;
        return std::unexpected(MetaError{MetaErrorCode::TypeMismatch});
    }

    auto MetaBlock::serialize() const -> std::vector<uint8_t>
    {
        wk::ByteWriter w;

        w.write_u8(0x01);
        w.write_u16(static_cast<uint16_t>(entries.size()));

        for (const auto &entry : entries)
        {
            if (entry.is_opaque)
            {

                w.write_bytes(entry.opaque_data);
                continue;
            }

            w.write_u8(static_cast<uint8_t>(entry.ns));
            w.write_u16(entry.tag);

            TypeId tid = type_id_from_value(entry.value);
            w.write_u8(static_cast<uint8_t>(tid));

            wk::ByteWriter val_w;
            serialize_value(val_w, entry.value);
            auto val_data = val_w.finish();

            w.write_u32(static_cast<uint32_t>(val_data.size()));
            w.write_bytes(val_data);
        }

        return w.finish();
    }

    auto MetaBlock::parse(std::span<const uint8_t> data)
        -> std::expected<MetaBlock, MetaError>
    {

        wk::ByteReader r(data);
        MetaBlock block;

        auto ver = r.read_u8();
        if (!ver)
            return std::unexpected(MetaError{MetaErrorCode::ParseError, "truncated"});
        if (*ver != 0x01)
        {
            return std::unexpected(MetaError{MetaErrorCode::ParseError,
                                             "unsupported wkmeta version"});
        }

        auto count = r.read_u16();
        if (!count)
            return std::unexpected(MetaError{MetaErrorCode::ParseError});

        block.entries.reserve(*count);

        for (uint16_t i = 0; i < *count; i++)
        {
            size_t entry_start = r.position();

            auto ns_r = r.read_u8();
            auto tag_r = r.read_u16();
            auto tid_r = r.read_u8();
            auto vsize_r = r.read_u32();

            if (!ns_r || !tag_r || !tid_r || !vsize_r)
            {
                return std::unexpected(MetaError{MetaErrorCode::ParseError, "truncated entry"});
            }

            TypeId tid = static_cast<TypeId>(*tid_r);

            auto val = parse_value(r, tid, *vsize_r);

            Entry entry;
            entry.ns = static_cast<Namespace>(*ns_r);
            entry.tag = *tag_r;

            if (val)
            {
                entry.value = std::move(*val);
            }
            else
            {

                entry.is_opaque = true;
                size_t entry_size = 1 + 2 + 1 + 4 + *vsize_r;
                entry.opaque_data.resize(entry_size);
                size_t copy_offset = entry_start;
                if (copy_offset + entry_size <= data.size())
                {
                    std::memcpy(entry.opaque_data.data(),
                                data.data() + copy_offset, entry_size);
                }
                entry.value = std::vector<uint8_t>{};
            }

            block.entries.push_back(std::move(entry));
        }

        return block;
    }

    static std::string json_escape(std::string_view s)
    {
        std::string result;
        result.reserve(s.size() + 8);
        for (char c : s)
        {
            switch (c)
            {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
                break;
            }
        }
        return result;
    }

    static std::string namespace_name(Namespace ns)
    {
        switch (ns)
        {
        case Namespace::Capture:
            return "capture";
        case Namespace::Geo:
            return "geo";
        case Namespace::Time:
            return "time";
        case Namespace::Rights:
            return "rights";
        case Namespace::Content:
            return "content";
        case Namespace::Anim:
            return "anim";
        case Namespace::Region:
            return "region";
        case Namespace::Device:
            return "device";
        case Namespace::Rating:
            return "rating";
        case Namespace::Custom:
            return "custom";
        case Namespace::ProvRef:
            return "provRef";
        default:
            return "unknown_" + std::to_string(static_cast<int>(ns));
        }
    }

    static std::string value_to_json(const Value &v, int indent = 2);

    static std::string value_to_json(const Value &v, int indent)
    {
        return std::visit([indent](const auto &val) -> std::string
                          {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, uint8_t>) {
            return std::to_string(val);
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            return std::to_string(val);
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            return std::to_string(val);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return std::to_string(val);
        } else if constexpr (std::is_same_v<T, int32_t>) {
            return std::to_string(val);
        } else if constexpr (std::is_same_v<T, float>) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(val));
            return buf;
        } else if constexpr (std::is_same_v<T, double>) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.10g", val);
            return buf;
        } else if constexpr (std::is_same_v<T, Rational>) {
            return "{\"numerator\":" + std::to_string(val.numerator)
                 + ",\"denominator\":" + std::to_string(val.denominator) + "}";
        } else if constexpr (std::is_same_v<T, LocalizedString>) {
            return "{\"" + json_escape(val.lang.empty() ? "default" : val.lang)
                 + "\":\"" + json_escape(val.text) + "\"}";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return "\"" + json_escape(val) + "\"";
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            std::string s = "\"<";
            s += std::to_string(val.size());
            s += " bytes>\"";
            return s;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<ValueArray>>) {
            if (!val || val->elements.empty()) return "[]";
            std::string s = "[";
            for (size_t i = 0; i < val->elements.size(); i++) {
                if (i > 0) s += ",";
                s += value_to_json(val->elements[i], indent + 2);
            }
            s += "]";
            return s;
        } else if constexpr (std::is_same_v<T, Struct>) {
            if (val.fields.empty()) return "{}";
            std::string s = "{";
            for (size_t i = 0; i < val.fields.size(); i++) {
                if (i > 0) s += ",";
                s += "\"" + std::to_string(val.fields[i].tag) + "\":";
                s += value_to_json(val.fields[i].value, indent + 2);
            }
            s += "}";
            return s;
        } else if constexpr (std::is_same_v<T, Timestamp>) {
            return "\"" + val.to_iso8601() + "\"";
        } else if constexpr (std::is_same_v<T, Uuid>) {
            return "\"" + val.to_string() + "\"";
        } else {
            return "null";
        } }, v);
    }

    auto MetaBlock::to_json() const -> std::string
    {
        std::string json = "{\n";

        std::map<Namespace, std::vector<const Entry *>> grouped;
        for (const auto &e : entries)
        {
            grouped[e.ns].push_back(&e);
        }

        bool first_ns = true;
        for (const auto &[ns, ns_entries] : grouped)
        {
            if (!first_ns)
                json += ",\n";
            first_ns = false;

            json += "  \"" + namespace_name(ns) + "\": {\n";

            bool first_entry = true;
            for (const auto *e : ns_entries)
            {
                if (!first_entry)
                    json += ",\n";
                first_entry = false;

                char tag_buf[8];
                std::snprintf(tag_buf, sizeof(tag_buf), "0x%04X", e->tag);

                json += "    \"" + std::string(tag_buf) + "\": ";
                if (e->is_opaque)
                {
                    json += "\"<opaque " + std::to_string(e->opaque_data.size()) + " bytes>\"";
                }
                else
                {
                    json += value_to_json(e->value, 4);
                }
            }
            json += "\n  }";
        }

        json += "\n}\n";
        return json;
    }

    auto MetaBlock::from_exif(std::span<const uint8_t> exif_blob)
        -> std::expected<MetaBlock, MetaError>
    {
        return parse_exif_blob(exif_blob);
    }

    auto MetaBlock::from_xmp(std::string_view xmp_xml)
        -> std::expected<MetaBlock, MetaError>
    {
        MetaBlock block;

        auto extract_tag = [&](std::string_view tag_name) -> std::string
        {
            std::string open = "<" + std::string(tag_name) + ">";
            std::string close = "</" + std::string(tag_name) + ">";
            auto start = xmp_xml.find(open);
            if (start == std::string_view::npos)
                return "";
            start += open.size();
            auto end = xmp_xml.find(close, start);
            if (end == std::string_view::npos)
                return "";
            return std::string(xmp_xml.substr(start, end - start));
        };

        auto title = extract_tag("dc:title");
        if (!title.empty())
        {
            block.set(Namespace::Content, content::TITLE,
                      LocalizedString{"", title});
        }

        auto desc = extract_tag("dc:description");
        if (!desc.empty())
        {
            block.set(Namespace::Content, content::DESCRIPTION,
                      LocalizedString{"", desc});
        }

        auto creator = extract_tag("dc:creator");
        if (!creator.empty())
        {
            block.set(Namespace::Rights, rights::CREATOR,
                      LocalizedString{"", creator});
        }

        auto license_url = extract_tag("xmpRights:WebStatement");
        if (!license_url.empty())
        {
            block.set(Namespace::Rights, rights::LICENSE_URL, license_url);
        }

        return block;
    }

    auto MetaBlock::to_exif() const -> std::vector<uint8_t>
    {

        return {};
    }

    auto MetaBlock::to_xmp() const -> std::string
    {
        std::string xmp = R"(<?xpacket begin="ï»¿" id="W5M0MpCehiHzreSzNTczkc9d"?>)"
                          "\n";
        xmp += R"(<x:xmpmeta xmlns:x="adobe:ns:meta/">)"
               "\n";
        xmp += R"(<rdf:RDF xmlns:rdf="http:
    xmp += R"(<rdf:Description rdf:about="")"
               "\n";
        xmp += R"(  xmlns:dc="http:
    xmp += R"(  xmlns:xmpRights="http:


    auto title = get_title();
    if (title) {
        xmp += "  <dc:title>" + json_escape(*title) + "</dc:title>\n";
    }

    auto license = get_license();
    if (license) {
        xmp += "  <xmpRights:WebStatement>" + json_escape(*license)
             + "</xmpRights:WebStatement>\n";
    }

    xmp += "</rdf:Description>\n";
    xmp += "</rdf:RDF>\n";
    xmp += "</x:xmpmeta>\n";
    xmp += R"(<?xpacket end="w"?>)";
        return xmp;
    }

}
