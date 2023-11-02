/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_ads/core/internal/ml/model/neural/neural.h"

#include <string>
#include <vector>

#include "brave/components/brave_ads/core/internal/common/resources/flat/text_classification_neural_model_generated.h"
#include "brave/components/brave_ads/core/internal/common/unittest/unittest_base.h"
#include "brave/components/brave_ads/core/internal/ml/data/vector_data.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

// npm run test -- brave_unit_tests --filter=BraveAds*

namespace brave_ads::ml {

namespace {

std::string BuildRawNeuralModel(
    const std::vector<std::vector<VectorData>>& raw_matrices,
    const std::vector<std::string>& raw_activation_functions,
    const std::vector<std::string>& raw_segments) {
  flatbuffers::FlatBufferBuilder builder;

  std::vector<flatbuffers::Offset<flatbuffers::String>>
      activation_functions_data;
  activation_functions_data.reserve(raw_activation_functions.size());
  for (const auto& func : raw_activation_functions) {
    activation_functions_data.push_back(builder.CreateString(func));
  }
  auto activation_functions = builder.CreateVector(activation_functions_data);

  std::vector<flatbuffers::Offset<flatbuffers::String>> segments_data;
  segments_data.reserve(raw_segments.size());
  for (const auto& cls : raw_segments) {
    segments_data.push_back(builder.CreateString(cls));
  }
  auto segments = builder.CreateVector(segments_data);

  std::vector<flatbuffers::Offset<neural_text_classification::flat::Matrix>>
      matrices_data;
  matrices_data.reserve(raw_matrices.size());
  for (const auto& matrix : raw_matrices) {
    std::vector<
        ::flatbuffers::Offset<neural_text_classification::flat::WeightsRow>>
        weights_rows_data;
    weights_rows_data.reserve(matrix.size());
    for (const auto& row : matrix) {
      auto weights_row = builder.CreateVector(row.GetData());
      weights_rows_data.push_back(
          neural_text_classification::flat::CreateWeightsRow(builder,
                                                             weights_row));
    }
    auto weights_rows = builder.CreateVector(weights_rows_data);
    matrices_data.push_back(
        neural_text_classification::flat::CreateMatrix(builder, weights_rows));
  }
  auto matrices = builder.CreateVector(matrices_data);

  auto classifier = neural_text_classification::flat::CreateClassifier(
      builder, builder.CreateString("NEURAL"), segments, matrices,
      activation_functions);

  neural_text_classification::flat::ModelBuilder neural_model_builder(builder);
  neural_model_builder.add_classifier(classifier);
  builder.Finish(neural_model_builder.Finish());

  std::string buffer(reinterpret_cast<char*>(builder.GetBufferPointer()),
                     builder.GetSize());

  return buffer;
}

}  // namespace

class BraveAdsNeuralTest : public UnitTestBase {
 public:
  absl::optional<NeuralModel> BuildNeuralModel(
      const std::vector<std::vector<VectorData>>& raw_matrices,
      const std::vector<std::string>& raw_activation_functions,
      const std::vector<std::string>& raw_segments) {
    buffer_ = BuildRawNeuralModel(raw_matrices, raw_activation_functions,
                                  raw_segments);
    flatbuffers::Verifier verifier(
        reinterpret_cast<const uint8_t*>(buffer_.data()), buffer_.size());
    if (!neural_text_classification::flat::VerifyModelBuffer(verifier)) {
      return absl::nullopt;
    }

    const auto* raw_model =
        neural_text_classification::flat::GetModel(buffer_.data());
    if (!raw_model) {
      return absl::nullopt;
    }

    return NeuralModel(raw_model);
  }

 private:
  std::string buffer_;
};

TEST_F(BraveAdsNeuralTest, Prediction) {
  // Arrange
  constexpr double kTolerance = 1e-6;

  std::vector<VectorData> matrix_1 = {VectorData({1.0, 0.0, -3.5}),
                                      VectorData({0.0, 2.2, 8.3})};
  std::vector<VectorData> matrix_2 = {VectorData({-0.5, 1.6}),
                                      VectorData({4.38, -1.0}),
                                      VectorData({2.0, 1.0})};
  std::vector<std::vector<VectorData>> matrices = {matrix_1, matrix_2};

  std::vector<std::string> activation_functions = {"tanh", "softmax"};

  std::vector<std::string> segments = {"class_1", "class_2", "class_3"};

  absl::optional<NeuralModel> neural(
      BuildNeuralModel(matrices, activation_functions, segments));
  ASSERT_TRUE(neural);
  const VectorData sample_observation({0.2, 0.65, 0.15});

  // Act
  const absl::optional<PredictionMap> sample_predictions =
      neural->Predict(sample_observation);
  ASSERT_TRUE(sample_predictions);

  // Assert
  EXPECT_TRUE(
      (std::fabs(0.78853326 - sample_predictions->at("class_1")) <
       kTolerance) &&
      (std::fabs(0.01296594 - sample_predictions->at("class_2")) <
       kTolerance) &&
      (std::fabs(0.19850080 - sample_predictions->at("class_3")) < kTolerance));
}

TEST_F(BraveAdsNeuralTest, PredictionNomatrices) {
  // Arrange
  constexpr double kTolerance = 1e-6;

  std::vector<std::vector<VectorData>> matrices = {};
  std::vector<std::string> activation_functions = {};
  std::vector<std::string> segments = {"class_1", "class_2", "class_3"};

  absl::optional<NeuralModel> neural(
      BuildNeuralModel(matrices, activation_functions, segments));
  ASSERT_TRUE(neural);
  const VectorData sample_observation({0.2, 0.65, 0.15});

  // Act
  const absl::optional<PredictionMap> sample_predictions =
      neural->Predict(sample_observation);
  ASSERT_TRUE(sample_predictions);

  // Assert
  EXPECT_TRUE(
      (std::fabs(0.2 - sample_predictions->at("class_1")) < kTolerance) &&
      (std::fabs(0.65 - sample_predictions->at("class_2")) < kTolerance) &&
      (std::fabs(0.15 - sample_predictions->at("class_3")) < kTolerance));
}

TEST_F(BraveAdsNeuralTest, PredictionDefaultPostMatrixFunctions) {
  // Arrange
  constexpr double kTolerance = 1e-6;

  std::vector<VectorData> matrix_1 = {VectorData({1.0, 0.0, -3.5}),
                                      VectorData({0.0, 2.2, 8.3})};
  std::vector<VectorData> matrix_2 = {VectorData({-0.5, 1.6}),
                                      VectorData({4.38, -1.0}),
                                      VectorData({2.0, 1.0})};
  std::vector<std::vector<VectorData>> matrices = {matrix_1, matrix_2};

  std::vector<std::string> activation_functions = {"tanh_misspelled", "none"};

  std::vector<std::string> segments = {"class_1", "class_2", "class_3"};

  absl::optional<NeuralModel> neural(
      BuildNeuralModel(matrices, activation_functions, segments));
  ASSERT_TRUE(neural);
  const VectorData sample_observation({0.2, 0.65, 0.15});

  // Act
  const absl::optional<PredictionMap> sample_predictions =
      neural->Predict(sample_observation);
  ASSERT_TRUE(sample_predictions);

  // Assert
  EXPECT_TRUE(
      (std::fabs(4.4425 - sample_predictions->at("class_1")) < kTolerance) &&
      (std::fabs(-4.0985 - sample_predictions->at("class_2")) < kTolerance) &&
      (std::fabs(2.025 - sample_predictions->at("class_3")) < kTolerance));
}

TEST_F(BraveAdsNeuralTest, TopPredictions) {
  // Arrange
  constexpr double kTolerance = 1e-6;

  std::vector<VectorData> matrix_1 = {VectorData({1.0, 0.0, -3.5}),
                                      VectorData({0.0, 2.2, 8.3})};
  std::vector<VectorData> matrix_2 = {VectorData({-0.5, 1.6}),
                                      VectorData({4.38, -1.0}),
                                      VectorData({2.0, 1.0})};
  std::vector<std::vector<VectorData>> matrices = {matrix_1, matrix_2};

  std::vector<std::string> activation_functions = {"tanh", "softmax"};

  std::vector<std::string> segments = {"class_1", "class_2", "class_3"};

  absl::optional<NeuralModel> neural(
      BuildNeuralModel(matrices, activation_functions, segments));
  ASSERT_TRUE(neural);
  const VectorData sample_observation({0.2, 0.65, 0.15});

  // Act
  const absl::optional<PredictionMap> sample_predictions =
      neural->GetTopPredictions(sample_observation);
  ASSERT_TRUE(sample_predictions);
  const absl::optional<PredictionMap> sample_predictions_constrained =
      neural->GetTopCountPredictions(sample_observation, 2);
  ASSERT_TRUE(sample_predictions_constrained);

  // Assert
  EXPECT_TRUE((sample_predictions->size() == 3) &&
              sample_predictions_constrained->size() == 2);
  EXPECT_TRUE(
      (std::fabs(0.78853326 - sample_predictions->at("class_1")) <
       kTolerance) &&
      (std::fabs(0.01296594 - sample_predictions->at("class_2")) <
       kTolerance) &&
      (std::fabs(0.19850080 - sample_predictions->at("class_3")) < kTolerance));
  EXPECT_TRUE(
      (std::fabs(0.78853326 - sample_predictions_constrained->at("class_1")) <
       kTolerance) &&
      (std::fabs(0.19850080 - sample_predictions_constrained->at("class_3")) <
       kTolerance));
}

}  // namespace brave_ads::ml
