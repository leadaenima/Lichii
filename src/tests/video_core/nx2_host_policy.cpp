// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "common/scope_exit.h"
#include "common/settings.h"
#include "video_core/nx2.h"
#include "video_core/nx2_host_policy.h"
#include "video_core/vulkan_common/vulkan_device.h"

TEST_CASE("Switch 2 uses on-demand CPU readback when Ampere flag is clear",
          "[video_core][oot][readback]") {
    const bool previous_switch2 = Settings::CurrentProgramIsSwitch2();
    const bool previous_ampere = Tegra::g_nx2_ampere.load();
    SCOPE_EXIT {
        Settings::SetCurrentProgramIsSwitch2(previous_switch2);
        Tegra::g_nx2_ampere.store(previous_ampere);
    };

    Tegra::g_nx2_ampere.store(false);
    Settings::SetCurrentProgramIsSwitch2(false);
    REQUIRE_FALSE(Tegra::Nx2OnDemandCpuReadback());

    Settings::SetCurrentProgramIsSwitch2(true);
    REQUIRE(Tegra::Nx2OnDemandCpuReadback());

    Settings::SetCurrentProgramIsSwitch2(false);
    Tegra::g_nx2_ampere.store(true);
    REQUIRE(Tegra::Nx2OnDemandCpuReadback());
}

TEST_CASE("AMD proprietary and open-source GPU split flickers", "[video_core][oot][split]") {
    using Vulkan::DriverGpuSplitFlickers;
    CHECK(DriverGpuSplitFlickers(VK_DRIVER_ID_AMD_PROPRIETARY));
    CHECK(DriverGpuSplitFlickers(VK_DRIVER_ID_AMD_OPEN_SOURCE));
    CHECK_FALSE(DriverGpuSplitFlickers(VK_DRIVER_ID_MESA_RADV));
    CHECK_FALSE(DriverGpuSplitFlickers(VK_DRIVER_ID_NVIDIA_PROPRIETARY));
}

TEST_CASE("Host GPU split is refused when the driver flag is set", "[video_core][oot][split]") {
    const bool previous = Tegra::g_nx2_host_split_unsafe.load();
    SCOPE_EXIT {
        Tegra::g_nx2_host_split_unsafe.store(previous);
    };

    Tegra::g_nx2_host_split_unsafe.store(false);
    REQUIRE(Tegra::Nx2HostGpuSplitAllowed());
    Tegra::g_nx2_host_split_unsafe.store(true);
    REQUIRE_FALSE(Tegra::Nx2HostGpuSplitAllowed());
}
