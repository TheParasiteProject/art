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

#include "instruction_set_features_x86_64.h"

#include <gtest/gtest.h>

namespace art HIDDEN {

TEST(X86_64InstructionSetFeaturesTest, X86Features) {
  const bool is_runtime_isa = kRuntimeISA == InstructionSet::kX86_64;
  std::string error_msg;
  std::unique_ptr<const InstructionSetFeatures> x86_64_features(
      InstructionSetFeatures::FromVariant(InstructionSet::kX86_64, "default", &error_msg));
  ASSERT_TRUE(x86_64_features.get() != nullptr) << error_msg;
  EXPECT_EQ(x86_64_features->GetInstructionSet(), InstructionSet::kX86_64);
  EXPECT_TRUE(x86_64_features->Equals(x86_64_features.get()));
  EXPECT_EQ(x86_64_features->GetFeatureString(),
            is_runtime_isa ? X86_64InstructionSetFeatures::FromCppDefines()->GetFeatureString()
                    : "-ssse3,-sse4.1,-sse4.2,-avx,-avx2,-popcnt");
  EXPECT_EQ(x86_64_features->AsBitmap(),
            is_runtime_isa ? X86_64InstructionSetFeatures::FromCppDefines()->AsBitmap() : 0);
}

}  // namespace art
