// Copyright (c) 2023 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/ios/browser/api/ai_chat/ai_chat.h"
#include "ai_chat.mojom.objc+private.h"
#include "brave/base/mac/conversions.h"
#include "brave/components/ai_chat/core/browser/conversation_driver.h"
#include "brave/ios/browser/skus/skus_service_factory.h"

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/sys_string_conversions.h"
#include "brave/components/ai_chat/core/browser/ai_chat_metrics.h"
#include "brave/components/ai_chat/core/browser/constants.h"
#include "brave/components/ai_chat/core/common/features.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom-shared.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom.h"
#include "brave/components/ai_chat/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "ios/chrome/browser/shared/model/application_context/application_context.h"
#include "ios/chrome/browser/shared/model/browser_state/chrome_browser_state.h"
#include "net/base/mac/url_conversions.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace ai_chat {
class AIChatConversationDriverIOS : public ConversationDriver,
                                    ConversationDriver::Observer {
 public:
  AIChatConversationDriverIOS(id<AIChatDelegateIOS> delegate,
                              ChromeBrowserState* browser_state,
                              raw_ptr<AIChatMetrics> ai_chat_metrics);
  ~AIChatConversationDriverIOS() override;

  bool HasUserOptedIn() const;
  void SetUserOptedIn(bool opted_in);

 protected:
  GURL GetPageURL() const override;
  void GetPageContent(base::OnceCallback<void(std::string, bool is_video)>
                          callback) const override;
  bool HasPrimaryMainFrame() const override;
  bool IsDocumentOnLoadCompletedInPrimaryMainFrame() const override;

  // Observer
  void OnHistoryUpdate() override;
  void OnAPIRequestInProgress(bool in_progress) override;
  void OnAPIResponseError(ai_chat::mojom::APIError error) override;
  void OnSuggestedQuestionsChanged(
      std::vector<std::string> questions,
      bool has_generated,
      ai_chat::mojom::AutoGenerateQuestionsPref auto_generate) override;

 private:
  base::RepeatingCallback<mojo::PendingRemote<skus::mojom::SkusService>()>
  GetSkusService(ChromeBrowserState* browser_state);

  id<AIChatDelegateIOS> bridge_;
  ChromeBrowserState* browser_state_;
  base::ScopedObservation<AIChatConversationDriverIOS,
                          AIChatConversationDriverIOS::Observer>
      chat_driver_observation_{this};
  base::WeakPtrFactory<AIChatConversationDriverIOS> weak_ptr_factory_{this};
};

AIChatConversationDriverIOS::AIChatConversationDriverIOS(
    id<AIChatDelegateIOS> delegate,
    ChromeBrowserState* browser_state,
    raw_ptr<AIChatMetrics> ai_chat_metrics)
    : ConversationDriver(user_prefs::UserPrefs::Get(browser_state),
                         ai_chat_metrics,
                         std::make_unique<AIChatCredentialManager>(
                             GetSkusService(browser_state),
                             GetApplicationContext()->GetLocalState()),
                         browser_state->GetSharedURLLoaderFactory()),
      bridge_(delegate),
      browser_state_(browser_state) {
  chat_driver_observation_.Observe(this);
}

AIChatConversationDriverIOS::~AIChatConversationDriverIOS() = default;

bool AIChatConversationDriverIOS::HasUserOptedIn() const {
  auto* pref_service = user_prefs::UserPrefs::Get(browser_state_);
  base::Time last_accepted_disclaimer =
      pref_service->GetTime(ai_chat::prefs::kLastAcceptedDisclaimer);
  return !last_accepted_disclaimer.is_null();
}

void AIChatConversationDriverIOS::SetUserOptedIn(bool opted_in) {
  auto* pref_service = user_prefs::UserPrefs::Get(browser_state_);

  if (opted_in) {
    pref_service->SetTime(ai_chat::prefs::kLastAcceptedDisclaimer,
                          base::Time::Now());
  } else {
    pref_service->ClearPref(ai_chat::prefs::kLastAcceptedDisclaimer);
  }
}

GURL AIChatConversationDriverIOS::GetPageURL() const {
  NSURL* url = [bridge_ getLastCommittedURL];
  if (url) {
    return net::GURLWithNSURL(url);
  }
  return GURL();
}

void AIChatConversationDriverIOS::GetPageContent(
    base::OnceCallback<void(std::string, bool is_video)> callback) const {
  [bridge_
      getPageContentWithCompletion:[callback =
                                        std::make_shared<decltype(callback)>(
                                            std::move(callback))](
                                       NSString* content, bool isVideo) {
        if (callback) {
          std::move(*callback).Run(base::SysNSStringToUTF8(content), isVideo);
        }
      }];
}

bool AIChatConversationDriverIOS::HasPrimaryMainFrame() const {
  return true;
}

bool AIChatConversationDriverIOS::IsDocumentOnLoadCompletedInPrimaryMainFrame()
    const {
  return true;
}

base::RepeatingCallback<mojo::PendingRemote<skus::mojom::SkusService>()>
AIChatConversationDriverIOS::GetSkusService(ChromeBrowserState* browser_state) {
  return base::BindRepeating(
      [](ChromeBrowserState* browser_state) {
        return skus::SkusServiceFactory::GetForBrowserState(browser_state);
      },
      base::Unretained(browser_state));
}

// MARK: - ConversationDriver::Observer

void AIChatConversationDriverIOS::OnHistoryUpdate() {
  [bridge_ onHistoryUpdate];
}

void AIChatConversationDriverIOS::OnAPIRequestInProgress(bool in_progress) {
  [bridge_ onAPIRequestInProgress:in_progress];
}

void AIChatConversationDriverIOS::OnAPIResponseError(
    ai_chat::mojom::APIError error) {
  [bridge_ onAPIResponseError:(AiChatAPIError)error];
}

void AIChatConversationDriverIOS::OnSuggestedQuestionsChanged(
    std::vector<std::string> questions,
    bool has_generated,
    ai_chat::mojom::AutoGenerateQuestionsPref auto_generate) {
  [bridge_ onSuggestedQuestionsChanged:brave::vector_to_ns(questions)
                          hasGenerated:has_generated
                          autoGenerate:auto_generate ==
                                       ai_chat::mojom::
                                           AutoGenerateQuestionsPref::Enabled];
}

}  // namespace ai_chat

@interface AIChat () {
  std::unique_ptr<ai_chat::AIChatMetrics> ai_chat_metrics_;
  std::unique_ptr<ai_chat::AIChatConversationDriverIOS> driver_;
}
@end

@implementation AIChat
- (instancetype)initWithChromeBrowserState:(ChromeBrowserState*)browserState
                                  delegate:
                                      (__weak id<AIChatDelegateIOS>)delegate {
  if ((self = [super init])) {
    ai_chat_metrics_ = std::make_unique<ai_chat::AIChatMetrics>(
        GetApplicationContext()->GetLocalState());
    driver_ = std::make_unique<ai_chat::AIChatConversationDriverIOS>(
        delegate, browserState, ai_chat_metrics_.get());
  }
  return self;
}

- (bool)hasUserOptedIn {
  return driver_->HasUserOptedIn();
}

- (void)setHasUserOptedIn:(bool)hasUserOptedIn {
  driver_->SetUserOptedIn(hasUserOptedIn);
}

- (void)changeModel:(NSString*)modelKey {
  driver_->ChangeModel(base::SysNSStringToUTF8(modelKey));
}

- (AiChatModel*)getCurrentModel {
  return [[AiChatModel alloc] initWithModel:driver_->GetCurrentModel()];
}

- (NSArray<AiChatConversationTurn*>*)getConversationHistory {
  NSMutableArray* array = [[NSMutableArray alloc] init];
  for (const auto& turn : driver_->GetConversationHistory()) {
    [array addObject:[[AiChatConversationTurn alloc]
                         initWithConversationTurn:turn]];
  }
  return array;
}

- (void)onConversationActiveChanged:(bool)is_conversation_active {
  driver_->OnConversationActiveChanged(is_conversation_active);
}

- (void)addToConversationHistory:(AiChatConversationTurn*)turn {
  driver_->AddToConversationHistory(*turn.cppObjPtr);
}

- (void)updateOrCreateLastAssistantEntry:(NSString*)text {
  driver_->UpdateOrCreateLastAssistantEntry(base::SysNSStringToUTF8(text));
}

- (void)makeAPIRequestWithConversationHistoryUpdate:
    (AiChatConversationTurn*)turn {
  driver_->MakeAPIRequestWithConversationHistoryUpdate(*turn.cppObjPtr);
}

- (void)retryAPIRequest {
  driver_->RetryAPIRequest();
}

- (bool)isRequestInProgress {
  return driver_->IsRequestInProgress();
}

- (void)generateQuestions {
  driver_->GenerateQuestions();
}

- (NSArray<NSString*>*)getSuggestedQuestions:(bool*)canGenerate
                                autoGenerate:(bool*)autoGenerate {
  ai_chat::mojom::AutoGenerateQuestionsPref pref =
      ai_chat::mojom::AutoGenerateQuestionsPref::Unset;
  std::vector<std::string> result =
      driver_->GetSuggestedQuestions(*canGenerate, pref);
  *autoGenerate = pref == ai_chat::mojom::AutoGenerateQuestionsPref::Enabled;
  return brave::vector_to_ns(result);
}

- (bool)hasPageContent {
  return driver_->HasPageContent();
}

- (void)disconnectPageContents {
  driver_->DisconnectPageContents();
}

- (void)clearConversationHistory {
  driver_->ClearConversationHistory();
}

- (AiChatAPIError)getCurrentAPIError {
  return (AiChatAPIError)driver_->GetCurrentAPIError();
}
@end
