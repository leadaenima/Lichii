// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "common/scope_exit.h"
#include "common/settings.h"
#include "video_core/nx2.h"
#include "video_core/nx2_host_policy.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace {

void RestoreHostDriver(u32 driver_id, bool unsafe, bool ampere) {
    Tegra::g_nx2_vk_driver_id.store(driver_id);
    Tegra::g_nx2_host_split_unsafe.store(unsafe);
    Tegra::g_nx2_ampere.store(ampere);
}

} // Anonymous namespace

TEST_CASE("AMD driver IDs match the Vulkan spec", "[video_core][oot][split]") {
    CHECK(Tegra::Nx2VkDriverAmdProprietary ==
          static_cast<u32>(VK_DRIVER_ID_AMD_PROPRIETARY));
    CHECK(Tegra::Nx2VkDriverAmdOpenSource == static_cast<u32>(VK_DRIVER_ID_AMD_OPEN_SOURCE));
    CHECK(Tegra::Nx2VkDriverGpuSplitFlickers(VK_DRIVER_ID_AMD_PROPRIETARY));
    CHECK(Tegra::Nx2VkDriverGpuSplitFlickers(VK_DRIVER_ID_AMD_OPEN_SOURCE));
    CHECK_FALSE(Tegra::Nx2VkDriverGpuSplitFlickers(VK_DRIVER_ID_MESA_RADV));
    CHECK_FALSE(Tegra::Nx2VkDriverGpuSplitFlickers(VK_DRIVER_ID_NVIDIA_PROPRIETARY));
    using Vulkan::DriverGpuSplitFlickers;
    CHECK(DriverGpuSplitFlickers(VK_DRIVER_ID_AMD_PROPRIETARY));
    CHECK(DriverGpuSplitFlickers(VK_DRIVER_ID_AMD_OPEN_SOURCE));
    CHECK_FALSE(DriverGpuSplitFlickers(VK_DRIVER_ID_MESA_RADV));
}

TEST_CASE("a3e94 session reset cannot re-enable an AMD GPU split", "[video_core][oot][split]") {
    const u32 previous_id = Tegra::g_nx2_vk_driver_id.load();
    const bool previous_unsafe = Tegra::g_nx2_host_split_unsafe.load();
    const bool previous_ampere = Tegra::g_nx2_ampere.load();
    SCOPE_EXIT {
        RestoreHostDriver(previous_id, previous_unsafe, previous_ampere);
    };

    // Device ctor on AMD proprietary (harvest driver_id=1).
    Tegra::Nx2NoteHostVulkanDriver(VK_DRIVER_ID_AMD_PROPRIETARY);
    REQUIRE_FALSE(Tegra::Nx2HostGpuSplitAllowed());

    // nvdrv Module::Module starts the game ~65s later and clears Ampere. It must
    // not clear the host driver. This is the a3e94f6b08 failure.
    Tegra::g_nx2_ampere.store(false);
    REQUIRE_FALSE(Tegra::Nx2HostGpuSplitAllowed());

    // Even if something wipes only the unsafe bit, the driver id still blocks.
    Tegra::g_nx2_host_split_unsafe.store(false);
    REQUIRE_FALSE(Tegra::Nx2HostGpuSplitAllowed());

    Tegra::Nx2NoteHostVulkanDriver(VK_DRIVER_ID_NVIDIA_PROPRIETARY);
    REQUIRE(Tegra::Nx2HostGpuSplitAllowed());
}

TEST_CASE("OoT program id uses on-demand readback with split off and Ampere clear",
          "[video_core][oot][readback]") {
    const bool previous_switch2 = Settings::CurrentProgramIsSwitch2();
    const bool previous_ampere = Tegra::g_nx2_ampere.load();
    const u64 previous_program = Settings::GetCurrentProgramID();
    SCOPE_EXIT {
        Settings::SetCurrentProgramIsSwitch2(previous_switch2);
        Settings::SetCurrentProgramID(previous_program);
        Tegra::g_nx2_ampere.store(previous_ampere);
    };

    Tegra::g_nx2_ampere.store(false);
    Settings::SetCurrentProgramIsSwitch2(false);
    Settings::SetCurrentProgramID(0);
    REQUIRE_FALSE(VideoCommon::GpuSplit::Get().BackRunning());
    REQUIRE_FALSE(Tegra::Nx2OnDemandCpuReadback());

    Settings::SetCurrentProgramID(Tegra::kNx2OcarinaOfTime);
    REQUIRE(Tegra::Nx2OnDemandCpuReadback());

    Settings::SetCurrentProgramID(0);
    Settings::SetCurrentProgramIsSwitch2(true);
    REQUIRE(Tegra::Nx2OnDemandCpuReadback());
}

TEST_CASE("Low OoT skips synchronous buffer downloads so shade cannot Finish-storm",
          "[video_core][oot][readback]") {
    const bool previous_switch2 = Settings::CurrentProgramIsSwitch2();
    const bool previous_ampere = Tegra::g_nx2_ampere.load();
    const u64 previous_program = Settings::GetCurrentProgramID();
    const auto previous_accuracy = Settings::values.gpu_accuracy.GetValue();
    SCOPE_EXIT {
        Settings::SetCurrentProgramIsSwitch2(previous_switch2);
        Settings::SetCurrentProgramID(previous_program);
        Tegra::g_nx2_ampere.store(previous_ampere);
        Settings::values.gpu_accuracy.SetValue(previous_accuracy);
    };

    Tegra::g_nx2_ampere.store(false);
    Settings::SetCurrentProgramIsSwitch2(false);
    Settings::SetCurrentProgramID(Tegra::kNx2OcarinaOfTime);
    Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::Low);
    REQUIRE(Tegra::Nx2SkipSynchronousBufferDownload());

    Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::High);
    REQUIRE_FALSE(Tegra::Nx2SkipSynchronousBufferDownload());

    Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::Low);
    Settings::SetCurrentProgramID(0);
    REQUIRE_FALSE(Tegra::Nx2SkipSynchronousBufferDownload());
}
