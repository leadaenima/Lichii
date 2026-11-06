// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "common/scope_exit.h"
#include "common/settings.h"
#include "video_core/nx2_oot_diag.h"

TEST_CASE("OOT diag is gated on OoT program id", "[video_core][oot][diag]") {
    using namespace VideoCommon::OotDiag;
    const u64 previous = Settings::GetCurrentProgramID();
    SCOPE_EXIT {
        Settings::SetCurrentProgramID(previous);
        Take(C::UploadCpuConverted);
        Take(C::ReadbackBytes);
    };

    Settings::SetCurrentProgramID(0);
    REQUIRE_FALSE(Enabled());
    Add(C::UploadCpuConverted, 7);
    CHECK(Take(C::UploadCpuConverted) == 0);

    Settings::SetCurrentProgramID(0x040074A01BF12000ULL);
    REQUIRE_FALSE(Enabled());

    Settings::SetCurrentProgramID(kOcarinaOfTime);
    REQUIRE(Enabled());
    Add(C::UploadCpuConverted, 3);
    Add(C::ReadbackBytes, 4096);
    CHECK(Take(C::UploadCpuConverted) == 3);
    CHECK(Take(C::ReadbackBytes) == 4096);
    CHECK(Take(C::UploadCpuConverted) == 0);
}

TEST_CASE("OOT diag first-wrong-pass hints name remaining live paths",
          "[video_core][oot][diag]") {
    using namespace VideoCommon::OotDiag;
    CHECK(Hint(0, 0, 0, 0, 0, 0, 0, false, false) == "no-hot-path-this-window");
    CHECK(Hint(12, 0, 0, 0, 0, 0, 0, false, false) == "ASTC-CPU-grain-is-elsewhere");
    CHECK(Hint(12, 4, 0, 0, 0, 0, 0, false, false) == "GPU-ASTC-still-running");
    CHECK(Hint(0, 0, 0, 0, 0, 0, 0, true, true) == "Low+SPLIT-#163-flicker");
    CHECK(Hint(8, 0, 2, 1, 3, 5, 1, true, true) ==
          "ASTC-CPU-grain-is-elsewhere dref-dropped dref-kept clear-skip-uploads "
          "deferred-z-flush cpu-readback-finish Low+SPLIT-#163-flicker");
}
