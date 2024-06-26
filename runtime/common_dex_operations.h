/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef ART_RUNTIME_COMMON_DEX_OPERATIONS_H_
#define ART_RUNTIME_COMMON_DEX_OPERATIONS_H_

#include "android-base/logging.h"
#include "art_field.h"
#include "art_method.h"
#include "base/locks.h"
#include "base/macros.h"
#include "class_linker.h"
#include "dex/code_item_accessors.h"
#include "dex/dex_file_structs.h"
#include "dex/primitive.h"
#include "entrypoints/entrypoint_utils.h"
#include "handle_scope-inl.h"
#include "instrumentation.h"
#include "interpreter/interpreter.h"
#include "interpreter/shadow_frame.h"
#include "interpreter/unstarted_runtime.h"
#include "jvalue-inl.h"
#include "mirror/class.h"
#include "mirror/object.h"
#include "obj_ptr-inl.h"
#include "reflective_handle.h"
#include "reflective_handle_scope.h"
#include "runtime.h"
#include "stack.h"
#include "thread.h"

namespace art HIDDEN {

namespace interpreter {
  void ArtInterpreterToInterpreterBridge(Thread* self,
                                        const dex::CodeItem* code_item,
                                        ShadowFrame* shadow_frame,
                                        JValue* result)
     REQUIRES_SHARED(Locks::mutator_lock_);

  void ArtInterpreterToCompiledCodeBridge(Thread* self,
                                          ArtMethod* caller,
                                          ShadowFrame* shadow_frame,
                                          uint16_t arg_offset,
                                          JValue* result);

}  // namespace interpreter

inline bool EnsureInitialized(Thread* self, ShadowFrame* shadow_frame)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  if (LIKELY(!shadow_frame->GetMethod()->StillNeedsClinitCheck())) {
    return true;
  }

  // Save the shadow frame.
  ScopedStackedShadowFramePusher pusher(self, shadow_frame);
  StackHandleScope<1> hs(self);
  Handle<mirror::Class> h_class = hs.NewHandle(shadow_frame->GetMethod()->GetDeclaringClass());
  if (UNLIKELY(!Runtime::Current()->GetClassLinker()->EnsureInitialized(
                    self, h_class, /*can_init_fields=*/ true, /*can_init_parents=*/ true))) {
    DCHECK(self->IsExceptionPending());
    return false;
  }
  DCHECK(h_class->IsInitializing());
  return true;
}

inline void PerformCall(Thread* self,
                        const CodeItemDataAccessor& accessor,
                        ArtMethod* caller_method,
                        const size_t first_dest_reg,
                        ShadowFrame* callee_frame,
                        JValue* result,
                        bool use_interpreter_entrypoint)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  if (UNLIKELY(!Runtime::Current()->IsStarted())) {
    interpreter::UnstartedRuntime::Invoke(self, accessor, callee_frame, result, first_dest_reg);
    return;
  }

  if (!EnsureInitialized(self, callee_frame)) {
    return;
  }

  if (use_interpreter_entrypoint) {
    interpreter::ArtInterpreterToInterpreterBridge(self, accessor, callee_frame, result);
  } else {
    interpreter::ArtInterpreterToCompiledCodeBridge(
        self, caller_method, callee_frame, first_dest_reg, result);
  }
}

template <typename T>
inline void DCheckStaticState(Thread* self, T* entity) REQUIRES_SHARED(Locks::mutator_lock_) {
  if (kIsDebugBuild) {
    ObjPtr<mirror::Class> klass = entity->GetDeclaringClass();
    if (entity->IsStatic()) {
      klass->AssertInitializedOrInitializingInThread(self);
    } else {
      CHECK(klass->IsInitializing() || klass->IsErroneousResolved());
    }
  }
}

template<Primitive::Type field_type>
static ALWAYS_INLINE bool DoFieldGetCommon(Thread* self,
                                           const ShadowFrame& shadow_frame,
                                           ObjPtr<mirror::Object> obj,
                                           ArtField* field,
                                           JValue* result)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCheckStaticState(self, field);

  // Report this field access to instrumentation if needed.
  instrumentation::Instrumentation* instrumentation = Runtime::Current()->GetInstrumentation();
  if (UNLIKELY(instrumentation->HasFieldReadListeners())) {
    StackHandleScope<1> hs(self);
    StackArtFieldHandleScope<1> rhs(self);
    // Wrap in handle wrapper in case the listener does thread suspension.
    HandleWrapperObjPtr<mirror::Object> h(hs.NewHandleWrapper(&obj));
    ReflectiveHandleWrapper<ArtField> fh(rhs.NewReflectiveHandleWrapper(&field));
    ObjPtr<mirror::Object> this_object;
    if (!field->IsStatic()) {
      this_object = obj;
    }
    instrumentation->FieldReadEvent(self,
                                    this_object,
                                    shadow_frame.GetMethod(),
                                    shadow_frame.GetDexPC(),
                                    field);
    if (UNLIKELY(self->IsExceptionPending())) {
      return false;
    }
  }

  switch (field_type) {
    case Primitive::kPrimBoolean:
      result->SetZ(field->GetBoolean(obj));
      break;
    case Primitive::kPrimByte:
      result->SetB(field->GetByte(obj));
      break;
    case Primitive::kPrimChar:
      result->SetC(field->GetChar(obj));
      break;
    case Primitive::kPrimShort:
      result->SetS(field->GetShort(obj));
      break;
    case Primitive::kPrimInt:
      result->SetI(field->GetInt(obj));
      break;
    case Primitive::kPrimLong:
      result->SetJ(field->GetLong(obj));
      break;
    case Primitive::kPrimNot:
      result->SetL(field->GetObject(obj));
      break;
    case Primitive::kPrimVoid:
      LOG(FATAL) << "Unreachable " << field_type;
      break;
  }
  return true;
}

template<Primitive::Type field_type, bool transaction_active>
ALWAYS_INLINE bool DoFieldPutCommon(Thread* self,
                                    const ShadowFrame& shadow_frame,
                                    ObjPtr<mirror::Object> obj,
                                    ArtField* field,
                                    JValue& value)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCheckStaticState(self, field);

  // Report this field access to instrumentation if needed. Since we only have the offset of
  // the field from the base of the object, we need to look for it first.
  instrumentation::Instrumentation* instrumentation = Runtime::Current()->GetInstrumentation();
  if (UNLIKELY(instrumentation->HasFieldWriteListeners())) {
    StackHandleScope<2> hs(self);
    StackArtFieldHandleScope<1> rhs(self);
    // Save this and return value (if needed) in case the instrumentation causes a suspend.
    HandleWrapperObjPtr<mirror::Object> h(hs.NewHandleWrapper(&obj));
    ReflectiveHandleWrapper<ArtField> fh(rhs.NewReflectiveHandleWrapper(&field));
    ObjPtr<mirror::Object> this_object = field->IsStatic() ? nullptr : obj;
    mirror::Object* fake_root = nullptr;
    HandleWrapper<mirror::Object> ret(hs.NewHandleWrapper<mirror::Object>(
        field_type == Primitive::kPrimNot ? value.GetGCRoot() : &fake_root));
    instrumentation->FieldWriteEvent(self,
                                     this_object,
                                     shadow_frame.GetMethod(),
                                     shadow_frame.GetDexPC(),
                                     field,
                                     value);
    if (UNLIKELY(self->IsExceptionPending())) {
      return false;
    }
    if (shadow_frame.GetForcePopFrame()) {
      // We need to check this here since we expect that the FieldWriteEvent happens before the
      // actual field write. If one pops the stack we should not modify the field.  The next
      // instruction will force a pop. Return true.
      DCHECK(Runtime::Current()->AreNonStandardExitsEnabled());
      return true;
    }
  }

  switch (field_type) {
    case Primitive::kPrimBoolean:
      field->SetBoolean<transaction_active>(obj, value.GetZ());
      break;
    case Primitive::kPrimByte:
      field->SetByte<transaction_active>(obj, value.GetB());
      break;
    case Primitive::kPrimChar:
      field->SetChar<transaction_active>(obj, value.GetC());
      break;
    case Primitive::kPrimShort:
      field->SetShort<transaction_active>(obj, value.GetS());
      break;
    case Primitive::kPrimInt:
      field->SetInt<transaction_active>(obj, value.GetI());
      break;
    case Primitive::kPrimLong:
      field->SetLong<transaction_active>(obj, value.GetJ());
      break;
    case Primitive::kPrimNot: {
      ObjPtr<mirror::Object> reg = value.GetL();
      if (reg != nullptr && !shadow_frame.GetMethod()->SkipAccessChecks()) {
        // FieldHelper::GetType can resolve classes, use a handle wrapper which will restore the
        // object in the destructor.
        ObjPtr<mirror::Class> field_class;
        {
          StackHandleScope<2> hs(self);
          StackArtFieldHandleScope<1> rhs(self);
          HandleWrapperObjPtr<mirror::Object> h_reg(hs.NewHandleWrapper(&reg));
          HandleWrapperObjPtr<mirror::Object> h_obj(hs.NewHandleWrapper(&obj));
          ReflectiveHandleWrapper<ArtField> fh(rhs.NewReflectiveHandleWrapper(&field));
          field_class = field->ResolveType();
        }
        // ArtField::ResolveType() may fail as evidenced with a dexing bug (b/78788577).
        if (UNLIKELY(field_class.IsNull())) {
          Thread::Current()->AssertPendingException();
          return false;
        }
        if (UNLIKELY(!reg->VerifierInstanceOf(field_class))) {
          // This should never happen.
          std::string temp1, temp2, temp3;
          self->ThrowNewExceptionF("Ljava/lang/InternalError;",
                                   "Put '%s' that is not instance of field '%s' in '%s'",
                                   reg->GetClass()->GetDescriptor(&temp1),
                                   field_class->GetDescriptor(&temp2),
                                   field->GetDeclaringClass()->GetDescriptor(&temp3));
          return false;
        }
      }
      field->SetObj<transaction_active>(obj, reg);
      break;
    }
    case Primitive::kPrimVoid: {
      LOG(FATAL) << "Unreachable " << field_type;
      break;
    }
  }
  if (transaction_active) {
    if (UNLIKELY(self->IsExceptionPending())) {
      return false;
    }
  }
  return true;
}

}  // namespace art

#endif  // ART_RUNTIME_COMMON_DEX_OPERATIONS_H_
