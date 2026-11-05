// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>
#include <initializer_list>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/backend/bindings.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/abstract_syntax_list.h"
#include "shader_recompiler/frontend/ir/basic_block.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/object_pool.h"

namespace {
constexpr u32 OpCapability = 17;
constexpr u32 OpConstant = 43;
constexpr u32 OpConstantComposite = 44;
constexpr u32 OpDecorate = 71;
constexpr u32 OpKill = 252;
constexpr u32 OpDemoteToHelperInvocation = 5380;
constexpr u32 CapabilityDemoteToHelperInvocation = 5379;
constexpr u32 DecorationBuiltIn = 11;
constexpr u32 BuiltInPrimitiveId = 7;
constexpr u32 BuiltInBaseVertex = 4424;
constexpr u32 BuiltInBaseInstance = 4425;
constexpr u32 BuiltInDrawIndex = 4426;
constexpr u32 CapabilityGeometry = 2;
constexpr u32 CapabilityDrawParameters = 4427;

std::vector<u32> Emit(Shader::Stage stage, bool support_vertex_instance_id,
                      std::initializer_list<Shader::IR::Attribute> loads) {
    Shader::ObjectPool<Shader::IR::Inst> inst_pool;
    Shader::IR::Block block{inst_pool};
    Shader::IR::Program program;
    program.stage = stage;
    program.blocks.push_back(&block);
    program.post_order_blocks.push_back(&block);
    Shader::IR::AbstractSyntaxNode block_node{};
    block_node.type = Shader::IR::AbstractSyntaxNode::Type::Block;
    block_node.data.block = &block;
    Shader::IR::AbstractSyntaxNode return_node{};
    return_node.type = Shader::IR::AbstractSyntaxNode::Type::Return;
    program.syntax_list = {block_node, return_node};
    for (const Shader::IR::Attribute attribute : loads) {
        program.info.loads.Set(attribute);
    }
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    profile.support_vertex_instance_id = support_vertex_instance_id;
    return Shader::Backend::SPIRV::EmitSPIRV(profile, program);
}

bool HasCapability(const std::vector<u32>& code, u32 capability) {
    for (size_t offset = 5; offset < code.size();) {
        const u32 word_count = code[offset] >> 16;
        if (word_count == 0) {
            break;
        }
        if ((code[offset] & 0xffff) == OpCapability && code[offset + 1] == capability) {
            return true;
        }
        offset += word_count;
    }
    return false;
}

bool HasBuiltIn(const std::vector<u32>& code, u32 builtin) {
    for (size_t offset = 5; offset < code.size();) {
        const u32 word_count = code[offset] >> 16;
        if (word_count == 0) {
            break;
        }
        if ((code[offset] & 0xffff) == OpDecorate && word_count >= 4 &&
            code[offset + 2] == DecorationBuiltIn && code[offset + 3] == builtin) {
            return true;
        }
        offset += word_count;
    }
    return false;
}

TEST_CASE("SPIR-V draw parameter built-ins declare DrawParameters",
          "[video_core][spirv][draw_parameters]") {
    using Shader::IR::Attribute;
    struct Case {
        bool support_vertex_instance_id;
        Attribute load;
        u32 builtin;
    };
    for (const Case test : {
             Case{true, Attribute::BaseInstance, BuiltInBaseInstance},
             Case{true, Attribute::BaseVertex, BuiltInBaseVertex},
             Case{true, Attribute::DrawID, BuiltInDrawIndex},
             Case{false, Attribute::BaseInstance, BuiltInBaseInstance},
             Case{false, Attribute::BaseVertex, BuiltInBaseVertex},
             Case{false, Attribute::DrawID, BuiltInDrawIndex},
             Case{false, Attribute::InstanceId, BuiltInBaseInstance},
             Case{false, Attribute::VertexId, BuiltInBaseVertex},
         }) {
        const std::vector<u32> code =
            Emit(Shader::Stage::VertexB, test.support_vertex_instance_id, {test.load});
        REQUIRE(HasBuiltIn(code, test.builtin));
        REQUIRE(HasCapability(code, CapabilityDrawParameters));
    }
}

TEST_CASE("SPIR-V vertex and instance ids alone do not need DrawParameters",
          "[video_core][spirv][draw_parameters]") {
    using Shader::IR::Attribute;
    const std::vector<u32> code =
        Emit(Shader::Stage::VertexB, true, {Attribute::InstanceId, Attribute::VertexId});
    REQUIRE_FALSE(HasBuiltIn(code, BuiltInBaseInstance));
    REQUIRE_FALSE(HasBuiltIn(code, BuiltInBaseVertex));
    REQUIRE_FALSE(HasCapability(code, CapabilityDrawParameters));
}

u32 CountOpcode(const std::vector<u32>& code, u32 opcode) {
    u32 count = 0;
    for (size_t offset = 5; offset < code.size();) {
        const u32 word_count = code[offset] >> 16;
        if (word_count == 0) {
            break;
        }
        if ((code[offset] & 0xffff) == opcode) {
            ++count;
        }
        offset += word_count;
    }
    return count;
}

u32 F32Bits(float value) {
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool HasVec4Constant(const std::vector<u32>& code, float x, float y, float z, float w) {
    std::unordered_map<u32, u32> floats;
    for (size_t offset = 5; offset < code.size();) {
        const u32 word_count = code[offset] >> 16;
        if (word_count == 0) {
            break;
        }
        if ((code[offset] & 0xffff) == OpConstant && word_count >= 4) {
            floats[code[offset + 2]] = code[offset + 3];
        }
        offset += word_count;
    }
    const u32 want[4]{F32Bits(x), F32Bits(y), F32Bits(z), F32Bits(w)};
    for (size_t offset = 5; offset < code.size();) {
        const u32 word_count = code[offset] >> 16;
        if (word_count == 0) {
            break;
        }
        if ((code[offset] & 0xffff) == OpConstantComposite && word_count >= 7) {
            bool match = true;
            for (u32 i = 0; i < 4; ++i) {
                const auto found = floats.find(code[offset + 3 + i]);
                if (found == floats.end() || found->second != want[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        offset += word_count;
    }
    return false;
}

std::vector<u32> EmitFragment(const Shader::Profile& profile, const Shader::RuntimeInfo& runtime) {
    Shader::ObjectPool<Shader::IR::Inst> inst_pool;
    Shader::IR::Block block{inst_pool};
    Shader::IR::Program program;
    program.stage = Shader::Stage::Fragment;
    program.blocks.push_back(&block);
    program.post_order_blocks.push_back(&block);
    Shader::IR::AbstractSyntaxNode block_node{};
    block_node.type = Shader::IR::AbstractSyntaxNode::Type::Block;
    block_node.data.block = &block;
    Shader::IR::AbstractSyntaxNode return_node{};
    return_node.type = Shader::IR::AbstractSyntaxNode::Type::Return;
    program.syntax_list = {block_node, return_node};
    program.info.stores_frag_color[0] = true;
    Shader::IR::IREmitter ir{block};
    ir.Prologue();
    ir.Epilogue();
    Shader::Backend::Bindings bindings;
    return Shader::Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
}

TEST_CASE("SPIR-V fragment PrimitiveId declares Geometry", "[video_core][spirv][primitive_id]") {
    using Shader::IR::Attribute;
    const std::vector<u32> with_id =
        Emit(Shader::Stage::Fragment, false, {Attribute::PrimitiveId});
    REQUIRE(HasBuiltIn(with_id, BuiltInPrimitiveId));
    REQUIRE(HasCapability(with_id, CapabilityGeometry));

    const std::vector<u32> without_id = Emit(Shader::Stage::Fragment, false, {});
    REQUIRE_FALSE(HasBuiltIn(without_id, BuiltInPrimitiveId));
    REQUIRE_FALSE(HasCapability(without_id, CapabilityGeometry));
}

TEST_CASE("SPIR-V dual-source prologue defaults unused src1 to zero",
          "[video_core][spirv][dual_source]") {
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    Shader::RuntimeInfo runtime{};
    runtime.dual_source_blend = true;
    const std::vector<u32> code = EmitFragment(profile, runtime);
    REQUIRE(HasVec4Constant(code, 0.0f, 0.0f, 0.0f, 0.0f));
    REQUIRE_FALSE(HasVec4Constant(code, 0.0f, 0.0f, 0.0f, 1.0f));
}

TEST_CASE("SPIR-V alpha test demotes instead of killing when the device supports it",
          "[video_core][spirv][alpha_test]") {
    Shader::RuntimeInfo runtime{};
    runtime.alpha_test_func = Shader::CompareFunction::Less;
    runtime.alpha_test_reference = 0.5f;

    Shader::Profile with_demote{};
    with_demote.supported_spirv = 0x00010600;
    with_demote.support_demote_to_helper_invocation = true;
    const std::vector<u32> demote = EmitFragment(with_demote, runtime);
    REQUIRE(HasCapability(demote, CapabilityDemoteToHelperInvocation));
    REQUIRE(CountOpcode(demote, OpDemoteToHelperInvocation) >= 1);
    REQUIRE(CountOpcode(demote, OpKill) == 0);

    Shader::Profile without_demote{};
    without_demote.supported_spirv = 0x00010300;
    without_demote.support_demote_to_helper_invocation = false;
    const std::vector<u32> kill = EmitFragment(without_demote, runtime);
    REQUIRE_FALSE(HasCapability(kill, CapabilityDemoteToHelperInvocation));
    REQUIRE(CountOpcode(kill, OpKill) >= 1);
    REQUIRE(CountOpcode(kill, OpDemoteToHelperInvocation) == 0);
}
} // Anonymous namespace
