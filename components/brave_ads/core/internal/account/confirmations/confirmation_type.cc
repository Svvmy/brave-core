/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_ads/core/public/account/confirmations/confirmation_type.h"

#include "base/debug/crash_logging.h"
#include "base/notreached.h"
#include "base/types/cxx23_to_underlying.h"

namespace brave_ads {

namespace {

// Do not change the following string values as they are used for persisting and
// restoring state
constexpr char kUndefinedType[] = "";
constexpr char kClickedType[] = "click";
constexpr char kDismissedType[] = "dismiss";
constexpr char kViewedType[] = "view";
constexpr char kServedType[] = "served";
constexpr char kTransferredType[] = "landed";
constexpr char kSavedType[] = "bookmark";
constexpr char kFlaggedType[] = "flag";
constexpr char kUpvotedType[] = "upvote";
constexpr char kDownvotedType[] = "downvote";
constexpr char kConversionType[] = "conversion";

}  // namespace

ConfirmationType ParseConfirmationType(std::string_view value) {
  if (value == kUndefinedType) {
    return ConfirmationType::kUndefined;
  } else if (value == kClickedType) {
    return ConfirmationType::kClicked;
  } else if (value == kDismissedType) {
    return ConfirmationType::kDismissed;
  } else if (value == kViewedType) {
    return ConfirmationType::kViewed;
  } else if (value == kServedType) {
    return ConfirmationType::kServed;
  } else if (value == kTransferredType) {
    return ConfirmationType::kTransferred;
  } else if (value == kSavedType) {
    return ConfirmationType::kSaved;
  } else if (value == kFlaggedType) {
    return ConfirmationType::kFlagged;
  } else if (value == kUpvotedType) {
    return ConfirmationType::kUpvoted;
  } else if (value == kDownvotedType) {
    return ConfirmationType::kDownvoted;
  } else if (value == kConversionType) {
    return ConfirmationType::kConversion;
  }
  SCOPED_CRASH_KEY_STRING32("ConfirmationType", "value", value);
  NOTREACHED() << "Unexpected value for ConfirmationType: " << value;
  return ConfirmationType::kUndefined;
}

const char* ToString(ConfirmationType type) {
  switch (type) {
    case ConfirmationType::kUndefined:
      return kUndefinedType;
    case ConfirmationType::kClicked:
      return kClickedType;
    case ConfirmationType::kDismissed:
      return kDismissedType;
    case ConfirmationType::kViewed:
      return kViewedType;
    case ConfirmationType::kServed:
      return kServedType;
    case ConfirmationType::kTransferred:
      return kTransferredType;
    case ConfirmationType::kSaved:
      return kSavedType;
    case ConfirmationType::kFlagged:
      return kFlaggedType;
    case ConfirmationType::kUpvoted:
      return kUpvotedType;
    case ConfirmationType::kDownvoted:
      return kDownvotedType;
    case ConfirmationType::kConversion:
      return kConversionType;
  }

  NOTREACHED_NORETURN() << "Unexpected value for Value: "
                        << base::to_underlying(type);
}

std::ostream& operator<<(std::ostream& os, const ConfirmationType type) {
  os << ToString(type);
  return os;
}

}  // namespace brave_ads
