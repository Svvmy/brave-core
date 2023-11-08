/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_ads/core/internal/user/user_interaction/ad_events/ad_event_handler_util.h"

#include "base/ranges/algorithm.h"
#include "brave/components/brave_ads/core/public/account/confirmations/confirmation_type.h"
#include "brave/components/brave_ads/core/public/units/ad_info.h"

namespace brave_ads {

bool HasFiredAdEvent(const AdInfo& ad,
                     const AdEventList& ad_events,
                     const ConfirmationType confirmation_type) {
  const auto iter = base::ranges::find_if(
      ad_events, [&ad, &confirmation_type](const AdEventInfo& ad_event) {
        return ad_event.placement_id == ad.placement_id &&
               ad_event.confirmation_type == confirmation_type;
      });

  return iter != ad_events.cend();
}

}  // namespace brave_ads
