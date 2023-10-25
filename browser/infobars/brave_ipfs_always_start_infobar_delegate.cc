/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/infobars/brave_ipfs_always_start_infobar_delegate.h"
#include <memory>
#include "base/functional/callback_helpers.h"
#include "brave/components/ipfs/ipfs_constants.h"
#include "brave/components/ipfs/pref_names.h"
#include "brave/components/l10n/common/localization_util.h"
#include "brave/grit/brave_generated_resources.h"
#include "components/prefs/pref_service.h"
#include "chrome/browser/devtools/global_confirm_info_bar.h"
#include "brave/components/ipfs/ipfs_service.h"

// BraveIPFSAlwaysStartInfoBarDelegate
// static
void BraveIPFSAlwaysStartInfoBarDelegate::Create(ipfs::IpfsService* ipfs_service, PrefService* local_state) {
  if (const auto is_ipfs_local =
          (local_state->GetInteger(kIPFSResolveMethod) ==
           static_cast<int>(ipfs::IPFSResolveMethodTypes::IPFS_LOCAL));
      is_ipfs_local && local_state->GetBoolean(kIPFSAlwaysStartMode)) {
    return;
  }

  std::unique_ptr<ConfirmInfoBarDelegate> delegate(
      new BraveIPFSAlwaysStartInfoBarDelegate(ipfs_service, local_state));
  GlobalConfirmInfoBar::Show(std::move(delegate));
}

BraveIPFSAlwaysStartInfoBarDelegate::BraveIPFSAlwaysStartInfoBarDelegate(
    ipfs::IpfsService* ipfs_service,
    PrefService* local_state)
    : local_state_(local_state), ipfs_service_(ipfs_service) {}

BraveIPFSAlwaysStartInfoBarDelegate::~BraveIPFSAlwaysStartInfoBarDelegate() = default;

// ConfirmInfoBarDelegate
infobars::InfoBarDelegate::InfoBarIdentifier
BraveIPFSAlwaysStartInfoBarDelegate::GetIdentifier() const {
  return BRAVE_IPFS_ALWAYS_START_INFOBAR_DELEGATE;
}

std::u16string BraveIPFSAlwaysStartInfoBarDelegate::GetMessageText() const {
  return brave_l10n::GetLocalizedResourceUTF16String(
      IDS_BRAVE_IPFS_ALWAYS_START_INFOBAR_TEXT);
}

int BraveIPFSAlwaysStartInfoBarDelegate::GetButtons() const {
  return BUTTON_OK | BUTTON_CANCEL;
}

std::u16string BraveIPFSAlwaysStartInfoBarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  if (button == BUTTON_CANCEL) {
    return brave_l10n::GetLocalizedResourceUTF16String(
        IDS_BRAVE_IPFS_ALWAYS_START_INFOBAR_NO);
  }
  return brave_l10n::GetLocalizedResourceUTF16String(
      IDS_BRAVE_IPFS_ALWAYS_START_INFOBAR_OK);
}

bool BraveIPFSAlwaysStartInfoBarDelegate::Accept() {
  if (local_state_) {
    local_state_->SetBoolean(kIPFSAlwaysStartMode, true);
    if (!ipfs_service_->IsDaemonLaunched()) {
       ipfs_service_->LaunchDaemon(base::NullCallback());
    }
  }
  return true;
}

bool BraveIPFSAlwaysStartInfoBarDelegate::Cancel() {
  return true;
}