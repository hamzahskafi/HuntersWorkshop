// SPDX-License-Identifier: GPL-2.0-or-later
// Hunter's Workshop 0.4.4 beta 18: BB_SHADER_DISASM=<hash>[,<hash>...] writes the game's code of
// those compute shaders into the log at their first dispatch, decoded by the recompiler's own
// decoder, with the dispatch's size. BB_DUMP_SHADERS writes only the shaders being compiled, and
// the pipeline cache and the shader catalog leave most of them uncompiled; LLVM disassembles no
// GCN older than GCN 3 (the PS4's is GCN 2). Hashes as the GPU profile prints them
// (`dispatch cs 00000000a954e79d`).
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <magic_enum/magic_enum.hpp>
#include "common/types.h"
#include "shader_recompiler/frontend/decode.h"

namespace Vulkan::ShaderDisasm {

/// The hashes asked for, each written once (false once written).
inline std::vector<std::pair<u64, bool>>& List() {
    static std::vector<std::pair<u64, bool>> list = [] {
        std::vector<std::pair<u64, bool>> hashes;
        const char* p = std::getenv("BB_SHADER_DISASM");
        while (p && *p) {
            char* end = nullptr;
            const u64 hash = std::strtoull(p, &end, 16);
            if (end == p) {
                ++p; // a separator
                continue;
            }
            hashes.emplace_back(hash, true);
            p = end;
        }
        return hashes;
    }();
    return list;
}

/// Whether the shader's code is still to be written (the first call for it says yes).
inline bool Take(u64 hash) {
    auto& list = List();
    if (list.empty()) {
        return false; // the list never changes size: no lock needed to see it empty
    }
    static std::mutex mutex;
    std::scoped_lock lock{mutex};
    for (auto& [wanted, pending] : list) {
        if (wanted == hash && pending) {
            pending = false;
            return true;
        }
    }
    return false;
}

inline std::string FieldText(const Shader::Gcn::InstOperand& op) {
    using Shader::Gcn::OperandField;
    char text[48];
    switch (op.field) {
    case OperandField::ScalarGPR:
        std::snprintf(text, sizeof(text), "s%u", op.code);
        return text;
    case OperandField::VectorGPR:
        std::snprintf(text, sizeof(text), "v%u", op.code);
        return text;
    case OperandField::SignedConstIntPos:
        return std::to_string(int(op.code) - int(Shader::Gcn::SignedConstIntPosMin) + 1);
    case OperandField::SignedConstIntNeg:
        return std::to_string(-(int(op.code) - int(Shader::Gcn::SignedConstIntNegMin) + 1));
    case OperandField::LiteralConst: {
        float value;
        std::memcpy(&value, &op.code, sizeof(value));
        std::snprintf(text, sizeof(text), "0x%08x (%g)", op.code, value);
        return text;
    }
    case OperandField::ConstZero:
        return "0";
    // Named here: past magic_enum's default range (up to 127).
    case OperandField::ConstFloatPos_0_5:
        return "0.5";
    case OperandField::ConstFloatNeg_0_5:
        return "-0.5";
    case OperandField::ConstFloatPos_1_0:
        return "1.0";
    case OperandField::ConstFloatNeg_1_0:
        return "-1.0";
    case OperandField::ConstFloatPos_2_0:
        return "2.0";
    case OperandField::ConstFloatNeg_2_0:
        return "-2.0";
    case OperandField::ConstFloatPos_4_0:
        return "4.0";
    case OperandField::ConstFloatNeg_4_0:
        return "-4.0";
    case OperandField::VccZ:
        return "vccz";
    case OperandField::ExecZ:
        return "execz";
    case OperandField::Scc:
        return "scc";
    case OperandField::LdsDirect:
        return "lds_direct";
    case OperandField::VccLo:
        return "vcc_lo";
    case OperandField::VccHi:
        return "vcc_hi";
    case OperandField::M0:
        return "m0";
    case OperandField::ExecLo:
        return "exec_lo";
    case OperandField::ExecHi:
        return "exec_hi";
    default:
        std::snprintf(text, sizeof(text), "field%u", static_cast<u32>(op.field));
        return text;
    }
}

inline std::string OperandText(const Shader::Gcn::InstOperand& op) {
    std::string text = FieldText(op);
    if (op.input_modifier.abs) {
        text = "|" + text + "|";
    }
    if (op.input_modifier.neg) {
        text = "-" + text;
    }
    return text;
}

/// The instruction's fields its operands do not show: branch targets, offsets, masks.
inline std::string ControlText(const Shader::Gcn::GcnInst& inst, u32 pc) {
    using Shader::Gcn::InstEncoding;
    char text[96];
    text[0] = '\0';
    const auto& c = inst.control;
    switch (inst.encoding) {
    case InstEncoding::SOPP:
        if (inst.IsUnconditionalBranch() || inst.IsConditionalBranch() || inst.IsFork()) {
            std::snprintf(text, sizeof(text), "-> %04x", inst.BranchTarget(pc));
        } else if (c.sopp.simm) {
            std::snprintf(text, sizeof(text), "simm 0x%04x", u16(c.sopp.simm));
        }
        break;
    case InstEncoding::SOPK:
        std::snprintf(text, sizeof(text), "simm %d", int(c.sopk.simm));
        break;
    case InstEncoding::SMRD:
        if (c.smrd.imm) {
            std::snprintf(text, sizeof(text), "offset %u dwords", u32(c.smrd.offset));
        }
        break;
    case InstEncoding::MUBUF:
        std::snprintf(text, sizeof(text), "offset %u%s%s%s", u32(c.mubuf.offset),
                      c.mubuf.offen ? " offen" : "", c.mubuf.idxen ? " idxen" : "",
                      c.mubuf.glc ? " glc" : "");
        break;
    case InstEncoding::MTBUF:
        std::snprintf(text, sizeof(text), "offset %u%s%s", u32(c.mtbuf.offset),
                      c.mtbuf.offen ? " offen" : "", c.mtbuf.idxen ? " idxen" : "");
        break;
    case InstEncoding::DS:
        std::snprintf(text, sizeof(text), "offset0 %u offset1 %u%s", u32(c.ds.offset0),
                      u32(c.ds.offset1), c.ds.gds ? " gds" : "");
        break;
    case InstEncoding::MIMG:
        std::snprintf(text, sizeof(text), "dmask 0x%x%s", u32(c.mimg.dmask),
                      c.mimg.glc ? " glc" : "");
        break;
    default:
        break;
    }
    return text;
}

/// The program: one line an instruction, its byte offset first.
inline void Print(const char* what, u64 hash, std::span<const u32> code, const char* setup) {
    std::printf("Shader code (BB_SHADER_DISASM): %s %016llx, %zu bytes; %s\n", what,
                static_cast<unsigned long long>(hash), code.size() * 4, setup);
    Shader::Gcn::GcnCodeSlice slice{code.data(), code.data() + code.size()};
    Shader::Gcn::GcnDecodeContext decoder;
    while (!slice.atEnd()) {
        const u32 pc = u32(slice.position() - code.data()) * 4;
        const auto inst = decoder.decodeInstruction(slice);
        std::string line;
        const auto add = [&](const Shader::Gcn::InstOperand& op) {
            if (op.field != Shader::Gcn::OperandField::Undefined) { // a slot this one leaves unused
                line += (line.empty() ? " " : ", ") + OperandText(op);
            }
        };
        for (u32 i = 0; i < inst.dst_count; ++i) {
            add(inst.dst[i]);
        }
        for (u32 i = 0; i < inst.src_count; ++i) {
            add(inst.src[i]);
        }
        const std::string control = ControlText(inst, pc);
        const std::string name{magic_enum::enum_name(inst.opcode)};
        std::printf("  %04x  %s%s%s%s\n", pc, name.c_str(), line.c_str(),
                    control.empty() ? "" : "  ; ", control.c_str());
    }
    std::fflush(stdout);
}

} // namespace Vulkan::ShaderDisasm
