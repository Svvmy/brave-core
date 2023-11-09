/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_WALLET_PROVIDER_LINKAGE_CHECKER_H_
#define BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_WALLET_PROVIDER_LINKAGE_CHECKER_H_

#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "brave/components/brave_rewards/core/endpoints/brave/get_wallet.h"

namespace brave_rewards::internal {

class RewardsEngineImpl;

class LinkageChecker {
 public:
  explicit LinkageChecker(RewardsEngineImpl& engine);
  ~LinkageChecker();

  // Starts the wallet linkage checker, if not already started. If not already
  // started, the check will be run immediately and then on a timer.
  void Start();

  // Stops the wallet linkage checker.
  void Stop();

 private:
  void CheckLinkage();
  void CheckLinkageCallback(endpoints::GetWallet::Result&& result);

  const raw_ref<RewardsEngineImpl> engine_;
  base::RepeatingTimer timer_;
  base::WeakPtrFactory<LinkageChecker> weak_factory_{this};
};

}  // namespace brave_rewards::internal

#endif  // BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_WALLET_PROVIDER_LINKAGE_CHECKER_H_
