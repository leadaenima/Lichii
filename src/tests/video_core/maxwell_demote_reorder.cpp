// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ir/opcodes.h"
#include "shader_recompiler/frontend/maxwell/structured_control_flow.h"
#include "shader_recompiler/host_translate_info.h"

namespace {
namespace IR = Shader::IR;

constexpr u64 KilP1 = 0xe33000000001000fULL;
constexpr u64 ExitP1 = 0xe30000000001000fULL;
constexpr u64 ExitPt = 0xe30000000007000fULL;
constexpr u64 BraBase = 0xe24000000000000fULL;

u32 NextAddress(u32 offset) {
    return offset + 8 + (offset % 32 == 24 ? 8 : 0);
}

u64 Bra(u32 predicate, u32 from, u32 to) {
    const s64 relative = static_cast<s64>(to) - static_cast<s64>(from) - 8;
    return BraBase | (u64{predicate} << 16) | ((static_cast<u64>(relative) & 0xffffff) << 20);
}

class DemoteEnvironment final : public Shader::Environment {
public:
    explicit DemoteEnvironment(std::span<const u64> code) {
        stage = Shader::Stage::Fragment;
        u32 address = 0x50;
        for (const u64 instruction : code) {
            memory.emplace(address, instruction);
            address = NextAddress(address);
        }
    }

    u64 ReadInstruction(u32 address) override {
        const auto it = memory.find(address);
        if (it == memory.end()) {
            FAIL("Read outside demote reorder fixture at " << address);
            return 0;
        }
        return it->second;
    }
    u32 ReadCbufValue(u32, u32) override {
        FAIL("Unexpected cbuf read");
        return 0;
    }
    Shader::TextureType ReadTextureType(u32) override {
        return Shader::TextureType::Color2D;
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return false;
    }
    u32 ReadViewportTransformState() override {
        return 1;
    }
    u32 TextureBoundBuffer() const override {
        return 0;
    }
    u32 LocalMemorySize() const override {
        return 0;
    }
    u32 SharedMemorySize() const override {
        return 0;
    }
    std::array<u32, 3> WorkgroupSize() const override {
        return {1, 1, 1};
    }
    bool HasHLEMacroState() const override {
        return false;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}

private:
    std::unordered_map<u32, u64> memory;
};

struct Scan {
    size_t demotes{};
    size_t epilogues{};
    size_t first_demote{std::numeric_limits<size_t>::max()};
    size_t first_epilogue{std::numeric_limits<size_t>::max()};
    size_t last_epilogue{std::numeric_limits<size_t>::max()};
    bool combined_demote_before_last_epilogue{};
};

bool BlockHas(const IR::Block& block, IR::Opcode opcode) {
    for (const IR::Inst& inst : block.Instructions()) {
        if (inst.GetOpcode() == opcode) {
            return true;
        }
    }
    return false;
}

Scan Analyze(const IR::AbstractSyntaxList& asl) {
    using Type = IR::AbstractSyntaxNode::Type;
    Scan scan;
    for (size_t index = 0; index < asl.size(); ++index) {
        const auto& node = asl[index];
        if (node.type != Type::Block) {
            continue;
        }
        if (BlockHas(*node.data.block, IR::Opcode::DemoteToHelperInvocation)) {
            if (scan.first_demote == std::numeric_limits<size_t>::max()) {
                scan.first_demote = index;
            }
            ++scan.demotes;
        }
        if (BlockHas(*node.data.block, IR::Opcode::Epilogue)) {
            if (scan.first_epilogue == std::numeric_limits<size_t>::max()) {
                scan.first_epilogue = index;
            }
            scan.last_epilogue = index;
            ++scan.epilogues;
        }
    }
    if (scan.last_epilogue >= 3 && scan.demotes != 0) {
        const auto& demote_if = asl[scan.last_epilogue - 3];
        const auto& demote_block = asl[scan.last_epilogue - 2];
        const auto& demote_endif = asl[scan.last_epilogue - 1];
        scan.combined_demote_before_last_epilogue =
            demote_if.type == Type::If && demote_block.type == Type::Block &&
            demote_endif.type == Type::EndIf &&
            BlockHas(*demote_block.data.block, IR::Opcode::DemoteToHelperInvocation);
    }
    return scan;
}

Scan Translate(std::span<const u64> code, bool reorder) {
    DemoteEnvironment env{code};
    Shader::ObjectPool<Shader::Maxwell::Flow::Block> flow_pool;
    Shader::Maxwell::Flow::CFG cfg{env, flow_pool, Shader::Maxwell::Location{0x50}};
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool;
    Shader::HostTranslateInfo host;
    host.needs_demote_reorder = reorder;
    const auto asl =
        Shader::Maxwell::BuildASL(inst_pool, block_pool, env, cfg, host);
    const Scan scan = Analyze(asl);
    REQUIRE(scan.demotes >= 1);
    REQUIRE(scan.epilogues >= 1);
    REQUIRE(scan.first_demote != std::numeric_limits<size_t>::max());
    REQUIRE(scan.first_epilogue != std::numeric_limits<size_t>::max());
    return scan;
}

std::vector<u64> NestedKillAndReturn() {
    const u32 kil = 0x50;
    const u32 bra = NextAddress(kil);
    const u32 nested_exit = NextAddress(bra);
    const u32 final_exit = NextAddress(nested_exit);
    // Ampere `@P1 KILL` then `@P0 EXIT P1` lowers to a skip around a predicated EXIT:
    // if (P0) { if (P1) return; }.
    return {KilP1, Bra(8, bra, final_exit), ExitP1, ExitPt};
}
} // namespace

TEST_CASE("AMD demote reorder keeps a nested early return in front of the demote",
          "[video_core][shader]") {
    const auto code = NestedKillAndReturn();
    const Scan without = Translate(code, false);
    REQUIRE(without.epilogues >= 2);
    REQUIRE(without.first_demote < without.first_epilogue);
    REQUIRE_FALSE(without.combined_demote_before_last_epilogue);

    const Scan with = Translate(code, true);
    REQUIRE(with.epilogues >= 2);
    REQUIRE(with.first_demote < with.first_epilogue);
    REQUIRE_FALSE(with.combined_demote_before_last_epilogue);
}

TEST_CASE("AMD demote reorder still moves a single-return demote to the epilogue",
          "[video_core][shader]") {
    const std::vector<u64> code{KilP1, ExitPt};
    const Scan without = Translate(code, false);
    REQUIRE(without.epilogues == 1);
    REQUIRE_FALSE(without.combined_demote_before_last_epilogue);

    const Scan with = Translate(code, true);
    REQUIRE(with.epilogues == 1);
    REQUIRE(with.combined_demote_before_last_epilogue);
    REQUIRE(with.first_demote < with.first_epilogue);
}
