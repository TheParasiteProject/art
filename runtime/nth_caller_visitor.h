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

#ifndef ART_RUNTIME_NTH_CALLER_VISITOR_H_
#define ART_RUNTIME_NTH_CALLER_VISITOR_H_

#include "base/macros.h"
#include "art_method.h"
#include "base/locks.h"
#include "stack.h"

namespace art HIDDEN {
class Thread;

// Walks up the stack 'n' callers, when used with Thread::WalkStack.
struct NthCallerVisitor : public StackVisitor {
  NthCallerVisitor(Thread* thread, size_t n_in, bool include_runtime_and_upcalls = false)
      : StackVisitor(thread, nullptr, StackVisitor::StackWalkKind::kIncludeInlinedFrames),
        n(n_in),
        include_runtime_and_upcalls_(include_runtime_and_upcalls),
        count(0),
        caller(nullptr),
        caller_pc(0) {}

  bool VisitFrame() REQUIRES_SHARED(Locks::mutator_lock_) {
    ArtMethod* m = GetMethod();
    bool do_count = false;
    if (m == nullptr || m->IsRuntimeMethod()) {
      // Upcall.
      do_count = include_runtime_and_upcalls_;
    } else {
      do_count = true;
    }
    if (do_count) {
      DCHECK(caller == nullptr);
      if (count == n) {
        caller = m;
        caller_pc = GetCurrentQuickFramePc();
        return false;
      }
      count++;
    }
    return true;
  }

  const size_t n;
  const bool include_runtime_and_upcalls_;
  size_t count;
  ArtMethod* caller;
  uintptr_t caller_pc;
};

}  // namespace art

#endif  // ART_RUNTIME_NTH_CALLER_VISITOR_H_
