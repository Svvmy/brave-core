// Copyright (c) 2023 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/ios/browser/api/ai_chat/ai_chat.h"
#include "brave/ios/browser/api/ai_chat/ai_chat_factory+private.h"

@interface AIChat (Private)
- (instancetype)initWithChromeBrowserState:(ChromeBrowserState*)browserState
                                  delegate:
                                      (__weak id<AIChatDelegateIOS>)delegate;
@end

@interface AIChatFactoryAPI () {
  ChromeBrowserState* browser_state_;
}
@end

@implementation AIChatFactoryAPI
- (instancetype)initWithChromeBrowserState:(ChromeBrowserState*)browserState {
  if ((self = [super init])) {
    browser_state_ = browserState;
  }
  return self;
}

- (AIChat*)aiChatWithDelegate:(__weak id<AIChatDelegateIOS>)delegate {
  return [[AIChat alloc] initWithChromeBrowserState:browser_state_
                                           delegate:delegate];
}
@end
