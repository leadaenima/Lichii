// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/settings.h"
#include "video_core/gpu_split.h"
#include "video_core/nx2.h"

namespace Tegra {

// #147 trap-and-flush, not Preflushable fence copies. Harvest 93597b5c38 on RX 6800
// OoT: g_nx2_ampere stayed 0 while the GPU split was running, so the Ampere-only
// gate never fired and Low preflush copied 100-400 MiB/5s.
[[nodiscard]] inline bool Nx2OnDemandCpuReadback() {
    return g_nx2_ampere.load(std::memory_order_relaxed) || Settings::CurrentProgramIsSwitch2() ||
           VideoCommon::GpuSplit::Get().BackRunning();
}

[[nodiscard]] inline bool Nx2HostGpuSplitAllowed() {
    return !g_nx2_host_split_unsafe.load(std::memory_order_relaxed);
}

} // namespace Tegra
