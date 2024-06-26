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

#ifndef ART_COMPILER_JNI_QUICK_CALLING_CONVENTION_H_
#define ART_COMPILER_JNI_QUICK_CALLING_CONVENTION_H_

#include "base/arena_object.h"
#include "base/array_ref.h"
#include "base/enums.h"
#include "base/macros.h"
#include "dex/primitive.h"
#include "thread.h"
#include "utils/managed_register.h"

namespace art HIDDEN {

enum class InstructionSet;

// Top-level abstraction for different calling conventions.
class CallingConvention : public DeletableArenaObject<kArenaAllocCallingConvention> {
 public:
  bool IsReturnAReference() const { return shorty_[0] == 'L'; }

  Primitive::Type GetReturnType() const {
    return Primitive::GetType(shorty_[0]);
  }

  size_t SizeOfReturnValue() const {
    size_t result = Primitive::ComponentSize(Primitive::GetType(shorty_[0]));
    if (result >= 1 && result < 4) {
      result = 4;
    }
    return result;
  }

  // Register that holds result of this method invocation.
  virtual ManagedRegister ReturnRegister() const = 0;

  // Iterator interface

  // Place iterator at start of arguments. The displacement is applied to
  // frame offset methods to account for frames which may be on the stack
  // below the one being iterated over.
  virtual void ResetIterator(FrameOffset displacement) {
    displacement_ = displacement;
    itr_slots_ = 0;
    itr_args_ = 0;
    itr_refs_ = 0;
    itr_longs_and_doubles_ = 0;
    itr_float_and_doubles_ = 0;
  }

  FrameOffset GetDisplacement() const {
    return displacement_;
  }

  PointerSize GetFramePointerSize() const {
    return frame_pointer_size_;
  }

  virtual ~CallingConvention() {}

 protected:
  CallingConvention(bool is_static,
                    bool is_synchronized,
                    const char* shorty,
                    PointerSize frame_pointer_size)
      : itr_slots_(0), itr_refs_(0), itr_args_(0), itr_longs_and_doubles_(0),
        itr_float_and_doubles_(0), displacement_(0),
        frame_pointer_size_(frame_pointer_size),
        is_static_(is_static), is_synchronized_(is_synchronized),
        shorty_(shorty) {
    num_args_ = (is_static ? 0 : 1) + strlen(shorty) - 1;
    num_ref_args_ = is_static ? 0 : 1;  // The implicit this pointer.
    num_float_or_double_args_ = 0;
    num_long_or_double_args_ = 0;
    for (size_t i = 1; i < strlen(shorty); i++) {
      char ch = shorty_[i];
      switch (ch) {
      case 'L':
        num_ref_args_++;
        break;
      case 'J':
        num_long_or_double_args_++;
        break;
      case 'D':
        num_long_or_double_args_++;
        num_float_or_double_args_++;
        break;
      case 'F':
        num_float_or_double_args_++;
        break;
      }
    }
  }

  bool IsStatic() const {
    return is_static_;
  }
  bool IsSynchronized() const {
    return is_synchronized_;
  }
  bool IsParamALongOrDouble(unsigned int param) const {
    DCHECK_LT(param, NumArgs());
    if (IsStatic()) {
      param++;  // 0th argument must skip return value at start of the shorty
    } else if (param == 0) {
      return false;  // this argument
    }
    char ch = shorty_[param];
    return (ch == 'J' || ch == 'D');
  }
  bool IsParamAFloatOrDouble(unsigned int param) const {
    DCHECK_LT(param, NumArgs());
    if (IsStatic()) {
      param++;  // 0th argument must skip return value at start of the shorty
    } else if (param == 0) {
      return false;  // this argument
    }
    char ch = shorty_[param];
    return (ch == 'F' || ch == 'D');
  }
  bool IsParamADouble(unsigned int param) const {
    DCHECK_LT(param, NumArgs());
    if (IsStatic()) {
      param++;  // 0th argument must skip return value at start of the shorty
    } else if (param == 0) {
      return false;  // this argument
    }
    return shorty_[param] == 'D';
  }
  bool IsParamALong(unsigned int param) const {
    DCHECK_LT(param, NumArgs());
    if (IsStatic()) {
      param++;  // 0th argument must skip return value at start of the shorty
    } else if (param == 0) {
      return false;  // this argument
    }
    return shorty_[param] == 'J';
  }
  bool IsParamAReference(unsigned int param) const {
    DCHECK_LT(param, NumArgs());
    if (IsStatic()) {
      param++;  // 0th argument must skip return value at start of the shorty
    } else if (param == 0) {
      return true;  // this argument
    }
    return shorty_[param] == 'L';
  }
  size_t NumArgs() const {
    return num_args_;
  }
  // Implicit argument count: 1 for instance functions, 0 for static functions.
  // (The implicit argument is only relevant to the shorty, i.e.
  // the 0th arg is not in the shorty if it's implicit).
  size_t NumImplicitArgs() const {
    return IsStatic() ? 0 : 1;
  }
  size_t NumLongOrDoubleArgs() const {
    return num_long_or_double_args_;
  }
  size_t NumFloatOrDoubleArgs() const {
    return num_float_or_double_args_;
  }
  size_t NumReferenceArgs() const {
    return num_ref_args_;
  }
  size_t ParamSize(size_t param, size_t reference_size) const {
    DCHECK_LT(param, NumArgs());
    if (IsStatic()) {
      param++;  // 0th argument must skip return value at start of the shorty
    } else if (param == 0) {
      return reference_size;  // this argument
    }
    Primitive::Type type = Primitive::GetType(shorty_[param]);
    if (type == Primitive::kPrimNot) {
      return reference_size;
    }
    size_t result = Primitive::ComponentSize(type);
    if (result >= 1 && result < 4) {
      result = 4;
    }
    return result;
  }
  const char* GetShorty() const {
    return shorty_.c_str();
  }
  // The slot number for current calling_convention argument.
  // Note that each slot is 32-bit. When the current argument is bigger
  // than 32 bits, return the first slot number for this argument.
  unsigned int itr_slots_;
  // The number of references iterated past.
  unsigned int itr_refs_;
  // The argument number along argument list for current argument.
  unsigned int itr_args_;
  // Number of longs and doubles seen along argument list.
  unsigned int itr_longs_and_doubles_;
  // Number of float and doubles seen along argument list.
  unsigned int itr_float_and_doubles_;
  // Space for frames below this on the stack.
  FrameOffset displacement_;
  // The size of a pointer.
  const PointerSize frame_pointer_size_;

 private:
  const bool is_static_;
  const bool is_synchronized_;
  std::string shorty_;
  size_t num_args_;
  size_t num_ref_args_;
  size_t num_float_or_double_args_;
  size_t num_long_or_double_args_;
};

// Abstraction for managed code's calling conventions
// | { Incoming stack args } |
// | { Prior Method* }       | <-- Prior SP
// | { Return address }      |
// | { Callee saves }        |
// | { Spills ... }          |
// | { Outgoing stack args } |
// | { Method* }             | <-- SP
class ManagedRuntimeCallingConvention : public CallingConvention {
 public:
  static std::unique_ptr<ManagedRuntimeCallingConvention> Create(ArenaAllocator* allocator,
                                                                 bool is_static,
                                                                 bool is_synchronized,
                                                                 const char* shorty,
                                                                 InstructionSet instruction_set);

  // Offset of Method within the managed frame.
  FrameOffset MethodStackOffset() {
    return FrameOffset(0u);
  }

  // Register that holds the incoming method argument
  virtual ManagedRegister MethodRegister() = 0;

  // Register that is used to pass frame size for method exit hook call. This
  // shouldn't be the same as the return register since method exit hook also expects
  // return values in the return register.
  virtual ManagedRegister ArgumentRegisterForMethodExitHook() = 0;

  // Iterator interface
  bool HasNext();
  virtual void Next();
  bool IsCurrentParamAReference();
  bool IsCurrentParamAFloatOrDouble();
  bool IsCurrentParamADouble();
  bool IsCurrentParamALong();
  bool IsCurrentParamALongOrDouble() {
    return IsCurrentParamALong() || IsCurrentParamADouble();
  }
  bool IsCurrentArgExplicit();  // ie a non-implict argument such as this
  bool IsCurrentArgPossiblyNull();
  size_t CurrentParamSize();
  virtual bool IsCurrentParamInRegister() = 0;
  virtual bool IsCurrentParamOnStack() = 0;
  virtual ManagedRegister CurrentParamRegister() = 0;
  virtual FrameOffset CurrentParamStackOffset() = 0;

  virtual ~ManagedRuntimeCallingConvention() {}

 protected:
  ManagedRuntimeCallingConvention(bool is_static,
                                  bool is_synchronized,
                                  const char* shorty,
                                  PointerSize frame_pointer_size)
      : CallingConvention(is_static, is_synchronized, shorty, frame_pointer_size) {}
};

// Abstraction for JNI calling conventions
// | { Incoming stack args }         | <-- Prior SP
// | { Return address }              |
// | { Callee saves }                |     ([1])
// | { Return value spill }          |     (live on return slow paths)
// | { Local Ref. Table State }      |
// | { Stack Indirect Ref. Table     |
// |   num. refs./link }             |     (here to prior SP is frame size)
// | { Method* }                     | <-- Anchor SP written to thread
// | { Outgoing stack args }         | <-- SP at point of call
// | Native frame                    |
//
// [1] We must save all callee saves here to enable any exception throws to restore
// callee saves for frames above this one.
class JniCallingConvention : public CallingConvention {
 public:
  static std::unique_ptr<JniCallingConvention> Create(ArenaAllocator* allocator,
                                                      bool is_static,
                                                      bool is_synchronized,
                                                      bool is_fast_native,
                                                      bool is_critical_native,
                                                      const char* shorty,
                                                      InstructionSet instruction_set);

  // Size of frame excluding space for outgoing args (its assumed Method* is
  // always at the bottom of a frame, but this doesn't work for outgoing
  // native args). Includes alignment.
  virtual size_t FrameSize() const = 0;
  // Size of outgoing frame, i.e. stack arguments, @CriticalNative return PC if needed, alignment.
  // -- Arguments that are passed via registers are excluded from this size.
  virtual size_t OutFrameSize() const = 0;
  // Number of references in stack indirect reference table
  size_t ReferenceCount() const;
  // Register that holds result if it is integer.
  virtual ManagedRegister IntReturnRegister() const = 0;
  // Whether the compiler needs to ensure zero-/sign-extension of a small result type
  virtual bool RequiresSmallResultTypeExtension() const = 0;

  // Callee save registers to spill prior to native code (which may clobber)
  virtual ArrayRef<const ManagedRegister> CalleeSaveRegisters() const = 0;

  // Subset of core callee save registers that can be used for arbitrary purposes after
  // constructing the JNI transition frame. These should be both managed and native callee-saves.
  // These should not include special purpose registers such as thread register.
  // JNI compiler currently requires at least 4 callee save scratch registers, except for x86
  // where we have only 3 such registers but all args are passed on stack, so the method register
  // is never clobbered by argument moves and does not need to be preserved elsewhere.
  virtual ArrayRef<const ManagedRegister> CalleeSaveScratchRegisters() const = 0;

  // Subset of core argument registers that can be used for arbitrary purposes after
  // calling the native function. These should exclude the return register(s).
  virtual ArrayRef<const ManagedRegister> ArgumentScratchRegisters() const = 0;

  // Spill mask values
  virtual uint32_t CoreSpillMask() const = 0;
  virtual uint32_t FpSpillMask() const = 0;

  // Iterator interface
  bool HasNext();
  virtual void Next();
  bool IsCurrentParamAReference();
  bool IsCurrentParamAFloatOrDouble();
  bool IsCurrentParamADouble();
  bool IsCurrentParamALong();
  bool IsCurrentParamALongOrDouble() {
    return IsCurrentParamALong() || IsCurrentParamADouble();
  }
  bool IsCurrentParamJniEnv();
  virtual size_t CurrentParamSize() const;
  virtual bool IsCurrentParamInRegister() = 0;
  virtual bool IsCurrentParamOnStack() = 0;
  virtual ManagedRegister CurrentParamRegister() = 0;
  virtual FrameOffset CurrentParamStackOffset() = 0;

  virtual ~JniCallingConvention() {}

  bool IsFastNative() const {
    return is_fast_native_;
  }

  bool IsCriticalNative() const {
    return is_critical_native_;
  }

  // Does the transition have a method pointer in the stack frame?
  bool SpillsMethod() const {
    // Exclude method pointer for @CriticalNative methods for optimization speed.
    return !IsCriticalNative();
  }

  // Locking argument register, used to pass the synchronization object for calls
  // to `JniLockObject()` and `JniUnlockObject()`.
  virtual ManagedRegister LockingArgumentRegister() const = 0;

  // Hidden argument register, used to pass the method pointer for @CriticalNative call.
  virtual ManagedRegister HiddenArgumentRegister() const = 0;

  // Whether to use tail call (used only for @CriticalNative).
  virtual bool UseTailCall() const = 0;

  // Whether the return type is small. Used for RequiresSmallResultTypeExtension()
  // on architectures that require the sign/zero extension.
  bool HasSmallReturnType() const {
    Primitive::Type return_type = GetReturnType();
    return return_type == Primitive::kPrimByte ||
           return_type == Primitive::kPrimShort ||
           return_type == Primitive::kPrimBoolean ||
           return_type == Primitive::kPrimChar;
  }

 protected:
  // Named iterator positions
  enum IteratorPos {
    kJniEnv = 0,
    kObjectOrClass = 1
  };

  JniCallingConvention(bool is_static,
                       bool is_synchronized,
                       bool is_fast_native,
                       bool is_critical_native,
                       const char* shorty,
                       PointerSize frame_pointer_size)
      : CallingConvention(is_static, is_synchronized, shorty, frame_pointer_size),
        is_fast_native_(is_fast_native),
        is_critical_native_(is_critical_native) {}

 protected:
  size_t NumberOfExtraArgumentsForJni() const;

  // Does the transition have a local reference segment state?
  bool HasLocalReferenceSegmentState() const {
    // Exclude local reference segment states for @CriticalNative methods for optimization speed.
    return !IsCriticalNative();
  }

  // Are there extra JNI arguments (JNIEnv* and maybe jclass)?
  bool HasExtraArgumentsForJni() const {
    // @CriticalNative jni implementations exclude both JNIEnv* and the jclass/jobject parameters.
    return !IsCriticalNative();
  }

  // Has a JNIEnv* parameter implicitly?
  bool HasJniEnv() const {
    // Exclude "JNIEnv*" parameter for @CriticalNative methods.
    return HasExtraArgumentsForJni();
  }

  // Has a 'jclass' parameter implicitly?
  bool HasSelfClass() const;

  // Returns the position of itr_args_, fixed up by removing the offset of extra JNI arguments.
  size_t GetIteratorPositionWithinShorty() const;

  // Is the current argument (at the iterator) an extra argument for JNI?
  bool IsCurrentArgExtraForJni() const;

  const bool is_fast_native_;
  const bool is_critical_native_;

 private:
  // Shorthand for switching on the switch value but only IF there are extra JNI arguments.
  //
  // Puts the case value into return_value.
  // * (switch_value == kJniEnv) => case_jni_env
  // * (switch_value == kObjectOrClass) => case_object_or_class
  //
  // Returns false otherwise (or if there are no extra JNI arguments).
  bool SwitchExtraJniArguments(size_t switch_value,
                               bool case_jni_env,
                               bool case_object_or_class,
                               /* out parameters */
                               bool* return_value) const;
};

}  // namespace art

#endif  // ART_COMPILER_JNI_QUICK_CALLING_CONVENTION_H_
