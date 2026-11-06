// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/vulkan_common/vulkan_device.h"

TEST_CASE("AMD drivers prefer host ASTC decode", "[video_core][astc][amd]") {
    using Vulkan::DriverPrefersHostAstcDecode;
    CHECK(DriverPrefersHostAstcDecode(VK_DRIVER_ID_AMD_PROPRIETARY));
    CHECK(DriverPrefersHostAstcDecode(VK_DRIVER_ID_AMD_OPEN_SOURCE));
    CHECK(DriverPrefersHostAstcDecode(VK_DRIVER_ID_MESA_RADV));
    CHECK_FALSE(DriverPrefersHostAstcDecode(VK_DRIVER_ID_NVIDIA_PROPRIETARY));
    CHECK_FALSE(DriverPrefersHostAstcDecode(VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS));
}
