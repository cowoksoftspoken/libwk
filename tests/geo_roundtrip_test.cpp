
#include <gtest/gtest.h>
#include <wk/wkmeta.hpp>
#include <cmath>

using namespace wk::meta;

TEST(GeoRoundtripTest, BasicCoordinates) {
    MetaBlock block;


    block.set(Namespace::Geo, geo::LAT, 48.856614);
    block.set(Namespace::Geo, geo::LON, 2.352222);

    auto bytes = block.serialize();
    auto parsed = MetaBlock::parse(bytes);
    ASSERT_TRUE(parsed.has_value());

    auto lat = parsed->get_geo_lat();
    auto lon = parsed->get_geo_lon();
    ASSERT_TRUE(lat.has_value());
    ASSERT_TRUE(lon.has_value());

    EXPECT_NEAR(*lat, 48.856614, 1e-10);
    EXPECT_NEAR(*lon, 2.352222, 1e-10);
}

TEST(GeoRoundtripTest, NegativeCoordinates) {
    MetaBlock block;


    block.set(Namespace::Geo, geo::LAT, -34.603722);
    block.set(Namespace::Geo, geo::LON, -58.381592);

    auto bytes = block.serialize();
    auto parsed = MetaBlock::parse(bytes);
    ASSERT_TRUE(parsed.has_value());

    auto lat = parsed->get_geo_lat();
    auto lon = parsed->get_geo_lon();
    ASSERT_TRUE(lat.has_value());
    ASSERT_TRUE(lon.has_value());

    EXPECT_NEAR(*lat, -34.603722, 1e-10);
    EXPECT_NEAR(*lon, -58.381592, 1e-10);
}

TEST(GeoRoundtripTest, DateLine) {
    MetaBlock block;


    block.set(Namespace::Geo, geo::LAT, 0.0);
    block.set(Namespace::Geo, geo::LON, 179.999999);

    auto bytes = block.serialize();
    auto parsed = MetaBlock::parse(bytes);
    ASSERT_TRUE(parsed.has_value());

    auto lon = parsed->get_geo_lon();
    ASSERT_TRUE(lon.has_value());
    EXPECT_NEAR(*lon, 179.999999, 1e-10);
}

TEST(GeoRoundtripTest, Poles) {

    MetaBlock block;
    block.set(Namespace::Geo, geo::LAT, 90.0);
    block.set(Namespace::Geo, geo::LON, 0.0);

    auto bytes = block.serialize();
    auto parsed = MetaBlock::parse(bytes);
    ASSERT_TRUE(parsed.has_value());

    EXPECT_NEAR(*parsed->get_geo_lat(), 90.0, 1e-10);


    MetaBlock block2;
    block2.set(Namespace::Geo, geo::LAT, -90.0);
    block2.set(Namespace::Geo, geo::LON, 180.0);

    auto bytes2 = block2.serialize();
    auto parsed2 = MetaBlock::parse(bytes2);
    ASSERT_TRUE(parsed2.has_value());

    EXPECT_NEAR(*parsed2->get_geo_lat(), -90.0, 1e-10);
}

TEST(GeoRoundtripTest, AltitudeAndErrors) {
    MetaBlock block;
    block.set(Namespace::Geo, geo::LAT, 27.988056);
    block.set(Namespace::Geo, geo::LON, 86.925278);
    block.set(Namespace::Geo, geo::ALT, 8848.86f);
    block.set(Namespace::Geo, geo::HPOS_ERR, 2.5f);
    block.set(Namespace::Geo, geo::VPOS_ERR, 5.0f);

    auto bytes = block.serialize();
    auto parsed = MetaBlock::parse(bytes);
    ASSERT_TRUE(parsed.has_value());

    auto alt = parsed->get(Namespace::Geo, geo::ALT);
    ASSERT_TRUE(alt.has_value());
    auto* alt_val = std::get_if<float>(*alt);
    ASSERT_NE(alt_val, nullptr);
    EXPECT_NEAR(*alt_val, 8848.86f, 0.01f);

    auto herr = parsed->get(Namespace::Geo, geo::HPOS_ERR);
    ASSERT_TRUE(herr.has_value());
    EXPECT_NEAR(*std::get_if<float>(*herr), 2.5f, 1e-6f);
}

TEST(GeoRoundtripTest, FullGeoMetadata) {
    MetaBlock block;
    block.set(Namespace::Geo, geo::LAT, 1.283333);
    block.set(Namespace::Geo, geo::LON, 103.833333);
    block.set(Namespace::Geo, geo::ALT, 15.0f);
    block.set(Namespace::Geo, geo::SPEED, 1.5f);
    block.set(Namespace::Geo, geo::HEADING, 270.0f);
    block.set(Namespace::Geo, geo::COUNTRY, std::string("SG"));
    block.set(Namespace::Geo, geo::CITY,
              LocalizedString{"en", "Singapore"});

    auto bytes = block.serialize();
    auto parsed = MetaBlock::parse(bytes);
    ASSERT_TRUE(parsed.has_value());

    EXPECT_NEAR(*parsed->get_geo_lat(), 1.283333, 1e-10);
    EXPECT_NEAR(*parsed->get_geo_lon(), 103.833333, 1e-10);

    auto country = parsed->get(Namespace::Geo, geo::COUNTRY);
    ASSERT_TRUE(country.has_value());
    EXPECT_EQ(*std::get_if<std::string>(*country), "SG");
}

TEST(GeoRoundtripTest, SubMeterPrecision) {


    MetaBlock block;
    double precise_lat = 51.50073849;
    double precise_lon = -0.12462839;

    block.set(Namespace::Geo, geo::LAT, precise_lat);
    block.set(Namespace::Geo, geo::LON, precise_lon);

    auto bytes = block.serialize();
    auto parsed = MetaBlock::parse(bytes);
    ASSERT_TRUE(parsed.has_value());

    EXPECT_NEAR(*parsed->get_geo_lat(), precise_lat, 1e-10);
    EXPECT_NEAR(*parsed->get_geo_lon(), precise_lon, 1e-10);
}
