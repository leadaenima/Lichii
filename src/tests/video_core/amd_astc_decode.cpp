// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/vulkan_common/vulkan_device.h"

TEST_CASE("AMD depth-compare samplers need nearest LOD0, not a drop toggle",
          "[video_core][oot][dref]") {
    using Vulkan::ApplyNearestLod0DepthCompare;
    using Vulkan::DriverNeedsNearestLod0DepthCompare;
    CHECK(DriverNeedsNearestLod0DepthCompare(VK_DRIVER_ID_AMD_PROPRIETARY));
    CHECK(DriverNeedsNearestLod0DepthCompare(VK_DRIVER_ID_AMD_OPEN_SOURCE));
    CHECK(DriverNeedsNearestLod0DepthCompare(VK_DRIVER_ID_MESA_RADV));
    CHECK_FALSE(DriverNeedsNearestLod0DepthCompare(VK_DRIVER_ID_NVIDIA_PROPRIETARY));

    VkSamplerCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 8.0f,
        .compareEnable = VK_TRUE,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .minLod = 0.0f,
        .maxLod = 16.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .unnormalizedCoordinates = VK_FALSE,
    };
    ApplyNearestLod0DepthCompare(ci);
    CHECK(ci.compareEnable == VK_TRUE);
    CHECK(ci.compareOp == VK_COMPARE_OP_LESS_OR_EQUAL);
    CHECK(ci.magFilter == VK_FILTER_NEAREST);
    CHECK(ci.minFilter == VK_FILTER_NEAREST);
    CHECK(ci.mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST);
    CHECK(ci.anisotropyEnable == VK_FALSE);
    CHECK(ci.maxAnisotropy == 1.0f);
    CHECK(ci.minLod == 0.0f);
    CHECK(ci.maxLod == 0.25f);
}

TEST_CASE("AMD drivers prefer host ASTC decode", "[video_core][astc][amd]") {
    using Vulkan::DriverPrefersHostAstcDecode;
    CHECK(DriverPrefersHostAstcDecode(VK_DRIVER_ID_AMD_PROPRIETARY));
    CHECK(DriverPrefersHostAstcDecode(VK_DRIVER_ID_AMD_OPEN_SOURCE));
    CHECK(DriverPrefersHostAstcDecode(VK_DRIVER_ID_MESA_RADV));
    CHECK_FALSE(DriverPrefersHostAstcDecode(VK_DRIVER_ID_NVIDIA_PROPRIETARY));
    CHECK_FALSE(DriverPrefersHostAstcDecode(VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS));
}
