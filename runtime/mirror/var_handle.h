/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef ART_RUNTIME_MIRROR_VAR_HANDLE_H_
#define ART_RUNTIME_MIRROR_VAR_HANDLE_H_

#include "handle.h"
#include "interpreter/shadow_frame.h"
#include "jvalue.h"
#include "object.h"

namespace art HIDDEN {

template<class T> class Handle;
class InstructionOperands;
template<class T> class ObjPtr;

enum class Intrinsics;

struct VarHandleOffsets;
struct FieldVarHandleOffsets;
struct StaticFieldVarHandleOffsets;
struct ArrayElementVarHandleOffsets;
struct ByteArrayViewVarHandleOffsets;
struct ByteBufferViewVarHandleOffsets;

class ReflectiveValueVisitor;
class ShadowFrameGetter;

namespace mirror {

class MethodType;
class RawMethodType;
class VarHandleTest;

// C++ mirror of java.lang.invoke.VarHandle
class MANAGED VarHandle : public Object {
 public:
  MIRROR_CLASS("Ljava/lang/invoke/VarHandle;");

  // The maximum number of parameters a VarHandle accessor method can
  // take. The Worst case is equivalent to a compare-and-swap
  // operation on an array element which requires four parameters
  // (array, index, old, new).
  static constexpr int kMaxAccessorParameters = 4;

  // The maximum number of VarType parameters a VarHandle accessor
  // method can take.
  static constexpr size_t kMaxVarTypeParameters = 2;

  // The minimum number of CoordinateType parameters a VarHandle acessor method may take.
  static constexpr size_t kMinCoordinateTypes = 0;

  // The maximum number of CoordinateType parameters a VarHandle acessor method may take.
  static constexpr size_t kMaxCoordinateTypes = 2;

  // Enumeration of the possible access modes. This mirrors the enum
  // in java.lang.invoke.VarHandle.
  enum class AccessMode : uint32_t {
    kGet,
    kSet,
    kGetVolatile,
    kSetVolatile,
    kGetAcquire,
    kSetRelease,
    kGetOpaque,
    kSetOpaque,
    kCompareAndSet,
    kCompareAndExchange,
    kCompareAndExchangeAcquire,
    kCompareAndExchangeRelease,
    kWeakCompareAndSetPlain,
    kWeakCompareAndSet,
    kWeakCompareAndSetAcquire,
    kWeakCompareAndSetRelease,
    kGetAndSet,
    kGetAndSetAcquire,
    kGetAndSetRelease,
    kGetAndAdd,
    kGetAndAddAcquire,
    kGetAndAddRelease,
    kGetAndBitwiseOr,
    kGetAndBitwiseOrRelease,
    kGetAndBitwiseOrAcquire,
    kGetAndBitwiseAnd,
    kGetAndBitwiseAndRelease,
    kGetAndBitwiseAndAcquire,
    kGetAndBitwiseXor,
    kGetAndBitwiseXorRelease,
    kGetAndBitwiseXorAcquire,
    kLast = kGetAndBitwiseXorAcquire,
  };
  constexpr static size_t kNumberOfAccessModes = static_cast<size_t>(AccessMode::kLast) + 1u;

  // Enumeration for describing the parameter and return types of an AccessMode.
  enum class AccessModeTemplate : uint32_t {
    kGet,                 // T Op(C0..CN)
    kSet,                 // void Op(C0..CN, T)
    kCompareAndSet,       // boolean Op(C0..CN, T, T)
    kCompareAndExchange,  // T Op(C0..CN, T, T)
    kGetAndUpdate,        // T Op(C0..CN, T)
  };

  // Returns true if the AccessMode specified is a supported operation.
  bool IsAccessModeSupported(AccessMode accessMode) REQUIRES_SHARED(Locks::mutator_lock_) {
    return (GetAccessModesBitMask() & (1u << static_cast<uint32_t>(accessMode))) != 0;
  }

  enum MatchKind : uint8_t {
    kNone,
    kWithConversions,
    kExact
  };

  // Returns match information on the compatability between the exact method type for
  // 'access_mode' and the provided 'method_type'.
  MatchKind GetMethodTypeMatchForAccessMode(AccessMode access_mode, ObjPtr<MethodType> method_type)
        REQUIRES_SHARED(Locks::mutator_lock_);
  MatchKind GetMethodTypeMatchForAccessMode(AccessMode access_mode, Handle<MethodType> method_type)
        REQUIRES_SHARED(Locks::mutator_lock_);
  MatchKind GetMethodTypeMatchForAccessMode(AccessMode access_mode, RawMethodType method_type)
        REQUIRES_SHARED(Locks::mutator_lock_);

  // Allocates and returns the MethodType associated with the
  // AccessMode. No check is made for whether the AccessMode is a
  // supported operation so the MethodType can be used when raising a
  // WrongMethodTypeException exception.
  ObjPtr<MethodType> GetMethodTypeForAccessMode(Thread* self, AccessMode access_mode)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Overload that fills a handle scope with the return type and argument types
  // instead of creating an actual `MethodType`.
  void GetMethodTypeForAccessMode(AccessMode access_mode, /*out*/ RawMethodType method_type)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Returns a string representing the descriptor of the MethodType associated with
  // this AccessMode.
  std::string PrettyDescriptorForAccessMode(AccessMode access_mode)
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool Access(AccessMode access_mode,
              ShadowFrame* shadow_frame,
              const InstructionOperands* const operands,
              JValue* result)
      REQUIRES_SHARED(Locks::mutator_lock_);

  // Gets the variable type that is operated on by this VarHandle instance.
  ObjPtr<Class> GetVarType() REQUIRES_SHARED(Locks::mutator_lock_);

  // Gets the type of the object that this VarHandle operates on, null for StaticFieldVarHandle.
  ObjPtr<Class> GetCoordinateType0() REQUIRES_SHARED(Locks::mutator_lock_);

  // Gets the return type descriptor for a named accessor method,
  // nullptr if accessor_method is not supported.
  static const char* GetReturnTypeDescriptor(const char* accessor_method);

  // Returns the AccessMode corresponding to a VarHandle accessor intrinsic.
  static AccessMode GetAccessModeByIntrinsic(Intrinsics ordinal);

  // Returns true and sets access_mode if method_name corresponds to a
  // VarHandle access method, such as "setOpaque". Returns false otherwise.
  static bool GetAccessModeByMethodName(const char* method_name, AccessMode* access_mode);

  // Returns the AccessModeTemplate for a given mode.
  static AccessModeTemplate GetAccessModeTemplate(AccessMode access_mode);

  // Returns the AccessModeTemplate corresponding to a VarHandle accessor intrinsic.
  static AccessModeTemplate GetAccessModeTemplateByIntrinsic(Intrinsics ordinal);

  // Returns the number of VarType parameters for an access mode template.
  static int32_t GetNumberOfVarTypeParameters(AccessModeTemplate access_mode_template);

  static MemberOffset VarTypeOffset() {
    return MemberOffset(OFFSETOF_MEMBER(VarHandle, var_type_));
  }

  static MemberOffset CoordinateType0Offset() {
    return MemberOffset(OFFSETOF_MEMBER(VarHandle, coordinate_type0_));
  }

  static MemberOffset CoordinateType1Offset() {
    return MemberOffset(OFFSETOF_MEMBER(VarHandle, coordinate_type1_));
  }

  static MemberOffset AccessModesBitMaskOffset() {
    return MemberOffset(OFFSETOF_MEMBER(VarHandle, access_modes_bit_mask_));
  }

 private:
  ObjPtr<Class> GetCoordinateType1() REQUIRES_SHARED(Locks::mutator_lock_);
  int32_t GetAccessModesBitMask() REQUIRES_SHARED(Locks::mutator_lock_);

  template <typename MethodTypeType>
  static MatchKind GetMethodTypeMatchForAccessModeImpl(AccessMode access_mode,
                                                       ObjPtr<VarHandle> var_handle,
                                                       MethodTypeType method_type)
        REQUIRES_SHARED(Locks::mutator_lock_);

  HeapReference<mirror::Class> coordinate_type0_;
  HeapReference<mirror::Class> coordinate_type1_;
  HeapReference<mirror::Class> var_type_;
  int32_t access_modes_bit_mask_;

  friend class VarHandleTest;  // for testing purposes
  friend struct art::VarHandleOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(VarHandle);
};

// Represents a VarHandle to a static or instance field.
// The corresponding managed class in libart java.lang.invoke.FieldVarHandle.
class MANAGED FieldVarHandle : public VarHandle {
 public:
  MIRROR_CLASS("Ljava/lang/invoke/FieldVarHandle;");

  bool Access(AccessMode access_mode,
              ShadowFrame* shadow_frame,
              const InstructionOperands* const operands,
              JValue* result)
      REQUIRES_SHARED(Locks::mutator_lock_);

  template <VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ArtField* GetArtField() REQUIRES_SHARED(Locks::mutator_lock_) {
    return reinterpret_cast64<ArtField*>(GetField64<kVerifyFlags>(ArtFieldOffset()));
  }

  template <VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  void SetArtField(ArtField* art_field) REQUIRES_SHARED(Locks::mutator_lock_) {
    SetField64</*kTransactionActive*/ false,
               /*kCheckTransaction=*/ true,
               kVerifyFlags>(ArtFieldOffset(), reinterpret_cast64<uint64_t>(art_field));
  }

  // Used for updating var-handles to obsolete fields.
  void VisitTarget(ReflectiveValueVisitor* v) REQUIRES(Locks::mutator_lock_);

  static MemberOffset ArtFieldOffset() {
    return MemberOffset(OFFSETOF_MEMBER(FieldVarHandle, art_field_));
  }

 private:
  // ArtField instance corresponding to variable for accessors.
  int64_t art_field_;

  friend class VarHandleTest;  // for var_handle_test.
  friend struct art::FieldVarHandleOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(FieldVarHandle);
};

class MANAGED StaticFieldVarHandle : public FieldVarHandle {
 public:
  MIRROR_CLASS("Ljava/lang/invoke/StaticFieldVarHandle;");

  // Used for updating var-handles to obsolete fields.
  void VisitTarget(ReflectiveValueVisitor* v) REQUIRES(Locks::mutator_lock_);

  static MemberOffset DeclaringClassOffset() {
    return MemberOffset(OFFSETOF_MEMBER(StaticFieldVarHandle, declaring_class_));
  }

 private:
  HeapReference<mirror::Class> declaring_class_;

  friend class VarHandleTest;  // for var_handle_test.
  friend struct art::StaticFieldVarHandleOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(StaticFieldVarHandle);
};


// Represents a VarHandle providing accessors to an array.
// The corresponding managed class in libart java.lang.invoke.ArrayElementVarHandle.
class MANAGED ArrayElementVarHandle : public VarHandle {
 public:
  bool Access(AccessMode access_mode,
              ShadowFrame* shadow_frame,
              const InstructionOperands* const operands,
              JValue* result) REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  static bool CheckArrayStore(AccessMode access_mode,
                              ShadowFrameGetter getter,
                              ObjPtr<ObjectArray<Object>> array)
      REQUIRES_SHARED(Locks::mutator_lock_);

  friend class VarHandleTest;
  DISALLOW_IMPLICIT_CONSTRUCTORS(ArrayElementVarHandle);
};

// Represents a VarHandle providing accessors to a view of a ByteArray.
// The corresponding managed class in libart java.lang.invoke.ByteArrayViewVarHandle.
class MANAGED ByteArrayViewVarHandle : public VarHandle {
 public:
  MIRROR_CLASS("Ljava/lang/invoke/ByteArrayViewVarHandle;");

  bool Access(AccessMode access_mode,
              ShadowFrame* shadow_frame,
              const InstructionOperands* const operands,
              JValue* result)
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool GetNativeByteOrder() REQUIRES_SHARED(Locks::mutator_lock_);

  static MemberOffset NativeByteOrderOffset() {
    return MemberOffset(OFFSETOF_MEMBER(ByteArrayViewVarHandle, native_byte_order_));
  }

 private:
  // Flag indicating that accessors should use native byte-ordering.
  uint8_t native_byte_order_;

  friend class VarHandleTest;  // for var_handle_test.
  friend struct art::ByteArrayViewVarHandleOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(ByteArrayViewVarHandle);
};

// Represents a VarHandle providing accessors to a view of a ByteBuffer
// The corresponding managed class in libart java.lang.invoke.ByteBufferViewVarHandle.
class MANAGED ByteBufferViewVarHandle : public VarHandle {
 public:
  MIRROR_CLASS("Ljava/lang/invoke/ByteBufferViewVarHandle;");

  bool Access(AccessMode access_mode,
              ShadowFrame* shadow_frame,
              const InstructionOperands* const operands,
              JValue* result)
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool GetNativeByteOrder() REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  bool AccessHeapBuffer(AccessMode access_mode,
                        ObjPtr<Object> byte_buffer,
                        int buffer_offset,
                        ObjPtr<ByteArray> heap_byte_array,
                        ShadowFrameGetter* getter,
                        JValue* result)
      REQUIRES_SHARED(Locks::mutator_lock_);

  bool AccessFixedMemory(AccessMode access_mode,
                         ObjPtr<Object> byte_buffer,
                         int buffer_offset,
                         ShadowFrameGetter* getter,
                         JValue* result)
      REQUIRES_SHARED(Locks::mutator_lock_);

  static MemberOffset NativeByteOrderOffset() {
    return MemberOffset(OFFSETOF_MEMBER(ByteBufferViewVarHandle, native_byte_order_));
  }

  // Flag indicating that accessors should use native byte-ordering.
  uint8_t native_byte_order_;

  friend class VarHandleTest;  // for var_handle_test.
  friend struct art::ByteBufferViewVarHandleOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(ByteBufferViewVarHandle);
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_VAR_HANDLE_H_
