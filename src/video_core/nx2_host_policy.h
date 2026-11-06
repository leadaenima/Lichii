// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/settings.h"
#include "video_core/gpu_split.h"
#include "video_core/nx2.h"

namespace Tegra {

inline constexpr u64 kNx2OcarinaOfTime = 0x0400486024DEC000ULL;

// #147 trap-and-flush, not Preflushable fence copies. Must not depend on the split
// running: a3e94f6b08 started the split after Module wiped the AMD flag, and the
// shade storm returned. OoT is Switch 2 even when g_nx2_ampere and the Switch2
// setting both read 0 (harvest: ampere=0 every sample).
[[nodiscard]] inline bool Nx2OnDemandCpuReadback() {
    return g_nx2_ampere.load(std::memory_order_relaxed) || Settings::CurrentProgramIsSwitch2() ||
           Settings::GetCurrentProgramID() == kNx2OcarinaOfTime ||
           VideoCommon::GpuSplit::Get().BackRunning();
}

// 82c813ca8f: #147 still Finish()ed 200–350 times / 5s on Low OoT because
// GetFlushArea returned preemtive=false, so OnCPURead WaitForSyncOperation'd
// every guest read. Low already skips texture flushes; skip the buffer wait too.
[[nodiscard]] inline bool Nx2SkipSynchronousBufferDownload() {
    return Nx2OnDemandCpuReadback() &&
           Settings::values.gpu_accuracy.GetValue() != Settings::GpuAccuracy::High;
}

// MaybeStart must call this every time. Driver id and the unsafe bit are host
// facts; a session reset that clears only g_nx2_ampere cannot re-enable the split.
[[nodiscard]] inline bool Nx2HostGpuSplitAllowed() {
    return !g_nx2_host_split_unsafe.load(std::memory_order_relaxed) &&
           !Nx2VkDriverGpuSplitFlickers(g_nx2_vk_driver_id.load(std::memory_order_relaxed));
}

} // namespace Tegra
