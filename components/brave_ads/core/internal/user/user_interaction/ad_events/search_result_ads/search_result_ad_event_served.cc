/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_ads/core/internal/user/user_interaction/ad_events/search_result_ads/search_result_ad_event_served.h"

#include <utility>

#include "brave/components/brave_ads/core/internal/creatives/search_result_ads/search_result_ad_info.h"
#include "brave/components/brave_ads/core/internal/user/user_interaction/ad_events/ad_events.h"
#include "brave/components/brave_ads/core/public/account/confirmations/confirmation_type.h"

namespace brave_ads {

void SearchResultAdEventServed::FireEvent(const SearchResultAdInfo& ad,
                                          ResultCallback callback) {
  RecordAdEvent(ad, ConfirmationType::kServed, std::move(callback));
}

}  // namespace brave_ads
