/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_WALLET_PROVIDER_SOLANA_SOLANA_PROVIDER_H_
#define BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_WALLET_PROVIDER_SOLANA_SOLANA_PROVIDER_H_

#include <string>

#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/brave_rewards/core/endpoints/brave/post_challenges.h"
#include "brave/components/brave_rewards/core/wallet_provider/wallet_provider.h"

namespace brave_rewards::internal {
class RewardsEngineImpl;

class SolanaProvider final : public wallet_provider::WalletProvider {
 public:
  explicit SolanaProvider(RewardsEngineImpl& engine);
  ~SolanaProvider() override;

  const char* WalletType() const override;

  void FetchBalance(
      base::OnceCallback<void(mojom::Result, double)> callback) override;

  void BeginLogin(BeginExternalWalletLoginCallback callback) override;

  std::string GetFeeAddress() const override;

 private:
  void PostChallengesCallback(BeginExternalWalletLoginCallback callback,
                              endpoints::PostChallenges::Result&& result);

  const raw_ref<RewardsEngineImpl> engine_;
  base::WeakPtrFactory<SolanaProvider> weak_factory_{this};
};

}  // namespace brave_rewards::internal

#endif  // BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_WALLET_PROVIDER_SOLANA_SOLANA_PROVIDER_H_
