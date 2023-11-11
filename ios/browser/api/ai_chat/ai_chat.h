// Copyright (c) 2023 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_IOS_BROWSER_API_AI_CHAT_AI_CHAT_H_
#define BRAVE_IOS_BROWSER_API_AI_CHAT_AI_CHAT_H_

#import <Foundation/Foundation.h>
#import "ai_chat.mojom.objc.h"

NS_ASSUME_NONNULL_BEGIN

OBJC_EXPORT
@protocol AIChatDelegateIOS <NSObject>
- (NSURL*)getLastCommittedURL;
- (void)getPageContentWithCompletion:(void (^)(NSString* content,
                                               bool isVideo))completion;

- (void)onHistoryUpdate;
- (void)onAPIRequestInProgress:(bool)inProgress;
- (void)onAPIResponseError:(AiChatAPIError)error;
- (void)onSuggestedQuestionsChanged:(NSArray<NSString*>*)questions
                       hasGenerated:(bool)hasGenerated
                       autoGenerate:(bool)autoGenerate;
@end

OBJC_EXPORT
@interface AIChat : NSObject
@property(nonatomic) bool hasUserOptedIn;

- (void)changeModel:(NSString*)modelKey;
- (AiChatModel*)getCurrentModel;
- (NSArray<AiChatConversationTurn*>*)getConversationHistory;

- (void)onConversationActiveChanged:(bool)is_conversation_active;
- (void)addToConversationHistory:(AiChatConversationTurn*)turn;
- (void)updateOrCreateLastAssistantEntry:(NSString*)text;
- (void)makeAPIRequestWithConversationHistoryUpdate:
    (AiChatConversationTurn*)turn;
- (void)retryAPIRequest;
- (bool)isRequestInProgress;

- (void)generateQuestions;
- (NSArray<NSString*>*)getSuggestedQuestions:(bool*)canGenerate
                                autoGenerate:(bool*)autoGenerate;
- (bool)hasPageContent;

- (void)disconnectPageContents;

- (void)clearConversationHistory;
- (AiChatAPIError)getCurrentAPIError;
@end

NS_ASSUME_NONNULL_END

#endif  // BRAVE_IOS_BROWSER_API_AI_CHAT_AI_CHAT_H_
