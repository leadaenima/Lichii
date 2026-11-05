// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>
#include <catch2/catch_test_macros.hpp>

#include "video_core/texture_cache/format_lookup_table.h"
#include "video_core/textures/texture.h"

TEST_CASE("Legacy texture indices never decode as native header offsets",
          "[video_core][texture_header_index]") {
    using Tegra::Texture::TextureHeaderIndex;
    REQUIRE(TextureHeaderIndex(64, 63, false) == 64);
    REQUIRE(TextureHeaderIndex(2016, 63, false) == 2016);
    REQUIRE(TextureHeaderIndex(32, 0, false) == 32);
}

TEST_CASE("Native header offsets decode only when the shifted index is valid",
          "[video_core][texture_header_index]") {
    using Tegra::Texture::TextureHeaderIndex;
    REQUIRE(TextureHeaderIndex(64, 63, true) == 2);
    REQUIRE(TextureHeaderIndex(2016, 63, true) == 63);
    REQUIRE(TextureHeaderIndex(2048, 63, true) == 2048);
    REQUIRE(TextureHeaderIndex(65, 63, true) == 65);
    REQUIRE(TextureHeaderIndex(32, 0, true) == 32);
}

TEST_CASE("Resolved texture indices stay unchanged for both memory layouts",
          "[video_core][texture_header_index]") {
    using Tegra::Texture::TextureHeaderIndex;
    constexpr u32 maximum = std::numeric_limits<u32>::max();
    for (const bool native_tiling : {false, true}) {
        REQUIRE(TextureHeaderIndex(0, 0, native_tiling) == 0);
        REQUIRE(TextureHeaderIndex(32, 63, native_tiling) == 32);
        REQUIRE(TextureHeaderIndex(64, 64, native_tiling) == 64);
        REQUIRE(TextureHeaderIndex(maximum, maximum, native_tiling) == maximum);
        REQUIRE(TextureHeaderIndex(maximum, 63, native_tiling) == maximum);
    }
}

TEST_CASE("Out-of-range handles are not used as shader TIC indices",
          "[video_core][texture_header_index]") {
    using Tegra::Texture::ResolveTextureHeaderIndex;
    // Switch 2 header-offset handles that land inside the pool still resolve.
    REQUIRE(ResolveTextureHeaderIndex(64, 63, true) == 2);
    REQUIRE(ResolveTextureHeaderIndex(2016, 63, true) == 63);
    REQUIRE(ResolveTextureHeaderIndex(32, 63, true) == 32);
    REQUIRE(ResolveTextureHeaderIndex(0, 0, true) == 0);
    // Handles that remain past the pool after decode must not be read. The previous
    // ASSERT(index <= tic_limit) logged and continued, so shader compile sampled garbage
    // headers (format 65/46/19/...) and AMDVLK later crashed.
    REQUIRE_FALSE(ResolveTextureHeaderIndex(2048, 63, true).has_value());
    REQUIRE_FALSE(ResolveTextureHeaderIndex(65, 63, true).has_value());
    REQUIRE_FALSE(ResolveTextureHeaderIndex(32, 0, true).has_value());
    REQUIRE_FALSE(ResolveTextureHeaderIndex(64, 63, false).has_value());
    REQUIRE_FALSE(ResolveTextureHeaderIndex(2016, 63, false).has_value());
    REQUIRE_FALSE(
        ResolveTextureHeaderIndex(std::numeric_limits<u32>::max(), 63, true).has_value());
}

TEST_CASE("Color2D fallback header is a known UNORM RGBA format",
          "[video_core][texture_header_index]") {
    const auto header = Tegra::Texture::Color2DFallbackHeader();
    REQUIRE(header.format.Value() == Tegra::Texture::TextureFormat::A8B8G8R8);
    REQUIRE(header.texture_type.Value() == Tegra::Texture::TextureType::Texture2D);
    REQUIRE(header.normalized_coords.Value() == 1);
    REQUIRE(VideoCommon::PixelFormatFromTextureInfo(
                header.format, header.r_type, header.g_type, header.b_type, header.a_type,
                header.srgb_conversion) == VideoCore::Surface::PixelFormat::A8B8G8R8_UNORM);
}
