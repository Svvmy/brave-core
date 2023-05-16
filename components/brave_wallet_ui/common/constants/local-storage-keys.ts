// Copyright (c) 2018 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.
export const LOCAL_STORAGE_KEYS = {
  IS_PORTFOLIO_OVERVIEW_GRAPH_HIDDEN: 'BRAVE_WALLET_IS_WALLET_PORTFOLIO_OVERVIEW_GRAPH_HIDDEN',
  HIDE_PORTFOLIO_BALANCES:
    'BRAVE_WALLET_HIDE_PORTFOLIO_BALANCES',
  PORTFOLIO_NETWORK_FILTER_OPTION: 'PORTFOLIO_NETWORK_FILTER_OPTION',
  PORTFOLIO_ACCOUNT_FILTER_OPTION: 'PORTFOLIO_ACCOUNT_FILTER_OPTION',
  PORTFOLIO_ASSET_FILTER_OPTION: 'PORTFOLIO_ASSET_FILTER_OPTION',
  PORTFOLIO_TIME_LINE_OPTION: 'PORTFOLIO_TIME_LINE_OPTION',
  IS_IPFS_BANNER_HIDDEN: 'BRAVE_WALLET_IS_IPFS_BANNER_HIDDEN',
  IS_ENABLE_NFT_AUTO_DISCOVERY_MODAL_HIDDEN: 'BRAVE_WALLET_IS_ENABLE_NFT_AUTO_DISCOVERY_MODAL_HIDDEN',
  USER_REMOVED_NON_FUNGIBLE_TOKEN_IDS:
    'BRAVE_WALLET_USER_REMOVED_NON_FUNGIBLE_TOKEN_IDS',
  USER_REMOVED_FUNGIBLE_TOKEN_IDS:
    'BRAVE_WALLET_USER_REMOVED_FUNGIBLE_TOKEN_IDS',
  NFT_METADATA_RESPONSE_CACHE: 'BRAVE_WALLET_NFT_METADATA_RESPONSE_CACHE'
} as const
