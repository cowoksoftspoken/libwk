# WK Metadata Guide

This guide shows how to use the WKMETA metadata system with practical examples for common use cases.

---

## 1. Photo from Camera

When encoding a photo taken with a digital camera, include capture parameters, GPS location, and timestamps:

### CLI Example

```bash
# Encode with metadata
wkenc --quality 85 photo.ppm photo.wk

# Add camera metadata
wkmeta-edit \
  --set CAPTURE.MAKE "Canon" \
  --set CAPTURE.MODEL "EOS R5" \
  --set CAPTURE.ISO 400 \
  --set GEO.LAT 48.856614 \
  --set GEO.LON 2.352222 \
  --set GEO.COUNTRY FR \
  --set GEO.CITY "en:Paris" \
  --set RIGHTS.CREATOR "en:John Doe" \
  --set RIGHTS.LICENSE CC-BY-4.0 \
  --set CONTENT.TITLE "en:Eiffel Tower at Sunset" \
  --set CONTENT.ALT_TEXT "en:The Eiffel Tower silhouetted against an orange sunset sky" \
  photo.wk
```

### Stored Metadata (JSON output)

```json
{
  "capture": {
    "0x0001": "Canon",
    "0x0002": "EOS R5",
    "0x000A": 400
  },
  "geo": {
    "0x0001": 48.856614,
    "0x0002": 2.352222,
    "0x000A": "FR",
    "0x000C": {"en": "Paris"}
  },
  "rights": {
    "0x0001": {"en": "John Doe"},
    "0x0004": "CC-BY-4.0"
  },
  "content": {
    "0x0001": {"en": "Eiffel Tower at Sunset"},
    "0x0003": {"en": "The Eiffel Tower silhouetted against an orange sunset sky"}
  }
}
```

---

## 2. Animated Sticker

For animated stickers used in messaging apps:

```bash
# Encode animated sticker
wkenc --lossless --tile-size 128 sticker_frames.ppm sticker.wk

# Add animation metadata
wkmeta-edit \
  --set ANIM.TITLE "en:Happy Dance" \
  --set ANIM.CATEGORY sticker \
  --set RATING.AUDIENCE 0 \
  --set CONTENT.KEYWORDS "happy,dance,celebration" \
  sticker.wk
```

### Key Fields

| Namespace | Tag       | Example Value |
|-----------|-----------|--------------|
| ANIM      | TITLE     | `{"en": "Happy Dance"}` |
| ANIM      | CATEGORY  | `"sticker"` |
| ANIM      | DURATION_MS | `2000` |
| ANIM      | FPS_TARGET | `24.0` |
| RATING    | AUDIENCE  | `0` (all ages) |

---

## 3. Drone Aerial with GPS

For aerial photography from drones, include detailed GPS and motion data:

```bash
wkenc --quality 90 --hdr --tile-size 512 aerial.ppm aerial.wk

wkmeta-edit \
  --set CAPTURE.MAKE "DJI" \
  --set CAPTURE.MODEL "Mavic 3 Pro" \
  --set GEO.LAT -6.200000 \
  --set GEO.LON 106.816666 \
  --set GEO.ALT 150.5 \
  --set GEO.HPOS_ERR 2.5 \
  --set GEO.SPEED 5.2 \
  --set GEO.HEADING 270.0 \
  --set GEO.PITCH -45.0 \
  --set GEO.COUNTRY ID \
  --set GEO.CITY "en:Jakarta" \
  --set GEO.PLACE_NAME "en:Monas National Monument" \
  --set CONTENT.TITLE "en:Jakarta Skyline from Above" \
  --set TIME.TIMEZONE_ID "Asia/Jakarta" \
  aerial.wk
```

### GPS Precision

WK stores GPS coordinates as IEEE 754 `FLOAT64` (double-precision), providing:
- **Latitude/Longitude**: ~1 nanometer precision at the equator
- **Altitude**: `FLOAT32` (~7 significant digits, sub-meter precision)
- **Positioning errors**: 1σ standard deviation in metres

---

## 4. AI-Generated Image with Region Annotations

For AI-generated or AI-assisted images, it's important to document provenance:

```bash
wkenc --quality 95 --tile-size 256 ai_image.ppm ai_image.wk

wkmeta-edit \
  --set RATING.AI_GENERATED 2 \
  --set CONTENT.TITLE "en:Cyberpunk Cityscape" \
  --set CONTENT.DESCRIPTION "en:A futuristic city at night with neon lights" \
  --set RIGHTS.CREATOR "en:AI Studio v3.0" \
  --set RIGHTS.LICENSE CC-BY-4.0 \
  --set CAPTURE.SOFTWARE "StableDiffusion XL 2.0" \
  ai_image.wk
```

### AI Generation Flags

| Value | Meaning |
|-------|---------|
| 0     | Human-created |
| 1     | Partial AI assistance |
| 2     | Fully AI-generated |

### Region Annotations (Programmatic)

```cpp
#include <wk/wkmeta.hpp>

wk::meta::MetaBlock meta;

// Add a face region
wk::meta::Struct face;
face.fields.push_back({wk::meta::Namespace::Region, 
    wk::meta::region::NAME,
    wk::meta::LocalizedString{"en", "Main Subject"}});
face.fields.push_back({wk::meta::Namespace::Region,
    wk::meta::region::TYPE, uint8_t(1)});  // object
face.fields.push_back({wk::meta::Namespace::Region,
    wk::meta::region::OBJECT_CLASS, std::string("building")});
face.fields.push_back({wk::meta::Namespace::Region,
    wk::meta::region::CONFIDENCE, float(0.92)});

meta.entries.push_back({wk::meta::Namespace::Region, 0x0001, face});
```

---

## 5. Querying Metadata

### Dump all metadata as JSON

```bash
wkmeta-dump photo.wk
```

### Export during decoding

```bash
wkdec --export-meta metadata.json photo.wk decoded.ppm
```

### Programmatic access (C++)

```cpp
#include <wk/wk.hpp>
#include <wk/wkmeta.hpp>

auto data = read_file("photo.wk");
auto file = wk::parse_container(data);

if (file && file->metadata) {
    auto& meta = *file->metadata;
    
    // Get GPS coordinates
    if (auto lat = meta.get_geo_lat()) {
        printf("Latitude: %.6f\n", *lat);
    }
    
    // Get title
    if (auto title = meta.get_title("en")) {
        printf("Title: %s\n", title->c_str());
    }
    
    // Get license
    if (auto lic = meta.get_license()) {
        printf("License: %s\n", lic->c_str());
    }
    
    // Get all region annotations
    auto regions = meta.get_regions();
    for (const auto& r : regions) {
        printf("Region: %s (confidence: %.2f)\n",
               r.name.c_str(), r.confidence);
    }
}
```

---

## Metadata Compatibility

| Standard    | WK Namespace | Mapping |
|-------------|-------------|---------|
| Exif GPS    | GEO         | Full GPS IFD → signed decimal degrees |
| Exif Camera | CAPTURE     | Make, Model, ISO, etc. |
| XMP dc:     | CONTENT     | title, description, creator |
| XMP rights  | RIGHTS      | WebStatement → LICENSE_URL |
| IPTC        | CONTENT     | Genre, Subject, Scene codes |
| Dublin Core | CONTENT     | Title, Description (as LSTR) |
| SPDX        | RIGHTS      | LICENSE_SPDX identifier |
| BCP 47      | LSTR type   | Language tags on all LSTR values |
