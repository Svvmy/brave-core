/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_ads/core/internal/ml/pipeline/neural_pipeline_util.h"

#include <string>
#include <utility>

#include "brave/components/brave_ads/core/internal/common/unittest/unittest_base.h"
#include "brave/components/brave_ads/core/internal/common/unittest/unittest_file_path_util.h"
#include "brave/components/brave_ads/core/internal/ml/pipeline/pipeline_info.h"

// npm run test -- brave_unit_tests --filter=BraveAds*

namespace brave_ads::ml {

namespace {
constexpr char kOnlyRequiredFieldsNeuralModelPipeline[] =
    "ml/pipeline/text_processing/neural/only_required_fields_neural_model.fb";
}  // namespace

class BraveAdsNeuralPipelineUtilTest : public UnitTestBase {};

TEST_F(BraveAdsNeuralPipelineUtilTest, LoadNeuralPipelineTest) {
  // Arrange
  absl::optional<std::string> buffer =
      ReadFileFromTestPathToString(kOnlyRequiredFieldsNeuralModelPipeline);
  ASSERT_TRUE(buffer);

  // Act & Assert
  EXPECT_TRUE(pipeline::LoadNeuralPipeline(
      reinterpret_cast<const uint8_t*>(buffer->data()), buffer->size()));
}

}  // namespace brave_ads::ml
