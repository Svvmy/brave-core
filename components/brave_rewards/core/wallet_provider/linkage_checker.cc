/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_rewards/core/wallet_provider/linkage_checker.h"

#include <string>
#include <utility>

#include "brave/components/brave_rewards/core/endpoints/request_for.h"
#include "brave/components/brave_rewards/core/rewards_engine_impl.h"
#include "brave/components/brave_rewards/core/state/state_keys.h"
#include "brave/components/brave_rewards/core/wallet/wallet_util.h"

namespace brave_rewards::internal {

LinkageChecker::LinkageChecker(RewardsEngineImpl& engine) : engine_(engine) {}

LinkageChecker::~LinkageChecker() = default;

void LinkageChecker::Start() {
  if (timer_.IsRunning()) {
    return;
  }
  CheckLinkage();
  timer_.Start(FROM_HERE, base::Hours(24), this, &LinkageChecker::CheckLinkage);
}

void LinkageChecker::Stop() {
  timer_.Stop();
}

void LinkageChecker::CheckLinkage() {
  endpoints::RequestFor<endpoints::GetWallet>(*engine_).Send(base::BindOnce(
      &LinkageChecker::CheckLinkageCallback, weak_factory_.GetWeakPtr()));
}

void LinkageChecker::CheckLinkageCallback(
    endpoints::GetWallet::Result&& result) {
  if (!result.has_value()) {
    return;
  }

  auto& value = result.value();

  // If the user has a connected wallet, but the server indicates that the
  // user is linked to a different provider, or is not linked at all, then
  // transition the user back into the not-connected state.
  auto wallet_type = engine_->GetState<std::string>(state::kExternalWalletType);
  if (!wallet_type.empty()) {
    auto wallet = wallet::GetWalletIf(
        *engine_, wallet_type,
        {mojom::WalletStatus::kConnected, mojom::WalletStatus::kLoggedOut});

    if (wallet && (value.wallet_provider != wallet->type || !value.linked)) {
      // {kConnected, kLoggedOut} ==> kNotConnected
      if (wallet::TransitionWallet(*engine_, std::move(wallet),
                                   mojom::WalletStatus::kNotConnected)) {
        engine_->client()->ExternalWalletDisconnected();
      } else {
        BLOG(0, "Failed to transition " << wallet->type << " wallet state!");
      }
    }
  }

  // Save the available self-custody providers for this user.
  engine_->SetState(state::kSelfCustodyAvailable,
                    base::Value(std::move(value.self_custody_available)));
}

}  // namespace brave_rewards::internal
