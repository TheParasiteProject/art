/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "disassembler_arm64.h"

#include <inttypes.h>

#include <regex>

#include <sstream>

#include "android-base/logging.h"
#include "android-base/stringprintf.h"

using android::base::StringPrintf;

using namespace vixl::aarch64;  // NOLINT(build/namespaces)

namespace art {
namespace arm64 {

// This enumeration should mirror the declarations in
// runtime/arch/arm64/registers_arm64.h. We do not include that file to
// avoid a dependency on libart.
enum {
  TR  = 19,
  IP0 = 16,
  IP1 = 17,
  FP  = 29,
  LR  = 30
};

void CustomDisassembler::AppendRegisterNameToOutput(const Instruction* instr,
                                                    const CPURegister& reg) {
  USE(instr);
  if (reg.IsRegister() && reg.Is64Bits()) {
    if (reg.GetCode() == TR) {
      AppendToOutput("tr");
      return;
    } else if (reg.GetCode() == LR) {
      AppendToOutput("lr");
      return;
    }
    // Fall through.
  }
  // Print other register names as usual.
  Disassembler::AppendRegisterNameToOutput(instr, reg);
}

void CustomDisassembler::AppendCodeRelativeAddressToOutput(const Instruction* instr,
                                                           const void* addr) {
  USE(instr);
  int64_t rel_addr = CodeRelativeAddress(addr);
  if (rel_addr >= 0) {
    AppendToOutput("(addr 0x%08" PRIx64 ")", rel_addr);
  } else {
    AppendToOutput("(addr -0x%08" PRIx64 ")", -rel_addr);
  }
}

void CustomDisassembler::Visit(vixl::aarch64::Metadata* metadata, const Instruction* instr) {
  vixl::aarch64::Disassembler::Visit(metadata, instr);
  const std::string& form = (*metadata)["form"];

  // These regexs are long, but it is an attempt to match the mapping entry keys in the
  // #define DEFAULT_FORM_TO_VISITOR_MAP(VISITORCLASS) in the file
  // external/vixl/src/aarch64/decoder-visitor-map-aarch64.h
  // for the ::VisitLoadLiteralInstr, ::VisitLoadStoreUnsignedOffset or ::VisitUnconditionalBranch
  // function addresess key values.
  // N.B. the mapping are many to one.
  if (std::regex_match(form, std::regex("(ldrsw|ldr|prfm)_(32|64|d|b|h|q|s)_loadlit"))) {
    VisitLoadLiteralInstr(instr);
    return;
  }

  if (std::regex_match(form, std::regex(
      "(ldrb|ldrh|ldrsb|ldrsh|ldrsw|ldr|prfm|strb|strh|str)_(32|64|d|b|h|q|s)_ldst_pos"))) {
    VisitLoadStoreUnsignedOffsetInstr(instr);
    return;
  }

  if (std::regex_match(form, std::regex("(bl|b)_only_branch_imm"))) {
    VisitUnconditionalBranchInstr(instr);
    return;
  }
}

void CustomDisassembler::VisitLoadLiteralInstr(const Instruction* instr) {
  if (!read_literals_) {
    return;
  }

  // Get address of literal. Bail if not within expected buffer range to
  // avoid trying to fetch invalid literals (we can encounter this when
  // interpreting raw data as instructions).
  void* data_address = instr->GetLiteralAddress<void*>();

  if (data_address < base_address_ || data_address >= end_address_) {
    AppendToOutput(" (?)");
    return;
  }

  // Output information on literal.
  Instr op = instr->Mask(LoadLiteralMask);
  switch (op) {
    case LDR_w_lit:
    case LDR_x_lit:
    case LDRSW_x_lit: {
      int64_t data = op == LDR_x_lit ? *reinterpret_cast<int64_t*>(data_address)
                                     : *reinterpret_cast<int32_t*>(data_address);
      AppendToOutput(" (0x%" PRIx64 " / %" PRId64 ")", data, data);
      break;
    }
    case LDR_s_lit:
    case LDR_d_lit: {
      double data = (op == LDR_s_lit) ? *reinterpret_cast<float*>(data_address)
                                      : *reinterpret_cast<double*>(data_address);
      AppendToOutput(" (%g)", data);
      break;
    }
    default:
      break;
  }
}

void CustomDisassembler::VisitLoadStoreUnsignedOffsetInstr(const Instruction* instr) {
  if (instr->GetRn() == TR) {
    AppendThreadOfsetName(instr);
  }
}

void CustomDisassembler::VisitUnconditionalBranchInstr(const Instruction* instr) {
  if (instr->Mask(UnconditionalBranchMask) == BL) {
    const Instruction* target = instr->GetImmPCOffsetTarget();
    if (target >= base_address_ &&
        target < end_address_ &&
        target->Mask(LoadStoreMask) == LDR_x &&
        target->GetRn() == TR &&
        target->GetRt() == IP0 &&
        target->GetNextInstruction() < end_address_ &&
        target->GetNextInstruction()->Mask(UnconditionalBranchToRegisterMask) == BR &&
        target->GetNextInstruction()->GetRn() == IP0) {
      AppendThreadOfsetName(target);
    }
  }
}

void CustomDisassembler::AppendThreadOfsetName(const vixl::aarch64::Instruction* instr) {
  int64_t offset = instr->GetImmLSUnsigned() << instr->GetSizeLS();
  std::ostringstream tmp_stream;
  options_->thread_offset_name_function_(tmp_stream, static_cast<uint32_t>(offset));
  AppendToOutput(" ; %s", tmp_stream.str().c_str());
}

size_t DisassemblerArm64::Dump(std::ostream& os, const uint8_t* begin) {
  const Instruction* instr = reinterpret_cast<const Instruction*>(begin);
  decoder.Decode(instr);
    os << FormatInstructionPointer(begin)
     << StringPrintf(": %08x\t%s\n", instr->GetInstructionBits(), disasm.GetOutput());
  return kInstructionSize;
}

void DisassemblerArm64::Dump(std::ostream& os, const uint8_t* begin, const uint8_t* end) {
  for (const uint8_t* cur = begin; cur < end; cur += kInstructionSize) {
    Dump(os, cur);
  }
}

}  // namespace arm64
}  // namespace art
