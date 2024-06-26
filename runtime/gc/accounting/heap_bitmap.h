/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_H_
#define ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_H_

#include <android-base/logging.h>

#include "base/allocator.h"
#include "base/locks.h"
#include "base/macros.h"
#include "space_bitmap.h"

namespace art HIDDEN {
namespace gc {

class Heap;

namespace collector {
class ConcurrentCopying;
}  // namespace collector

namespace accounting {

class HeapBitmap {
 public:
  bool Test(const mirror::Object* obj) REQUIRES_SHARED(Locks::heap_bitmap_lock_);
  void Clear(const mirror::Object* obj) REQUIRES(Locks::heap_bitmap_lock_);
  template<typename LargeObjectSetVisitor>
  bool Set(const mirror::Object* obj, const LargeObjectSetVisitor& visitor)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_) ALWAYS_INLINE;
  template<typename LargeObjectSetVisitor>
  bool AtomicTestAndSet(const mirror::Object* obj, const LargeObjectSetVisitor& visitor)
      REQUIRES_SHARED(Locks::mutator_lock_)
      REQUIRES(Locks::heap_bitmap_lock_) ALWAYS_INLINE;
  ContinuousSpaceBitmap* GetContinuousSpaceBitmap(const mirror::Object* obj) const;
  LargeObjectBitmap* GetLargeObjectBitmap(const mirror::Object* obj) const;

  template <typename Visitor>
  ALWAYS_INLINE void Visit(Visitor&& visitor)
      REQUIRES(Locks::heap_bitmap_lock_)
      REQUIRES_SHARED(Locks::mutator_lock_);

  explicit HeapBitmap(Heap* heap) : heap_(heap) {}

 private:
  const Heap* const heap_;

  void AddContinuousSpaceBitmap(ContinuousSpaceBitmap* bitmap);
  void RemoveContinuousSpaceBitmap(ContinuousSpaceBitmap* bitmap);
  void AddLargeObjectBitmap(LargeObjectBitmap* bitmap);
  void RemoveLargeObjectBitmap(LargeObjectBitmap* bitmap);

  // Bitmaps covering continuous spaces.
  std::vector<ContinuousSpaceBitmap*,
              TrackingAllocator<ContinuousSpaceBitmap*, kAllocatorTagHeapBitmap>>
      continuous_space_bitmaps_;

  // Sets covering discontinuous spaces.
  std::vector<LargeObjectBitmap*,
              TrackingAllocator<LargeObjectBitmap*, kAllocatorTagHeapBitmapLOS>>
      large_object_bitmaps_;

  friend class art::gc::Heap;
  friend class art::gc::collector::ConcurrentCopying;
};

}  // namespace accounting
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_H_
