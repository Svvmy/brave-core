/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_rewards/core/endpoints/brave/post_challenges.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "brave/components/brave_rewards/core/common/request_util.h"
#include "brave/components/brave_rewards/core/common/security_util.h"
#include "brave/components/brave_rewards/core/endpoint/promotion/promotions_util.h"
#include "brave/components/brave_rewards/core/rewards_engine_impl.h"
#include "brave/components/brave_rewards/core/wallet/wallet.h"
#include "net/http/http_status_code.h"

namespace brave_rewards::internal::endpoints {

using Error = PostChallenges::Error;
using Result = PostChallenges::Result;

namespace {

Result ParseBody(const std::string& body) {
  const auto value = base::JSONReader::Read(body);
  if (!value || !value->is_dict()) {
    BLOG(0, "Failed to parse body");
    return base::unexpected(Error::kFailedToParseBody);
  }

  const auto* challenge_id = value->GetDict().FindString("challengeId");
  if (!challenge_id || challenge_id->empty()) {
    BLOG(0, "Failed to parse body");
    return base::unexpected(Error::kFailedToParseBody);
  }

  return *challenge_id;
}

}  // namespace

// static
Result PostChallenges::ProcessResponse(const mojom::UrlResponse& response) {
  // TODO(zenparsing): Mock mock
  // return ParseBody(R"({ "challedgeId: "abc123" })");

  switch (response.status_code) {
    case net::HTTP_CREATED:  // HTTP 201
      return ParseBody(response.body);
    case net::HTTP_BAD_REQUEST:  // HTTP 400
      BLOG(0, "Invalid request!");
      return base::unexpected(Error::kInvalidRequest);
    case net::HTTP_INTERNAL_SERVER_ERROR:  // HTTP 500
      BLOG(0, "Unexpected error!");
      return base::unexpected(Error::kUnexpectedError);
    default:
      BLOG(0, "Unexpected status code! (HTTP " << response.status_code << ')');
      return base::unexpected(Error::kUnexpectedStatusCode);
  }
}

PostChallenges::PostChallenges(RewardsEngineImpl& engine)
    : RequestBuilder(engine) {}

PostChallenges::~PostChallenges() = default;

const char* PostChallenges::Path() const {
  return "/v3/wallets/challenges";
}

absl::optional<std::string> PostChallenges::Url() const {
  return endpoint::promotion::GetServerUrl(Path());
}

absl::optional<std::vector<std::string>> PostChallenges::Headers(
    const std::string& content) const {
  const auto wallet = engine_->wallet()->GetWallet();
  if (!wallet) {
    BLOG(0, "Rewards wallet is null!");
    return absl::nullopt;
  }

  DCHECK(!wallet->recovery_seed.empty());

  return util::BuildSignHeaders(
      std::string("post ") + Path(), content,
      util::Security::GetPublicKeyHexFromSeed(wallet->recovery_seed),
      wallet->recovery_seed);
}

absl::optional<std::string> PostChallenges::Content() const {
  const auto wallet = engine_->wallet()->GetWallet();
  if (!wallet) {
    BLOG(0, "Rewards wallet is null");
    return absl::nullopt;
  }

  base::Value::Dict content;
  content.Set("paymentId", wallet->payment_id);

  std::string json;
  if (!base::JSONWriter::Write(content, &json)) {
    BLOG(0, "Failed to write content to JSON");
    return absl::nullopt;
  }

  return json;
}

std::string PostChallenges::ContentType() const {
  return kApplicationJson;
}

}  // namespace brave_rewards::internal::endpoints
