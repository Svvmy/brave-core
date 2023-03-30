/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_rewards/core/ledger_impl.h"

#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/types/always_false.h"
#include "brave/components/brave_rewards/core/common/legacy_callback_helpers.h"
#include "brave/components/brave_rewards/core/common/security_util.h"
#include "brave/components/brave_rewards/core/common/time_util.h"
#include "brave/components/brave_rewards/core/global_constants.h"
#include "brave/components/brave_rewards/core/legacy/static_values.h"
#include "brave/components/brave_rewards/core/publisher/publisher_status_helper.h"
#include "brave/components/brave_rewards/core/sku/sku_factory.h"

using std::placeholders::_1;

namespace {

bool testing() {
  return ledger::is_testing;
}

}  // namespace

namespace ledger {

LedgerImpl::LedgerImpl(
    mojo::PendingAssociatedReceiver<mojom::Ledger> ledger_receiver,
    mojo::PendingAssociatedRemote<mojom::RewardsService> rewards_service_remote)
    : receiver_(this, std::move(ledger_receiver)),
      rewards_service_(std::move(rewards_service_remote)),
      promotion_(std::make_unique<promotion::Promotion>(this)),
      publisher_(std::make_unique<publisher::Publisher>(this)),
      media_(std::make_unique<braveledger_media::Media>(this)),
      contribution_(std::make_unique<contribution::Contribution>(this)),
      wallet_(std::make_unique<wallet::Wallet>(this)),
      database_(std::make_unique<database::Database>(this)),
      report_(std::make_unique<report::Report>(this)),
      sku_(sku::SKUFactory::Create(this, sku::SKUType::kMerchant)),
      state_(std::make_unique<state::State>(this)),
      api_(std::make_unique<api::API>(this)),
      recovery_(std::make_unique<recovery::Recovery>(this)),
      bitflyer_(std::make_unique<bitflyer::Bitflyer>(this)),
      gemini_(std::make_unique<gemini::Gemini>(this)),
      uphold_(std::make_unique<uphold::Uphold>(this)) {
  DCHECK(base::ThreadPoolInstance::Get());
  set_ledger_client_for_logging(rewards_service_.get());
}

LedgerImpl::~LedgerImpl() = default;

void LedgerImpl::Initialize(bool execute_create_script,
                            InitializeCallback callback) {
  if (ready_state_ != ReadyState::kUninitialized) {
    BLOG(0, "Ledger already initializing");
    return std::move(callback).Run(ledger::mojom::Result::LEDGER_ERROR);
  }

  ready_state_ = ReadyState::kInitializing;
  InitializeDatabase(execute_create_script,
                     ledger::ToLegacyCallback(std::move(callback)));
}

void LedgerImpl::SetEnvironment(ledger::mojom::Environment environment) {
  DCHECK(IsUninitialized() || testing());
  ledger::_environment = environment;
}

void LedgerImpl::SetDebug(bool debug) {
  DCHECK(IsUninitialized() || testing());
  ledger::is_debug = debug;
}

void LedgerImpl::SetReconcileInterval(int32_t interval) {
  DCHECK(IsUninitialized() || testing());
  ledger::reconcile_interval = interval;
}

void LedgerImpl::SetRetryInterval(int32_t interval) {
  DCHECK(IsUninitialized() || testing());
  ledger::retry_interval = interval;
}

void LedgerImpl::SetTesting() {
  ledger::is_testing = true;
}

void LedgerImpl::SetStateMigrationTargetVersionForTesting(int32_t version) {
  ledger::state_migration_target_version_for_testing = version;
}

void LedgerImpl::GetEnvironment(GetEnvironmentCallback callback) {
  std::move(callback).Run(ledger::_environment);
}

void LedgerImpl::GetDebug(GetDebugCallback callback) {
  std::move(callback).Run(ledger::is_debug);
}

void LedgerImpl::GetReconcileInterval(GetReconcileIntervalCallback callback) {
  std::move(callback).Run(ledger::reconcile_interval);
}

void LedgerImpl::GetRetryInterval(GetRetryIntervalCallback callback) {
  std::move(callback).Run(ledger::retry_interval);
}

void LedgerImpl::CreateRewardsWallet(const std::string& country,
                                     CreateRewardsWalletCallback callback) {
  WhenReady([this, country, callback = std::move(callback)]() mutable {
    wallet()->CreateWalletIfNecessary(
        country.empty() ? absl::nullopt
                        : absl::optional<std::string>(std::move(country)),
        std::move(callback));
  });
}

void LedgerImpl::GetRewardsParameters(GetRewardsParametersCallback callback) {
  WhenReady([this, callback = std::move(callback)]() mutable {
    auto params = state()->GetRewardsParameters();
    if (params->rate == 0.0) {
      // A rate of zero indicates that the rewards parameters have
      // not yet been successfully initialized from the server.
      BLOG(1, "Rewards parameters not set - fetching from server");
      api()->FetchParameters(std::move(callback));
      return;
    }

    std::move(callback).Run(std::move(params));
  });
}

void LedgerImpl::GetAutoContributeProperties(
    GetAutoContributePropertiesCallback callback) {
  if (!IsReady()) {
    return std::move(callback).Run(
        ledger::mojom::AutoContributeProperties::New());
  }

  auto props = ledger::mojom::AutoContributeProperties::New();
  props->enabled_contribute = state()->GetAutoContributeEnabled();
  props->amount = state()->GetAutoContributionAmount();
  props->contribution_min_time = state()->GetPublisherMinVisitTime();
  props->contribution_min_visits = state()->GetPublisherMinVisits();
  props->contribution_non_verified = state()->GetPublisherAllowNonVerified();
  props->reconcile_stamp = state()->GetReconcileStamp();
  std::move(callback).Run(std::move(props));
}

void LedgerImpl::GetPublisherMinVisitTime(
    GetPublisherMinVisitTimeCallback callback) {
  if (!IsReady()) {
    return std::move(callback).Run(0);
  }

  std::move(callback).Run(state()->GetPublisherMinVisitTime());
}

void LedgerImpl::GetPublisherMinVisits(GetPublisherMinVisitsCallback callback) {
  if (!IsReady()) {
    return std::move(callback).Run(0);
  }

  std::move(callback).Run(state()->GetPublisherMinVisits());
}

void LedgerImpl::GetPublisherAllowNonVerified(
    GetPublisherAllowNonVerifiedCallback callback) {
  if (!IsReady()) {
    return std::move(callback).Run(false);
  }

  std::move(callback).Run(state()->GetPublisherAllowNonVerified());
}

void LedgerImpl::GetAutoContributeEnabled(
    GetAutoContributeEnabledCallback callback) {
  if (!IsReady()) {
    return std::move(callback).Run(false);
  }

  std::move(callback).Run(state()->GetAutoContributeEnabled());
}

void LedgerImpl::GetReconcileStamp(GetReconcileStampCallback callback) {
  if (!IsReady()) {
    return std::move(callback).Run(0);
  }

  std::move(callback).Run(state()->GetReconcileStamp());
}

void LedgerImpl::OnLoad(ledger::mojom::VisitDataPtr visit_data,
                        uint64_t current_time) {
  if (!IsReady() || !visit_data || visit_data->domain.empty()) {
    return;
  }

  auto iter = current_pages_.find(visit_data->tab_id);
  if (iter != current_pages_.end() &&
      iter->second.domain == visit_data->domain) {
    return;
  }

  if (last_shown_tab_id_ == visit_data->tab_id) {
    last_tab_active_time_ = current_time;
  }

  current_pages_[visit_data->tab_id] = *visit_data;
}

void LedgerImpl::OnUnload(uint32_t tab_id, uint64_t current_time) {
  if (!IsReady()) {
    return;
  }

  OnHide(tab_id, current_time);
  auto iter = current_pages_.find(tab_id);
  if (iter != current_pages_.end()) {
    current_pages_.erase(iter);
  }
}

void LedgerImpl::OnShow(uint32_t tab_id, uint64_t current_time) {
  if (!IsReady()) {
    return;
  }

  last_tab_active_time_ = current_time;
  last_shown_tab_id_ = tab_id;
}

void LedgerImpl::OnHide(uint32_t tab_id, uint64_t current_time) {
  if (!IsReady()) {
    return;
  }

  if (tab_id != last_shown_tab_id_ || last_tab_active_time_ == 0) {
    return;
  }

  auto iter = current_pages_.find(tab_id);
  if (iter == current_pages_.end()) {
    return;
  }

  const std::string type = media()->GetLinkType(iter->second.domain, "", "");
  uint64_t duration = current_time - last_tab_active_time_;
  last_tab_active_time_ = 0;

  if (type == GITHUB_MEDIA_TYPE) {
    base::flat_map<std::string, std::string> parts;
    parts["duration"] = std::to_string(duration);
    media()->ProcessMedia(parts, type, iter->second.Clone());
    return;
  }

  publisher()->SaveVisit(
      iter->second.domain, iter->second, duration, true, 0,
      [](ledger::mojom::Result, ledger::mojom::PublisherInfoPtr) {});
}

void LedgerImpl::OnForeground(uint32_t tab_id, uint64_t current_time) {
  if (!IsReady()) {
    return;
  }

  if (last_shown_tab_id_ != tab_id) {
    return;
  }

  OnShow(tab_id, current_time);
}

void LedgerImpl::OnBackground(uint32_t tab_id, uint64_t current_time) {
  if (!IsReady()) {
    return;
  }

  OnHide(tab_id, current_time);
}

void LedgerImpl::OnXHRLoad(
    uint32_t tab_id,
    const std::string& url,
    const base::flat_map<std::string, std::string>& parts,
    const std::string& first_party_url,
    const std::string& referrer,
    ledger::mojom::VisitDataPtr visit_data) {
  if (!IsReady()) {
    return;
  }

  std::string type = media()->GetLinkType(url, first_party_url, referrer);
  if (type.empty()) {
    return;
  }
  media()->ProcessMedia(parts, type, std::move(visit_data));
}

void LedgerImpl::SetPublisherExclude(const std::string& publisher_key,
                                     ledger::mojom::PublisherExclude exclude,
                                     SetPublisherExcludeCallback callback) {
  WhenReady(
      [this, publisher_key, exclude, callback = std::move(callback)]() mutable {
        publisher()->SetPublisherExclude(publisher_key, exclude,
                                         std::move(callback));
      });
}

void LedgerImpl::RestorePublishers(RestorePublishersCallback callback) {
  WhenReady([this, callback = std::move(callback)]() mutable {
    database()->RestorePublishers(std::move(callback));
  });
}

void LedgerImpl::FetchPromotions(FetchPromotionsCallback callback) {
  WhenReady([this, callback = std::move(callback)]() mutable {
    promotion()->Fetch(std::move(callback));
  });
}

void LedgerImpl::ClaimPromotion(const std::string& promotion_id,
                                const std::string& payload,
                                ClaimPromotionCallback callback) {
  WhenReady(
      [this, promotion_id, payload, callback = std::move(callback)]() mutable {
        promotion()->Claim(promotion_id, payload, std::move(callback));
      });
}

void LedgerImpl::AttestPromotion(const std::string& promotion_id,
                                 const std::string& solution,
                                 AttestPromotionCallback callback) {
  WhenReady(
      [this, promotion_id, solution, callback = std::move(callback)]() mutable {
        promotion()->Attest(promotion_id, solution, std::move(callback));
      });
}

void LedgerImpl::SetPublisherMinVisitTime(int duration_in_seconds) {
  WhenReady([this, duration_in_seconds]() {
    state()->SetPublisherMinVisitTime(duration_in_seconds);
  });
}

void LedgerImpl::SetPublisherMinVisits(int visits) {
  WhenReady([this, visits]() { state()->SetPublisherMinVisits(visits); });
}

void LedgerImpl::SetPublisherAllowNonVerified(bool allow) {
  WhenReady([this, allow]() { state()->SetPublisherAllowNonVerified(allow); });
}

void LedgerImpl::SetAutoContributionAmount(double amount) {
  WhenReady([this, amount]() { state()->SetAutoContributionAmount(amount); });
}

void LedgerImpl::SetAutoContributeEnabled(bool enabled) {
  WhenReady([this, enabled]() { state()->SetAutoContributeEnabled(enabled); });
}

void LedgerImpl::GetBalanceReport(ledger::mojom::ActivityMonth month,
                                  int32_t year,
                                  GetBalanceReportCallback callback) {
  WhenReady(
      [this, month, year,
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        database()->GetBalanceReportInfo(month, year, std::move(callback));
      });
}

void LedgerImpl::GetPublisherActivityFromUrl(
    uint64_t window_id,
    ledger::mojom::VisitDataPtr visit_data,
    const std::string& publisher_blob) {
  WhenReady([this, window_id, visit_data = std::move(visit_data),
             publisher_blob]() mutable {
    publisher()->GetPublisherActivityFromUrl(window_id, std::move(visit_data),
                                             publisher_blob);
  });
}

void LedgerImpl::GetAutoContributionAmount(
    GetAutoContributionAmountCallback callback) {
  if (!IsReady()) {
    return std::move(callback).Run(0);
  }

  std::move(callback).Run(state()->GetAutoContributionAmount());
}

void LedgerImpl::GetPublisherBanner(const std::string& publisher_id,
                                    GetPublisherBannerCallback callback) {
  WhenReady(
      [this, publisher_id,
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        publisher()->GetPublisherBanner(publisher_id, std::move(callback));
      });
}

void LedgerImpl::OneTimeTip(const std::string& publisher_key,
                            double amount,
                            OneTimeTipCallback callback) {
  WhenReady([this, publisher_key, amount,
             callback = ledger::ToLegacyCallback(std::move(callback))]() {
    contribution()->OneTimeTip(publisher_key, amount, std::move(callback));
  });
}

void LedgerImpl::RemoveRecurringTip(const std::string& publisher_key,
                                    RemoveRecurringTipCallback callback) {
  WhenReady(
      [this, publisher_key,
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        database()->RemoveRecurringTip(publisher_key, std::move(callback));
      });
}

void LedgerImpl::GetCreationStamp(GetCreationStampCallback callback) {
  if (!IsReady()) {
    return std::move(callback).Run(0);
  }

  std::move(callback).Run(state()->GetCreationStamp());
}

void LedgerImpl::GetRewardsInternalsInfo(
    GetRewardsInternalsInfoCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(std::move(callback))]() {
    auto info = ledger::mojom::RewardsInternalsInfo::New();

    ledger::mojom::RewardsWalletPtr wallet = wallet_->GetWallet();
    if (!wallet) {
      BLOG(0, "Wallet is null");
      callback(std::move(info));
      return;
    }

    // Retrieve the payment id.
    info->payment_id = wallet->payment_id;

    // Retrieve the boot stamp.
    info->boot_stamp = state()->GetCreationStamp();

    // Retrieve the key info seed and validate it.
    if (!ledger::util::Security::IsSeedValid(wallet->recovery_seed)) {
      info->is_key_info_seed_valid = false;
    } else {
      std::vector<uint8_t> secret_key =
          ledger::util::Security::GetHKDF(wallet->recovery_seed);
      std::vector<uint8_t> public_key;
      std::vector<uint8_t> new_secret_key;
      info->is_key_info_seed_valid =
          ledger::util::Security::GetPublicKeyFromSeed(secret_key, &public_key,
                                                       &new_secret_key);
    }

    callback(std::move(info));
  });
}

void LedgerImpl::SaveRecurringTip(ledger::mojom::RecurringTipPtr info,
                                  SaveRecurringTipCallback callback) {
  WhenReady([this, info = std::move(info),
             callback =
                 ledger::ToLegacyCallback(std::move(callback))]() mutable {
    database()->SaveRecurringTip(
        std::move(info),
        [this, callback = std::move(callback)](ledger::mojom::Result result) {
          contribution()->SetMonthlyContributionTimer();
          callback(result);
        });
  });
}

void LedgerImpl::SendContribution(const std::string& publisher_id,
                                  double amount,
                                  bool set_monthly,
                                  SendContributionCallback callback) {
  WhenReady([this, publisher_id, amount, set_monthly,
             callback = std::move(callback)]() mutable {
    contribution()->SendContribution(publisher_id, amount, set_monthly,
                                     std::move(callback));
  });
}

void LedgerImpl::GetRecurringTips(GetRecurringTipsCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(
                       std::move(callback))]() mutable {
    contribution()->GetRecurringTips(std::move(callback));
  });
}

void LedgerImpl::GetOneTimeTips(GetOneTimeTipsCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(
                       std::move(callback))]() mutable {
    database()->GetOneTimeTips(ledger::util::GetCurrentMonth(),
                               ledger::util::GetCurrentYear(),
                               std::move(callback));
  });
}

void LedgerImpl::GetActivityInfoList(
    uint32_t start,
    uint32_t limit,
    ledger::mojom::ActivityInfoFilterPtr filter,
    GetActivityInfoListCallback callback) {
  WhenReady(
      [this, start, limit, filter = std::move(filter),
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        database()->GetActivityInfoList(start, limit, std::move(filter),
                                        std::move(callback));
      });
}

void LedgerImpl::GetPublishersVisitedCount(
    GetPublishersVisitedCountCallback callback) {
  WhenReady([this, callback = std::move(callback)]() mutable {
    database()->GetPublishersVisitedCount(std::move(callback));
  });
}

void LedgerImpl::GetExcludedList(GetExcludedListCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(
                       std::move(callback))]() mutable {
    database()->GetExcludedList(std::move(callback));
  });
}

void LedgerImpl::RefreshPublisher(const std::string& publisher_key,
                                  RefreshPublisherCallback callback) {
  WhenReady(
      [this, publisher_key,
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        publisher()->RefreshPublisher(publisher_key, std::move(callback));
      });
}

void LedgerImpl::StartContributionsForTesting() {
  WhenReady([this]() {
    contribution()->StartContributionsForTesting();  // IN-TEST
  });
}

void LedgerImpl::UpdateMediaDuration(uint64_t window_id,
                                     const std::string& publisher_key,
                                     uint64_t duration,
                                     bool first_visit) {
  WhenReady([this, window_id, publisher_key, duration, first_visit]() {
    publisher()->UpdateMediaDuration(window_id, publisher_key, duration,
                                     first_visit);
  });
}

void LedgerImpl::IsPublisherRegistered(const std::string& publisher_id,
                                       IsPublisherRegisteredCallback callback) {
  WhenReady([this, publisher_id,
             callback =
                 ledger::ToLegacyCallback(std::move(callback))]() mutable {
    publisher()->GetServerPublisherInfo(
        publisher_id, true /* use_prefix_list */,
        [callback =
             std::move(callback)](ledger::mojom::ServerPublisherInfoPtr info) {
          callback(info && info->status !=
                               ledger::mojom::PublisherStatus::NOT_VERIFIED);
        });
  });
}

void LedgerImpl::GetPublisherInfo(const std::string& publisher_key,
                                  GetPublisherInfoCallback callback) {
  WhenReady(
      [this, publisher_key,
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        database()->GetPublisherInfo(publisher_key, std::move(callback));
      });
}

void LedgerImpl::GetPublisherPanelInfo(const std::string& publisher_key,
                                       GetPublisherPanelInfoCallback callback) {
  WhenReady(
      [this, publisher_key,
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        publisher()->GetPublisherPanelInfo(publisher_key, std::move(callback));
      });
}

void LedgerImpl::SavePublisherInfo(
    uint64_t window_id,
    ledger::mojom::PublisherInfoPtr publisher_info,
    SavePublisherInfoCallback callback) {
  WhenReady(
      [this, window_id, info = std::move(publisher_info),
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        publisher()->SavePublisherInfo(window_id, std::move(info),
                                       std::move(callback));
      });
}

void LedgerImpl::SetInlineTippingPlatformEnabled(
    ledger::mojom::InlineTipsPlatforms platform,
    bool enabled) {
  WhenReady([this, platform, enabled]() {
    state()->SetInlineTippingPlatformEnabled(platform, enabled);
  });
}

void LedgerImpl::GetInlineTippingPlatformEnabled(
    ledger::mojom::InlineTipsPlatforms platform,
    GetInlineTippingPlatformEnabledCallback callback) {
  if (!IsReady()) {
    return std::move(callback).Run(false);
  }

  std::move(callback).Run(state()->GetInlineTippingPlatformEnabled(platform));
}

void LedgerImpl::GetShareURL(
    const base::flat_map<std::string, std::string>& args,
    GetShareURLCallback callback) {
  if (!IsReady()) {
    return std::move(callback).Run("");
  }

  std::move(callback).Run(publisher()->GetShareURL(args));
}

void LedgerImpl::GetPendingContributions(
    GetPendingContributionsCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(
                       std::move(callback))]() mutable {
    database()->GetPendingContributions(
        [this, callback = std::move(callback)](
            std::vector<ledger::mojom::PendingContributionInfoPtr>
                list) mutable {
          // The publisher status field may be expired. Attempt to refresh
          // expired publisher status values before executing callback.
          ledger::publisher::RefreshPublisherStatus(this, std::move(list),
                                                    std::move(callback));
        });
  });
}

void LedgerImpl::RemovePendingContribution(
    uint64_t id,
    RemovePendingContributionCallback callback) {
  WhenReady(
      [this, id,
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        database()->RemovePendingContribution(id, std::move(callback));
      });
}

void LedgerImpl::RemoveAllPendingContributions(
    RemovePendingContributionCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(
                       std::move(callback))]() mutable {
    database()->RemoveAllPendingContributions(std::move(callback));
  });
}

void LedgerImpl::GetPendingContributionsTotal(
    GetPendingContributionsTotalCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(
                       std::move(callback))]() mutable {
    database()->GetPendingContributionsTotal(std::move(callback));
  });
}

void LedgerImpl::FetchBalance(FetchBalanceCallback callback) {
  WhenReady([this, callback = std::move(callback)]() mutable {
    wallet()->FetchBalance(std::move(callback));
  });
}

void LedgerImpl::GetExternalWallet(const std::string& wallet_type,
                                   GetExternalWalletCallback callback) {
  WhenReady([this, wallet_type, callback = std::move(callback)]() mutable {
    if (wallet_type == ledger::constant::kWalletBitflyer) {
      return bitflyer()->GetWallet(std::move(callback));
    }

    if (wallet_type == ledger::constant::kWalletGemini) {
      return gemini()->GetWallet(std::move(callback));
    }

    if (wallet_type == ledger::constant::kWalletUphold) {
      return uphold()->GetWallet(std::move(callback));
    }

    NOTREACHED() << "Unknown external wallet type!";
    std::move(callback).Run(
        base::unexpected(ledger::mojom::GetExternalWalletError::kUnexpected));
  });
}

void LedgerImpl::ConnectExternalWallet(
    const std::string& wallet_type,
    const base::flat_map<std::string, std::string>& args,
    ConnectExternalWalletCallback callback) {
  WhenReady(
      [this, wallet_type, args, callback = std::move(callback)]() mutable {
        if (wallet_type == ledger::constant::kWalletBitflyer) {
          return bitflyer()->ConnectWallet(args, std::move(callback));
        }

        if (wallet_type == ledger::constant::kWalletGemini) {
          return gemini()->ConnectWallet(args, std::move(callback));
        }

        if (wallet_type == ledger::constant::kWalletUphold) {
          return uphold()->ConnectWallet(args, std::move(callback));
        }

        NOTREACHED() << "Unknown external wallet type!";
        std::move(callback).Run(base::unexpected(
            ledger::mojom::ConnectExternalWalletError::kUnexpected));
      });
}

void LedgerImpl::GetTransactionReport(ledger::mojom::ActivityMonth month,
                                      int year,
                                      GetTransactionReportCallback callback) {
  WhenReady(
      [this, month, year,
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        database()->GetTransactionReport(month, year, std::move(callback));
      });
}

void LedgerImpl::GetContributionReport(ledger::mojom::ActivityMonth month,
                                       int year,
                                       GetContributionReportCallback callback) {
  WhenReady(
      [this, month, year,
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        database()->GetContributionReport(month, year, std::move(callback));
      });
}

void LedgerImpl::GetAllContributions(GetAllContributionsCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(
                       std::move(callback))]() mutable {
    database()->GetAllContributions(std::move(callback));
  });
}

void LedgerImpl::SavePublisherInfoForTip(
    ledger::mojom::PublisherInfoPtr info,
    SavePublisherInfoForTipCallback callback) {
  WhenReady(
      [this, info = std::move(info),
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        database()->SavePublisherInfo(std::move(info), std::move(callback));
      });
}

void LedgerImpl::GetMonthlyReport(ledger::mojom::ActivityMonth month,
                                  int year,
                                  GetMonthlyReportCallback callback) {
  WhenReady(
      [this, month, year,
       callback = ledger::ToLegacyCallback(std::move(callback))]() mutable {
        report()->GetMonthly(month, year, std::move(callback));
      });
}

void LedgerImpl::GetAllMonthlyReportIds(
    GetAllMonthlyReportIdsCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(
                       std::move(callback))]() mutable {
    report()->GetAllMonthlyIds(std::move(callback));
  });
}

void LedgerImpl::GetAllPromotions(GetAllPromotionsCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(
                       std::move(callback))]() mutable {
    database()->GetAllPromotions(std::move(callback));
  });
}

void LedgerImpl::Shutdown(ShutdownCallback callback) {
  if (!IsReady()) {
    return std::move(callback).Run(ledger::mojom::Result::LEDGER_ERROR);
  }

  ready_state_ = ReadyState::kShuttingDown;
  rewards_service_->ClearAllNotifications();

  database()->FinishAllInProgressContributions(
      std::bind(&LedgerImpl::OnAllDone, this, _1,
                ledger::ToLegacyCallback(std::move(callback))));
}

void LedgerImpl::GetEventLogs(GetEventLogsCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(
                       std::move(callback))]() mutable {
    database()->GetLastEventLogs(std::move(callback));
  });
}

void LedgerImpl::GetRewardsWallet(GetRewardsWalletCallback callback) {
  WhenReady([this, callback = ledger::ToLegacyCallback(std::move(callback))]() {
    auto rewards_wallet = wallet()->GetWallet();
    if (rewards_wallet) {
      // While the wallet creation flow is running, the Rewards wallet data may
      // have a recovery seed without a payment ID. Only return a struct to the
      // caller if it contains a payment ID.
      if (rewards_wallet->payment_id.empty()) {
        rewards_wallet = nullptr;
      }
    }
    callback(std::move(rewards_wallet));
  });
}

// -----------------------

mojom::RewardsService* LedgerImpl::rewards_service() const {
  return rewards_service_.get();
}

ledger::state::State* LedgerImpl::state() const {
  return state_.get();
}

ledger::promotion::Promotion* LedgerImpl::promotion() const {
  return promotion_.get();
}

ledger::publisher::Publisher* LedgerImpl::publisher() const {
  return publisher_.get();
}

braveledger_media::Media* LedgerImpl::media() const {
  return media_.get();
}

ledger::contribution::Contribution* LedgerImpl::contribution() const {
  return contribution_.get();
}

ledger::wallet::Wallet* LedgerImpl::wallet() const {
  return wallet_.get();
}

ledger::report::Report* LedgerImpl::report() const {
  return report_.get();
}

ledger::sku::SKU* LedgerImpl::sku() const {
  return sku_.get();
}

ledger::api::API* LedgerImpl::api() const {
  return api_.get();
}

ledger::database::Database* LedgerImpl::database() const {
  return database_.get();
}

ledger::bitflyer::Bitflyer* LedgerImpl::bitflyer() const {
  return bitflyer_.get();
}

ledger::gemini::Gemini* LedgerImpl::gemini() const {
  return gemini_.get();
}

ledger::uphold::Uphold* LedgerImpl::uphold() const {
  return uphold_.get();
}

template <typename LoadURLCallback>
void LedgerImpl::LoadURLImpl(ledger::mojom::UrlRequestPtr request,
                             LoadURLCallback callback) {
  DCHECK(request);
  if (IsShuttingDown()) {
    BLOG(1, request->url + " will not be executed as we are shutting down");
    return;
  }

  if (!request->skip_log) {
    BLOG(5, UrlRequestToString(request->url, request->headers, request->content,
                               request->content_type, request->method));
  }

  if constexpr (std::is_same_v<LoadURLCallback, LegacyLoadURLCallback>) {
    rewards_service_->LoadURL(
        std::move(request),
        base::BindOnce(
            [](LegacyLoadURLCallback callback, mojom::UrlResponsePtr response) {
              callback(std::move(response));
            },
            std::move(callback)));
  } else if constexpr (std::is_same_v<LoadURLCallback,  // NOLINT
                                      ledger::LoadURLCallback>) {
    rewards_service_->LoadURL(std::move(request), std::move(callback));
  } else {
    static_assert(base::AlwaysFalse<LoadURLCallback>,
                  "LoadURLCallback must be either "
                  "ledger::LegacyLoadURLCallback, or "
                  "ledger::LoadURLCallback!");
  }
}

void LedgerImpl::LoadURL(ledger::mojom::UrlRequestPtr request,
                         ledger::LegacyLoadURLCallback callback) {
  LoadURLImpl(std::move(request), std::move(callback));
}

void LedgerImpl::LoadURL(ledger::mojom::UrlRequestPtr request,
                         ledger::LoadURLCallback callback) {
  LoadURLImpl(std::move(request), std::move(callback));
}

template <typename RunDBTransactionCallback>
void LedgerImpl::RunDBTransactionImpl(
    ledger::mojom::DBTransactionPtr transaction,
    RunDBTransactionCallback callback) {
  if constexpr (std::is_same_v<  // NOLINT
                    RunDBTransactionCallback,
                    ledger::LegacyRunDBTransactionCallback>) {
    rewards_service_->RunDBTransaction(
        std::move(transaction),
        base::BindOnce(
            [](ledger::LegacyRunDBTransactionCallback callback,
               mojom::DBCommandResponsePtr response) {
              callback(std::move(response));
            },
            std::move(callback)));
  } else if constexpr (std::is_same_v<  // NOLINT
                           RunDBTransactionCallback,
                           ledger::RunDBTransactionCallback>) {
    rewards_service_->RunDBTransaction(std::move(transaction),
                                       std::move(callback));
  } else {
    static_assert(base::AlwaysFalse<RunDBTransactionCallback>,
                  "RunDBTransactionCallback must be either "
                  "ledger::LegacyRunDBTransactionCallback, or "
                  "ledger::RunDBTransactionCallback!");
  }
}

void LedgerImpl::RunDBTransaction(
    ledger::mojom::DBTransactionPtr transaction,
    ledger::LegacyRunDBTransactionCallback callback) {
  RunDBTransactionImpl(std::move(transaction), std::move(callback));
}

void LedgerImpl::RunDBTransaction(ledger::mojom::DBTransactionPtr transaction,
                                  ledger::RunDBTransactionCallback callback) {
  RunDBTransactionImpl(std::move(transaction), std::move(callback));
}

bool LedgerImpl::IsShuttingDown() const {
  return ready_state_ == ReadyState::kShuttingDown;
}

bool LedgerImpl::IsUninitialized() const {
  return ready_state_ == ReadyState::kUninitialized;
}

bool LedgerImpl::IsReady() const {
  return ready_state_ == ReadyState::kReady;
}

void LedgerImpl::InitializeDatabase(bool execute_create_script,
                                    ledger::LegacyResultCallback callback) {
  DCHECK(ready_state_ == ReadyState::kInitializing);

  ledger::LegacyResultCallback finish_callback =
      std::bind(&LedgerImpl::OnInitialized, this, _1, std::move(callback));

  auto database_callback =
      std::bind(&LedgerImpl::OnDatabaseInitialized, this, _1, finish_callback);
  database()->Initialize(execute_create_script, database_callback);
}

void LedgerImpl::OnDatabaseInitialized(ledger::mojom::Result result,
                                       ledger::LegacyResultCallback callback) {
  DCHECK(ready_state_ == ReadyState::kInitializing);

  if (result != ledger::mojom::Result::LEDGER_OK) {
    BLOG(0, "Database could not be initialized. Error: " << result);
    callback(result);
    return;
  }

  state()->Initialize(base::BindOnce(&LedgerImpl::OnStateInitialized,
                                     base::Unretained(this),
                                     std::move(callback)));
}

void LedgerImpl::OnStateInitialized(ledger::LegacyResultCallback callback,
                                    ledger::mojom::Result result) {
  DCHECK(ready_state_ == ReadyState::kInitializing);

  if (result != ledger::mojom::Result::LEDGER_OK) {
    BLOG(0, "Failed to initialize state");
    return;
  }

  callback(ledger::mojom::Result::LEDGER_OK);
}

void LedgerImpl::OnInitialized(ledger::mojom::Result result,
                               ledger::LegacyResultCallback callback) {
  DCHECK(ready_state_ == ReadyState::kInitializing);

  if (result == ledger::mojom::Result::LEDGER_OK) {
    StartServices();
  } else {
    BLOG(0, "Failed to initialize wallet " << result);
  }

  while (!ready_callbacks_.empty()) {
    auto ready_callback = std::move(ready_callbacks_.front());
    ready_callbacks_.pop();
    ready_callback();
  }

  ready_state_ = ReadyState::kReady;

  callback(result);
}

void LedgerImpl::StartServices() {
  DCHECK(ready_state_ == ReadyState::kInitializing);

  publisher()->SetPublisherServerListTimer();
  contribution()->SetAutoContributeTimer();
  contribution()->SetMonthlyContributionTimer();
  promotion()->Refresh(false);
  contribution()->Initialize();
  promotion()->Initialize();
  api()->Initialize();
  recovery_->Check();
}

void LedgerImpl::OnAllDone(ledger::mojom::Result result,
                           ledger::LegacyResultCallback callback) {
  database()->Close(std::move(callback));
}

template <typename T>
void LedgerImpl::WhenReady(T callback) {
  switch (ready_state_) {
    case ReadyState::kReady:
      callback();
      break;
    case ReadyState::kShuttingDown:
      NOTREACHED();
      break;
    default:
      ready_callbacks_.push(std::function<void()>(
          [shared_callback = std::make_shared<T>(std::move(callback))]() {
            (*shared_callback)();
          }));
      break;
  }
}

}  // namespace ledger
