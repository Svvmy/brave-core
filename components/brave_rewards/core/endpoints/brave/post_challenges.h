/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_ENDPOINTS_BRAVE_POST_CHALLENGES_H_
#define BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_ENDPOINTS_BRAVE_POST_CHALLENGES_H_

#include <string>
#include <vector>

#include "brave/components/brave_rewards/common/mojom/rewards.mojom.h"
#include "brave/components/brave_rewards/common/mojom/rewards_core.mojom.h"
#include "brave/components/brave_rewards/core/endpoints/request_builder.h"
#include "brave/components/brave_rewards/core/endpoints/response_handler.h"
#include "brave/components/brave_rewards/core/endpoints/result_for.h"

// POST /v3/wallets/challenges
//
// Success code:
// HTTP_CREATED (201)
//
// Error codes:
// HTTP_BAD_REQUEST (400)
//
// Request body:
// {
//   "paymentId": ""
// }
//
// Response body:
// {
//   "challengeId": ""
// }

namespace brave_rewards::internal {
class RewardsEngineImpl;

namespace endpoints {

class PostChallenges;

template <>
struct ResultFor<PostChallenges> {
  using Value = std::string;  // challenge ID
  using Error = mojom::PostChallengesError;
};

class PostChallenges final : public RequestBuilder,
                             public ResponseHandler<PostChallenges> {
 public:
  static Result ProcessResponse(const mojom::UrlResponse&);

  explicit PostChallenges(RewardsEngineImpl& engine);
  ~PostChallenges() override;

 private:
  const char* Path() const;

  absl::optional<std::string> Url() const override;
  absl::optional<std::vector<std::string>> Headers(
      const std::string& content) const override;
  absl::optional<std::string> Content() const override;
  std::string ContentType() const override;
};

}  // namespace endpoints
}  // namespace brave_rewards::internal

#endif  // BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_ENDPOINTS_BRAVE_POST_CHALLENGES_H_
