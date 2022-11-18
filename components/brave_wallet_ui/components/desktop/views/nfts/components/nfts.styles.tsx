// Copyright (c) 2022 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at https://mozilla.org/MPL/2.0/.
import styled from 'styled-components'
import { WalletButton } from '../../../../shared/style'

import Ipfs from '../../../../../assets/svg-icons/nft-ipfs/ipfs.svg'

export const FilterTokenRow = styled.div`
  display: flex;
  align-items: center;
  justify-content: space-between;
  flex-direction: row;
  width: 100%;
  gap: 14px;
`

export const NftGrid = styled.div`
  display: grid;
  grid-template-columns: repeat(5, 1fr);
  grid-gap: 25px;
  box-sizing: border-box;
  width: 100%;
  padding-top: 10px;
  @media screen and (max-width: 1350px) {
    grid-template-columns: repeat(4, 1fr);
  }
  @media screen and (max-width: 1150px) {
    grid-template-columns: repeat(3, 1fr);
  }
  @media screen and (max-width: 950px) {
    grid-template-columns: repeat(2, 1fr);
  }
`

export const EmptyStateText = styled.div`
  text-align: center;
  padding: 30px 0;
  color: ${p => p.theme.color.text03};
  font-size: 14px;
  font-family: Poppins;
`

export const IpfsButton = styled(WalletButton)`
  display: flex;
  align-items: center;
  justify-content:center ;
  background-color: transparent;
  border-radius: 6px;
  border: 1px solid ${p => p.theme.color.interactive08};
  padding: 6px;
  align-self: flex-start;
`

export const IpfsIcon = styled.div`
  display: flex;
  align-items: center;
  justify-content: center;
  width: 24px;
  height: 24px;
  mask-image: url(${Ipfs});
  -webkit-mask-image: url(${Ipfs});
  mask-repeat: no-repeat;
  background-color: #495057;
  cursor: pointer;
  
  &:hover {
    background-color: #469EA2;
  }
`
