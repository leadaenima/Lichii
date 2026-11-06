// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/opcodes.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/object_pool.h"

namespace {
constexpr u32 OpGroupNonUniformShuffle = 345;
constexpr u32 OpGroupNonUniformBroadcast = 337;

u32 CountOpcode(const std::vector<u32>& code, u32 opcode) {
    u32 count = 0;
    for (size_t offset = 5; offset < code.size();) {
        const u32 word_count = code[offset] >> 16;
        if (word_count == 0) {
            break;
        }
        if ((code[offset] & 0xffffU) == opcode) {
            ++count;
        }
        offset += word_count;
    }
    return count;
}

std::vector<u32> EmitShuffle(Shader::Stage stage, bool avoid_shuffle) {
    Shader::ObjectPool<Shader::IR::Inst> pool;
    Shader::IR::Block block{pool};
    Shader::IR::Program program;
    program.stage = stage;
    program.workgroup_size = {32, 1, 1};
    program.blocks = {&block};
    program.post_order_blocks = {&block};
    program.syntax_list = {{{.block = &block}, Shader::IR::AbstractSyntaxNode::Type::Block},
                           {{}, Shader::IR::AbstractSyntaxNode::Type::Return}};
    program.info.uses_subgroup_shuffles = true;
    program.info.uses_subgroup_invocation_id = true;
    program.info.used_storage_buffer_types = Shader::IR::Type::U32;
    program.info.storage_buffers_descriptors = {{0, 0, 1, true}};
    program.info.uses_local_invocation_id = true;

    Shader::IR::IREmitter ir{block};
    const auto result =
        ir.ShuffleIndex(ir.Imm32(0x12345678U), ir.LaneId(), ir.Imm32(31), ir.Imm32(0));
    const auto offset = ir.ShiftLeftLogical(ir.LocalInvocationIdX(), ir.Imm32(2));
    block.AppendNewInst(Shader::IR::Opcode::WriteStorage32, {ir.Imm32(0), offset, result});

    Shader::Profile profile{};
    profile.supported_spirv = 0x00010500;
    profile.support_vote = true;
    profile.supported_subgroup_stages = 1U << static_cast<u32>(stage);
    profile.max_subgroup_size = 64;
    profile.has_broken_spirv_dynamic_subgroup_shuffle = avoid_shuffle;
    return Shader::Backend::SPIRV::EmitSPIRV(profile, program);
}
} // namespace

TEST_CASE("AMDVLK keeps Shuffle on compute and uses a 32-lane Broadcast loop on fragment",
          "[video_core][spirv][subgroup]") {
    const auto compute_default = EmitShuffle(Shader::Stage::Compute, false);
    REQUIRE(CountOpcode(compute_default, OpGroupNonUniformShuffle) >= 1);

    const auto compute_amd = EmitShuffle(Shader::Stage::Compute, true);
    REQUIRE(CountOpcode(compute_amd, OpGroupNonUniformShuffle) >= 1);
    REQUIRE(CountOpcode(compute_amd, OpGroupNonUniformShuffle) ==
            CountOpcode(compute_default, OpGroupNonUniformShuffle));

    const auto fragment_amd = EmitShuffle(Shader::Stage::Fragment, true);
    REQUIRE(CountOpcode(fragment_amd, OpGroupNonUniformShuffle) == 0);
    const u32 broadcasts = CountOpcode(fragment_amd, OpGroupNonUniformBroadcast);
    REQUIRE(broadcasts >= 1);
    REQUIRE(broadcasts <= 32);
}
