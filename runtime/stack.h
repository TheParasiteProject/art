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

#ifndef ART_RUNTIME_STACK_H_
#define ART_RUNTIME_STACK_H_

#include <stdint.h>

#include <optional>
#include <string>

#include "base/locks.h"
#include "base/macros.h"
#include "deoptimization_kind.h"
#include "oat/stack_map.h"
#include "obj_ptr.h"
#include "quick/quick_method_frame_info.h"

namespace art HIDDEN {

namespace mirror {
class Object;
}  // namespace mirror

class ArtMethod;
class Context;
class HandleScope;
class OatQuickMethodHeader;
class ShadowFrame;
class Thread;
union JValue;

// The kind of vreg being accessed in calls to Set/GetVReg.
enum VRegKind {
  kReferenceVReg,
  kIntVReg,
  kFloatVReg,
  kLongLoVReg,
  kLongHiVReg,
  kDoubleLoVReg,
  kDoubleHiVReg,
  kConstant,
  kImpreciseConstant,
  kUndefined,
};
std::ostream& operator<<(std::ostream& os, VRegKind rhs);

/*
 * Our current stack layout.
 * The Dalvik registers come first, followed by the
 * Method*, followed by other special temporaries if any, followed by
 * regular compiler temporary. As of now we only have the Method* as
 * as a special compiler temporary.
 * A compiler temporary can be thought of as a virtual register that
 * does not exist in the dex but holds intermediate values to help
 * optimizations and code generation. A special compiler temporary is
 * one whose location in frame is well known while non-special ones
 * do not have a requirement on location in frame as long as code
 * generator itself knows how to access them.
 *
 * TODO: Update this documentation?
 *
 *     +-------------------------------+
 *     | IN[ins-1]                     |  {Note: resides in caller's frame}
 *     |       .                       |
 *     | IN[0]                         |
 *     | caller's ArtMethod            |  ... ArtMethod*
 *     +===============================+  {Note: start of callee's frame}
 *     | core callee-save spill        |  {variable sized}
 *     +-------------------------------+
 *     | fp callee-save spill          |
 *     +-------------------------------+
 *     | filler word                   |  {For compatibility, if V[locals-1] used as wide
 *     +-------------------------------+
 *     | V[locals-1]                   |
 *     | V[locals-2]                   |
 *     |      .                        |
 *     |      .                        |  ... (reg == 2)
 *     | V[1]                          |  ... (reg == 1)
 *     | V[0]                          |  ... (reg == 0) <---- "locals_start"
 *     +-------------------------------+
 *     | stack alignment padding       |  {0 to (kStackAlignWords-1) of padding}
 *     +-------------------------------+
 *     | Compiler temp region          |  ... (reg >= max_num_special_temps)
 *     |      .                        |
 *     |      .                        |
 *     | V[max_num_special_temps + 1]  |
 *     | V[max_num_special_temps + 0]  |
 *     +-------------------------------+
 *     | OUT[outs-1]                   |
 *     | OUT[outs-2]                   |
 *     |       .                       |
 *     | OUT[0]                        |
 *     | ArtMethod*                    |  ... (reg == num_total_code_regs == special_temp_value) <<== sp, 16-byte aligned
 *     +===============================+
 */

class StackVisitor {
 public:
  // This enum defines a flag to control whether inlined frames are included
  // when walking the stack.
  enum class StackWalkKind {
    kIncludeInlinedFrames,
    kSkipInlinedFrames,
  };

 protected:
  EXPORT StackVisitor(Thread* thread,
                      Context* context,
                      StackWalkKind walk_kind,
                      bool check_suspended = true);

  bool GetRegisterIfAccessible(uint32_t reg, DexRegisterLocation::Kind kind, uint32_t* val) const
      REQUIRES_SHARED(Locks::mutator_lock_);

 public:
  virtual ~StackVisitor() {}
  StackVisitor(const StackVisitor&) = default;
  StackVisitor(StackVisitor&&) = default;

  // Return 'true' if we should continue to visit more frames, 'false' to stop.
  virtual bool VisitFrame() REQUIRES_SHARED(Locks::mutator_lock_) = 0;

  enum class EXPORT CountTransitions {
    kYes,
    kNo,
  };

  template <CountTransitions kCount = CountTransitions::kYes>
  EXPORT void WalkStack(bool include_transitions = false) REQUIRES_SHARED(Locks::mutator_lock_);

  // Convenience helper function to walk the stack with a lambda as a visitor.
  template <CountTransitions kCountTransitions = CountTransitions::kYes,
            typename T>
  ALWAYS_INLINE static void WalkStack(const T& fn,
                                      Thread* thread,
                                      Context* context,
                                      StackWalkKind walk_kind,
                                      bool check_suspended = true,
                                      bool include_transitions = false)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    class LambdaStackVisitor : public StackVisitor {
     public:
      LambdaStackVisitor(const T& fn,
                         Thread* thread,
                         Context* context,
                         StackWalkKind walk_kind,
                         bool check_suspended = true)
          : StackVisitor(thread, context, walk_kind, check_suspended), fn_(fn) {}

      bool VisitFrame() override REQUIRES_SHARED(Locks::mutator_lock_) {
        return fn_(this);
      }

     private:
      T fn_;
    };
    LambdaStackVisitor visitor(fn, thread, context, walk_kind, check_suspended);
    visitor.template WalkStack<kCountTransitions>(include_transitions);
  }

  Thread* GetThread() const {
    return thread_;
  }

  EXPORT ArtMethod* GetMethod() const REQUIRES_SHARED(Locks::mutator_lock_);

  // Sets this stack frame's method pointer. This requires a full lock of the MutatorLock. This
  // doesn't work with inlined methods.
  EXPORT void SetMethod(ArtMethod* method) REQUIRES(Locks::mutator_lock_);

  ArtMethod* GetOuterMethod() const {
    return *GetCurrentQuickFrame();
  }

  bool IsShadowFrame() const {
    return cur_shadow_frame_ != nullptr;
  }

  EXPORT uint32_t GetDexPc(bool abort_on_failure = true) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Returns a vector of the inlined dex pcs, in order from outermost to innermost but it replaces
  // the innermost one with `handler_dex_pc`. In essence, (outermost dex pc, mid dex pc #1, ..., mid
  // dex pc #n-1, `handler_dex_pc`).
  std::vector<uint32_t> ComputeDexPcList(uint32_t handler_dex_pc) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  EXPORT ObjPtr<mirror::Object> GetThisObject() const REQUIRES_SHARED(Locks::mutator_lock_);

  EXPORT size_t GetNativePcOffset() const REQUIRES_SHARED(Locks::mutator_lock_);

  // Returns the height of the stack in the managed stack frames, including transitions.
  size_t GetFrameHeight() REQUIRES_SHARED(Locks::mutator_lock_) {
    return GetNumFrames() - cur_depth_ - 1;
  }

  // Returns a frame ID for JDWP use, starting from 1.
  size_t GetFrameId() REQUIRES_SHARED(Locks::mutator_lock_) {
    return GetFrameHeight() + 1;
  }

  size_t GetNumFrames() REQUIRES_SHARED(Locks::mutator_lock_) {
    if (num_frames_ == 0) {
      num_frames_ = ComputeNumFrames(thread_, walk_kind_);
    }
    return num_frames_;
  }

  size_t GetFrameDepth() const REQUIRES_SHARED(Locks::mutator_lock_) {
    return cur_depth_;
  }

  // Get the method and dex pc immediately after the one that's currently being visited.
  bool GetNextMethodAndDexPc(ArtMethod** next_method, uint32_t* next_dex_pc)
      REQUIRES_SHARED(Locks::mutator_lock_);

  EXPORT bool GetVReg(
      ArtMethod* m,
      uint16_t vreg,
      VRegKind kind,
      uint32_t* val,
      std::optional<DexRegisterLocation> location = std::optional<DexRegisterLocation>(),
      bool need_full_register_list = false) const REQUIRES_SHARED(Locks::mutator_lock_);

  EXPORT bool GetVRegPair(ArtMethod* m,
                          uint16_t vreg,
                          VRegKind kind_lo,
                          VRegKind kind_hi,
                          uint64_t* val) const REQUIRES_SHARED(Locks::mutator_lock_);

  // Values will be set in debugger shadow frames. Debugger will make sure deoptimization
  // is triggered to make the values effective.
  EXPORT bool SetVReg(ArtMethod* m, uint16_t vreg, uint32_t new_value, VRegKind kind)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Values will be set in debugger shadow frames. Debugger will make sure deoptimization
  // is triggered to make the values effective.
  EXPORT bool SetVRegReference(ArtMethod* m, uint16_t vreg, ObjPtr<mirror::Object> new_value)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Values will be set in debugger shadow frames. Debugger will make sure deoptimization
  // is triggered to make the values effective.
  EXPORT bool SetVRegPair(ArtMethod* m,
                          uint16_t vreg,
                          uint64_t new_value,
                          VRegKind kind_lo,
                          VRegKind kind_hi) REQUIRES_SHARED(Locks::mutator_lock_);

  uintptr_t* GetGPRAddress(uint32_t reg) const;

  uintptr_t GetReturnPc() const REQUIRES_SHARED(Locks::mutator_lock_);
  uintptr_t GetReturnPcAddr() const REQUIRES_SHARED(Locks::mutator_lock_);

  void SetReturnPc(uintptr_t new_ret_pc) REQUIRES_SHARED(Locks::mutator_lock_);

  bool IsInInlinedFrame() const {
    return !current_inline_frames_.empty();
  }

  size_t InlineDepth() const { return current_inline_frames_.size(); }

  InlineInfo GetCurrentInlinedFrame() const {
    return current_inline_frames_.back();
  }

  const BitTableRange<InlineInfo>& GetCurrentInlinedFrames() const {
    return current_inline_frames_;
  }

  uintptr_t GetCurrentQuickFramePc() const {
    return cur_quick_frame_pc_;
  }

  ArtMethod** GetCurrentQuickFrame() const {
    return cur_quick_frame_;
  }

  ShadowFrame* GetCurrentShadowFrame() const {
    return cur_shadow_frame_;
  }

  std::string DescribeLocation() const REQUIRES_SHARED(Locks::mutator_lock_);

  EXPORT static size_t ComputeNumFrames(Thread* thread, StackWalkKind walk_kind)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static void DescribeStack(Thread* thread) REQUIRES_SHARED(Locks::mutator_lock_);

  const OatQuickMethodHeader* GetCurrentOatQuickMethodHeader() const {
    return cur_oat_quick_method_header_;
  }

  QuickMethodFrameInfo GetCurrentQuickFrameInfo() const REQUIRES_SHARED(Locks::mutator_lock_);

  void SetShouldDeoptimizeFlag(DeoptimizeFlagValue value) REQUIRES_SHARED(Locks::mutator_lock_) {
    uint8_t* should_deoptimize_addr = GetShouldDeoptimizeFlagAddr();
    *should_deoptimize_addr = *should_deoptimize_addr | static_cast<uint8_t>(value);
  };

  void UnsetShouldDeoptimizeFlag(DeoptimizeFlagValue value) REQUIRES_SHARED(Locks::mutator_lock_) {
    uint8_t* should_deoptimize_addr = GetShouldDeoptimizeFlagAddr();
    *should_deoptimize_addr = *should_deoptimize_addr & ~static_cast<uint8_t>(value);
  };

  uint8_t GetShouldDeoptimizeFlag() const REQUIRES_SHARED(Locks::mutator_lock_) {
    return *GetShouldDeoptimizeFlagAddr();
  }

  bool ShouldForceDeoptForRedefinition() const REQUIRES_SHARED(Locks::mutator_lock_) {
    uint8_t should_deopt_flag = GetShouldDeoptimizeFlag();
    return (should_deopt_flag &
            static_cast<uint8_t>(DeoptimizeFlagValue::kForceDeoptForRedefinition)) != 0;
  }

  // Return the number of dex register in the map from the outermost frame to the number of inlined
  // frames indicated by `depth`. If `depth` is 0, grab just the registers from the outermost level.
  // If it is greater than 0, grab as many inline frames as `depth` indicates.
  size_t GetNumberOfRegisters(CodeInfo* code_info, int depth) const;

 private:
  // Private constructor known in the case that num_frames_ has already been computed.
  EXPORT StackVisitor(Thread* thread,
                      Context* context,
                      StackWalkKind walk_kind,
                      size_t num_frames,
                      bool check_suspended = true) REQUIRES_SHARED(Locks::mutator_lock_);

  bool IsAccessibleRegister(uint32_t reg, bool is_float) const {
    return is_float ? IsAccessibleFPR(reg) : IsAccessibleGPR(reg);
  }
  uintptr_t GetRegister(uint32_t reg, bool is_float) const {
    DCHECK(IsAccessibleRegister(reg, is_float));
    return is_float ? GetFPR(reg) : GetGPR(reg);
  }

  bool IsAccessibleGPR(uint32_t reg) const;
  uintptr_t GetGPR(uint32_t reg) const;

  bool IsAccessibleFPR(uint32_t reg) const;
  uintptr_t GetFPR(uint32_t reg) const;

  bool GetVRegFromDebuggerShadowFrame(uint16_t vreg, VRegKind kind, uint32_t* val) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool GetVRegFromOptimizedCode(ArtMethod* m,
                                uint16_t vreg,
                                VRegKind kind,
                                uint32_t* val,
                                bool need_full_register_list = false) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool GetVRegPairFromDebuggerShadowFrame(uint16_t vreg,
                                          VRegKind kind_lo,
                                          VRegKind kind_hi,
                                          uint64_t* val) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool GetVRegPairFromOptimizedCode(ArtMethod* m,
                                    uint16_t vreg,
                                    VRegKind kind_lo,
                                    VRegKind kind_hi,
                                    uint64_t* val) const
      REQUIRES_SHARED(Locks::mutator_lock_);
  bool GetVRegFromOptimizedCode(DexRegisterLocation location, uint32_t* val) const
      REQUIRES_SHARED(Locks::mutator_lock_);

  ShadowFrame* PrepareSetVReg(ArtMethod* m, uint16_t vreg, bool wide)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void ValidateFrame() const REQUIRES_SHARED(Locks::mutator_lock_);

  ALWAYS_INLINE CodeInfo* GetCurrentInlineInfo() const;
  ALWAYS_INLINE StackMap* GetCurrentStackMap() const;

  Thread* const thread_;
  const StackWalkKind walk_kind_;
  ShadowFrame* cur_shadow_frame_;
  ArtMethod** cur_quick_frame_;
  uintptr_t cur_quick_frame_pc_;
  const OatQuickMethodHeader* cur_oat_quick_method_header_;
  // Lazily computed, number of frames in the stack.
  size_t num_frames_;
  // Depth of the frame we're currently at.
  size_t cur_depth_;
  // Current inlined frames of the method we are currently at.
  // We keep poping frames from the end as we visit the frames.
  BitTableRange<InlineInfo> current_inline_frames_;

  // Cache the most recently decoded inline info data.
  // The 'current_inline_frames_' refers to this data, so we need to keep it alive anyway.
  // Marked mutable since the cache fields are updated from const getters.
  mutable std::pair<const OatQuickMethodHeader*, CodeInfo> cur_inline_info_;
  mutable std::pair<uintptr_t, StackMap> cur_stack_map_;

  uint8_t* GetShouldDeoptimizeFlagAddr() const REQUIRES_SHARED(Locks::mutator_lock_);

 protected:
  Context* const context_;
  const bool check_suspended_;
};

}  // namespace art

#endif  // ART_RUNTIME_STACK_H_
