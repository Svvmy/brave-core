/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_rewards/core/wallet_provider/solana/solana_provider.h"

#include <utility>

#include "base/strings/strcat.h"
#include "brave/components/brave_rewards/core/endpoint/rewards/rewards_util.h"
#include "brave/components/brave_rewards/core/endpoints/request_for.h"
#include "brave/components/brave_rewards/core/global_constants.h"
#include "brave/components/brave_rewards/core/rewards_engine_impl.h"
#include "net/base/url_util.h"
#include "url/gurl.h"

namespace brave_rewards::internal {

SolanaProvider::SolanaProvider(RewardsEngineImpl& engine)
    : WalletProvider(engine), engine_(engine) {}

SolanaProvider::~SolanaProvider() = default;

const char* SolanaProvider::WalletType() const {
  return constant::kWalletSolana;
}

void SolanaProvider::FetchBalance(
    base::OnceCallback<void(mojom::Result, double)> callback) {
  LOG(ERROR) << "Not implemented";
  std::move(callback).Run(mojom::Result::FAILED, 0);
}

void SolanaProvider::BeginLogin(BeginExternalWalletLoginCallback callback) {
  endpoints::RequestFor<endpoints::PostChallenges>(*engine_).Send(
      base::BindOnce(&SolanaProvider::PostChallengesCallback,
                     weak_factory_.GetWeakPtr(), std::move(callback)));
}

void SolanaProvider::PostChallengesCallback(
    BeginExternalWalletLoginCallback callback,
    endpoints::PostChallenges::Result&& result) {
  if (!result.has_value()) {
    std::move(callback).Run(nullptr);
    return;
  }

  const std::string& challenge_id = result.value();
  DCHECK(!challenge_id.empty());

  const auto wallet = engine_->wallet()->GetWallet();
  if (!wallet) {
    std::move(callback).Run(nullptr);
    return;
  }

  std::string message = base::StrCat({wallet->payment_id, ".", challenge_id});
  // TODO(zenparsing): Sign the message using `wallet->recovery_seed`. It's not
  // clear yet how. All of our current signature utilities are designed to sign
  // a collection of key/value "headers".
  std::string signature = message;

  GURL url(endpoint::rewards::GetServerUrl("/connect"));
  url = net::AppendOrReplaceQueryParameter(url, "msg", message);
  url = net::AppendOrReplaceQueryParameter(url, "sig", signature);

  auto params = mojom::ExternalWalletLoginParams::New();
  params->url = url.spec();
  params->cookies["__Secure-CSRF_TOKEN"] = challenge_id;
  std::move(callback).Run(std::move(params));
}

std::string SolanaProvider::GetFeeAddress() const {
  return "";
}

}  // namespace brave_rewards::internal
