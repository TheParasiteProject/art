/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_CHECK_REFERENCE_MAP_VISITOR_H_
#define ART_RUNTIME_CHECK_REFERENCE_MAP_VISITOR_H_

#include "art_method-inl.h"
#include "base/macros.h"
#include "dex/code_item_accessors-inl.h"
#include "dex/dex_file_types.h"
#include "oat/oat_quick_method_header.h"
#include "oat/stack_map.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"

namespace art HIDDEN {

// Helper class for tests checking that the compiler keeps track of dex registers
// holding references.
class CheckReferenceMapVisitor : public StackVisitor {
 public:
  explicit CheckReferenceMapVisitor(Thread* thread) REQUIRES_SHARED(Locks::mutator_lock_)
      : StackVisitor(thread, nullptr, StackVisitor::StackWalkKind::kIncludeInlinedFrames) {}

  bool VisitFrame() REQUIRES_SHARED(Locks::mutator_lock_) {
    ArtMethod* m = GetMethod();
    if (m->IsCalleeSaveMethod() || m->IsNative()) {
      CHECK_EQ(GetDexPc(), dex::kDexNoIndex);
    }

    // If the method is not compiled, continue the stack walk.
    if (m == nullptr ||
        m->IsNative() ||
        m->IsRuntimeMethod() ||
        IsShadowFrame() ||
        !GetCurrentOatQuickMethodHeader()->IsOptimized()) {
      return true;
    }

    LOG(INFO) << "At " << m->PrettyMethod(false);

    if (m->IsCalleeSaveMethod()) {
      LOG(WARNING) << "no PC for " << m->PrettyMethod();
      return true;
    }

    return false;
  }

  void CheckReferences(int* registers,
                       int number_of_references,
                       uint32_t dex_pc,
                       uint32_t native_pc_offset,
                       bool search_for_valid_stack_map)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    CHECK(GetCurrentOatQuickMethodHeader()->IsOptimized());
    CheckOptimizedMethod(
        registers, number_of_references, dex_pc, native_pc_offset, search_for_valid_stack_map);
  }

 private:
  void CheckOptimizedMethod(int* registers,
                            int number_of_references,
                            uint32_t dex_pc,
                            uint32_t native_pc_offset,
                            bool search_for_valid_stack_map)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    ArtMethod* m = GetMethod();
    CodeInfo code_info(GetCurrentOatQuickMethodHeader());
    StackMap stack_map = code_info.GetStackMapForNativePcOffset(native_pc_offset);
    if (search_for_valid_stack_map && !code_info.GetStackMaskOf(stack_map).IsValid()) {
      for (StackMap map : code_info.GetStackMaps()) {
        if (map.GetDexPc() == dex_pc && code_info.GetStackMaskOf(map).IsValid()) {
          stack_map = map;
          break;
        }
      }
    }
    CodeItemDataAccessor accessor(m->DexInstructionData());
    uint16_t number_of_dex_registers = accessor.RegistersSize();

    if (!Runtime::Current()->IsAsyncDeoptimizeable(GetOuterMethod(), GetCurrentQuickFramePc())) {
      // We can only guarantee dex register info presence for debuggable methods.
      return;
    }

    DexRegisterMap dex_register_map = code_info.GetDexRegisterMapOf(stack_map);
    DCHECK_EQ(dex_register_map.size(), number_of_dex_registers);
    uint32_t register_mask = code_info.GetRegisterMaskOf(stack_map);
    BitMemoryRegion stack_mask = code_info.GetStackMaskOf(stack_map);
    for (int i = 0; i < number_of_references; ++i) {
      int reg = registers[i];
      CHECK_LT(reg, accessor.RegistersSize());
      DexRegisterLocation location = dex_register_map[reg];
      switch (location.GetKind()) {
        case DexRegisterLocation::Kind::kNone:
          // Not set, should not be a reference.
          CHECK(false);
          break;
        case DexRegisterLocation::Kind::kInStack:
          CHECK(stack_mask.IsValid());
          DCHECK_EQ(location.GetValue() % kFrameSlotSize, 0);
          CHECK(stack_mask.LoadBit(location.GetValue() / kFrameSlotSize));
          break;
        case DexRegisterLocation::Kind::kInRegister:
        case DexRegisterLocation::Kind::kInRegisterHigh:
          CHECK_NE(register_mask & (1 << location.GetValue()), 0u);
          break;
        case DexRegisterLocation::Kind::kInFpuRegister:
        case DexRegisterLocation::Kind::kInFpuRegisterHigh:
          // In Fpu register, should not be a reference.
          CHECK(false);
          break;
        case DexRegisterLocation::Kind::kConstant:
          CHECK_EQ(location.GetValue(), 0);
          break;
        default:
          LOG(FATAL) << "Unexpected location kind " << location.GetKind();
      }
    }
  }
};

}  // namespace art

#endif  // ART_RUNTIME_CHECK_REFERENCE_MAP_VISITOR_H_
