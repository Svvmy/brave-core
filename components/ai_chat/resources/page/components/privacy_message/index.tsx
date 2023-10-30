/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import * as React from 'react'

import { getLocale } from '$web-common/locale'
import { Url } from 'gen/url/mojom/url.mojom.m.js'
import Button from '@brave/leo/react/button'

import styles from './style.module.scss'
import getPageHandlerInstance from '../../api/page_handler'
import formatMessage from '$web-common/formatMessage'
import DataContext from '../../state/context'

const WIKI_URL = "https://github.com/brave/brave-browser/wiki/Brave-Leo"

function PrivacyMessage () {
  const context = React.useContext(DataContext)

  const handleWikiLinkClick = () => {
    const mojomUrl = new Url()
    mojomUrl.url = WIKI_URL

    getPageHandlerInstance().pageHandler.openURL(mojomUrl)
  }

  const aboutDescription = formatMessage(getLocale('aboutDescription'), {
    tags: {
      $1: (content) => (
        <a onClick={handleWikiLinkClick} href={WIKI_URL} target='_blank'>
          {content}
        </a>
      )
    }
  })

  return (
    <div className={styles.overlay}>
      <div className={styles.box}>
        <section>
          <h4>{getLocale('aboutTitle')}</h4>
          <p>{aboutDescription}</p>
          <p>{getLocale('aboutDescription_2')}</p>
          <p>{getLocale('aboutDescription_3')}</p>
          <Button onClick={context.handleAgreeClick}>{getLocale('acceptButtonLabel')}</Button>
        </section>
      </div>
    </div>
  )
}

export default PrivacyMessage
