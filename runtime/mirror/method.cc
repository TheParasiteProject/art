/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "method.h"

#include "art_method.h"
#include "class_root-inl.h"
#include "mirror/class-alloc-inl.h"
#include "mirror/object-inl.h"
#include "obj_ptr-inl.h"

namespace art HIDDEN {
namespace mirror {

template <PointerSize kPointerSize>
ObjPtr<Method> Method::CreateFromArtMethod(Thread* self, ArtMethod* method) {
  DCHECK(!method->IsConstructor()) << method->PrettyMethod();
  ObjPtr<Method> ret = ObjPtr<Method>::DownCast(GetClassRoot<Method>()->AllocObject(self));
  if (LIKELY(ret != nullptr)) {
    ret->InitializeFromArtMethod<kPointerSize>(method);
  }
  return ret;
}

template ObjPtr<Method> Method::CreateFromArtMethod<PointerSize::k32>(
    Thread* self, ArtMethod* method);
template ObjPtr<Method> Method::CreateFromArtMethod<PointerSize::k64>(
    Thread* self, ArtMethod* method);

template <PointerSize kPointerSize>
ObjPtr<Constructor> Constructor::CreateFromArtMethod(Thread* self, ArtMethod* method) {
  DCHECK(method->IsConstructor()) << method->PrettyMethod();
  ObjPtr<Constructor> ret =
      ObjPtr<Constructor>::DownCast(GetClassRoot<Constructor>()->AllocObject(self));
  if (LIKELY(ret != nullptr)) {
    ret->InitializeFromArtMethod<kPointerSize>(method);
  }
  return ret;
}

template ObjPtr<Constructor> Constructor::CreateFromArtMethod<PointerSize::k32>(
    Thread* self, ArtMethod* method);
template ObjPtr<Constructor> Constructor::CreateFromArtMethod<PointerSize::k64>(
    Thread* self, ArtMethod* method);

}  // namespace mirror
}  // namespace art
