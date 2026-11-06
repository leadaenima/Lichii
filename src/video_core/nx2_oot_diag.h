// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>

#include <fmt/format.h>

#include "common/logging.h"
#include "common/settings.h"
#include "video_core/gpu_split.h"
#include "video_core/nx2.h"
#include "video_core/surface.h"

namespace VideoCommon::OotDiag {

inline constexpr u64 kOcarinaOfTime = 0x0400486024DEC000ULL;

[[nodiscard]] inline bool Enabled() {
    static const bool forced = [] {
        const char* value = std::getenv("EDEN_NX2_OOT_DIAG");
        return value != nullptr && value[0] != '0';
    }();
    return forced || Settings::GetCurrentProgramID() == kOcarinaOfTime;
}

enum class C : u32 {
    UploadCpuConverted,
    UploadAccelerated,
    UploadRaw,
    UploadClearSkip,
    DrefKept,
    DrefDropped,
    DeferredClearFlush,
    ReadbackFinish,
    ReadbackBytes,
    Count,
};

inline std::array<std::atomic<u64>, static_cast<size_t>(C::Count)> counters{};
inline std::atomic<u32> last_color_format{0};
inline std::atomic<u32> last_depth_format{0};
inline std::atomic<u64> frames{0};
inline std::atomic<u32> cached_driver_id{0};
inline std::atomic<u8> cached_device_flags{0};
inline std::atomic<bool> boot_logged{false};

// Grain on lit Link+walls+floor after CPU ASTC is armed means the first wrong pass is
// not ASTC. These hints name the remaining live paths so a log can pick the next change.
[[nodiscard]] inline std::string Hint(u64 cpu, u64 accel, u64 skip, u64 kept, u64 dropped,
                                      u64 flush, u64 finish, bool low_accuracy,
                                      bool split_back) {
    std::string hint;
    const auto add = [&](const char* part) {
        if (!hint.empty()) {
            hint += ' ';
        }
        hint += part;
    };
    if (accel > 0) {
        add("GPU-ASTC-still-running");
    } else if (cpu > 0) {
        add("ASTC-CPU-grain-is-elsewhere");
    }
    if (dropped > 0) {
        add("dref-dropped");
    }
    if (kept > 0) {
        add("dref-kept");
    }
    if (skip > 0) {
        add("clear-skip-uploads");
    }
    if (flush > 0) {
        add("deferred-z-flush");
    }
    if (finish > 0) {
        add("cpu-readback-finish");
    }
    if (low_accuracy && split_back) {
        add("Low+SPLIT-#163-flicker");
    }
    if (hint.empty()) {
        add("no-hot-path-this-window");
    }
    return hint;
}

inline void Add(C c, u64 n = 1) {
    if (Enabled()) {
        counters[static_cast<size_t>(c)].fetch_add(n, std::memory_order_relaxed);
    }
}

inline void NoteFormats(VideoCore::Surface::PixelFormat format) {
    if (!Enabled()) {
        return;
    }
    const auto type = VideoCore::Surface::GetFormatType(format);
    if (type == VideoCore::Surface::SurfaceType::Depth ||
        type == VideoCore::Surface::SurfaceType::DepthStencil ||
        type == VideoCore::Surface::SurfaceType::Stencil) {
        last_depth_format.store(static_cast<u32>(format), std::memory_order_relaxed);
    } else {
        last_color_format.store(static_cast<u32>(format), std::memory_order_relaxed);
    }
}

inline u64 Take(C c) {
    return counters[static_cast<size_t>(c)].exchange(0, std::memory_order_relaxed);
}

inline void CacheDevice(u32 driver_id, bool optimal_astc, bool prefer_host) {
    cached_driver_id.store(driver_id, std::memory_order_relaxed);
    cached_device_flags.store(static_cast<u8>((optimal_astc ? 1 : 0) | (prefer_host ? 2 : 0)),
                              std::memory_order_relaxed);
}

inline void EmitBootIfNeeded() {
    if (!Enabled() || boot_logged.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    const u32 driver_id = cached_driver_id.load(std::memory_order_relaxed);
    const u8 flags = cached_device_flags.load(std::memory_order_relaxed);
    const bool optimal_astc = (flags & 1) != 0;
    const bool prefer_host = (flags & 2) != 0;
    const char* astc = "Cpu";
    switch (Settings::values.accelerate_astc.GetValue()) {
    case Settings::AstcDecodeMode::Gpu:
        astc = "Gpu";
        break;
    case Settings::AstcDecodeMode::CpuAsynchronous:
        astc = "CpuAsynchronous";
        break;
    default:
        break;
    }
    const char* recompress = "Uncompressed";
    switch (Settings::GetAstcRecompression()) {
    case Settings::AstcRecompression::Bc1:
        recompress = "Bc1";
        break;
    case Settings::AstcRecompression::Bc3:
        recompress = "Bc3";
        break;
    default:
        break;
    }
    LOG_INFO(HW_GPU,
             "OOT diag boot: program={:016x} driver_id={} optimal_astc={} prefer_host={} "
             "accelerate={} recompress={} gpu_accuracy={} ampere={} split_active={} "
             "split_back={}",
             Settings::GetCurrentProgramID(), driver_id, optimal_astc ? 1 : 0,
             prefer_host ? 1 : 0, astc, recompress,
             Settings::values.gpu_accuracy.GetValue() == Settings::GpuAccuracy::High ? "High"
                                                                                     : "Low",
             Tegra::g_nx2_ampere.load(std::memory_order_relaxed) ? 1 : 0,
             VideoCommon::GpuSplit::Get().Active() ? 1 : 0,
             VideoCommon::GpuSplit::Get().BackRunning() ? 1 : 0);
}

inline void LogBoot(u32 driver_id, bool optimal_astc, bool prefer_host) {
    CacheDevice(driver_id, optimal_astc, prefer_host);
    EmitBootIfNeeded();
}

inline void MaybeReport() {
    if (!Enabled()) {
        return;
    }
    EmitBootIfNeeded();
    frames.fetch_add(1, std::memory_order_relaxed);
    static auto window = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - window < std::chrono::seconds{5}) {
        return;
    }
    window = now;
    const u64 cpu = Take(C::UploadCpuConverted);
    const u64 accel = Take(C::UploadAccelerated);
    const u64 raw = Take(C::UploadRaw);
    const u64 skip = Take(C::UploadClearSkip);
    const u64 kept = Take(C::DrefKept);
    const u64 dropped = Take(C::DrefDropped);
    const u64 flush = Take(C::DeferredClearFlush);
    const u64 finish = Take(C::ReadbackFinish);
    const u64 bytes = Take(C::ReadbackBytes);
    const u64 nframes = frames.exchange(0, std::memory_order_relaxed);
    const bool low = Settings::values.gpu_accuracy.GetValue() != Settings::GpuAccuracy::High;
    const bool split_back = VideoCommon::GpuSplit::Get().BackRunning();
    LOG_INFO(HW_GPU,
             "OOT diag 5s: frames={} fps~{} uploads cpu_converted={} accelerated={} raw={} "
             "clear_skip={} | dref kept={} dropped={} | deferred_clear_flush={} | "
             "readback finish={} download_KiB={} | last_rt color={} depth={} | "
             "accuracy={} split_active={} split_back={} | hint: {}",
             nframes, nframes / 5, cpu, accel, raw, skip, kept, dropped, flush, finish,
             bytes / 1024, last_color_format.load(std::memory_order_relaxed),
             last_depth_format.load(std::memory_order_relaxed), low ? "Low" : "High",
             VideoCommon::GpuSplit::Get().Active() ? 1 : 0, split_back ? 1 : 0,
             Hint(cpu, accel, skip, kept, dropped, flush, finish, low, split_back));
}

} // namespace VideoCommon::OotDiag
