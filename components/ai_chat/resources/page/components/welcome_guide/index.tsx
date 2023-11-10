/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import * as React from 'react'

import { getLocale } from '$web-common/locale'
import Button from '@brave/leo/react/button'
import formatMessage from '$web-common/formatMessage'

import styles from './style.module.scss'
import penroseTorusURL from '../../assets/penrose-torus.svg'
import radiantStarburstURL from '../../assets/radiant-starburst.svg'
import getPageHandlerInstance from '../../api/page_handler'
import DataContext from '../../state/context'

function WelcomeGuide() {
  const context = React.useContext(DataContext)

  const summarizeNow = () => {
    getPageHandlerInstance().pageHandler.submitHumanConversationEntry(
      getLocale('summarizePageButtonLabel')
    )
  }

  return (
    <div className={styles.box}>
      <h2 className={styles.title}>
        {formatMessage(getLocale('welcomeGuideTitle'), {
         tags: {
          $1: (content) => (
            <>
              <br />
              <span className={styles.subtle}>{content}</span>
            </>
          )
         }
        })}
      </h2>
      <div className={`${styles.card} ${styles.siteHelpCard}`}>
        <h4 className={styles.cardTitle}>{getLocale('siteHelpCardTitle')}</h4>
        {context.siteInfo?.isContentPresent ? (
          <>
            <p>{getLocale('siteHelpCardDescWithAction')}</p>
            <div className={styles.actions}>
              <Button kind='outline' onClick={summarizeNow}>
                {getLocale('summarizePageButtonLabel')}
              </Button>
            </div>
          </>
        ) : <p>{getLocale('siteHelpCardDesc')}</p>}
        <img className={styles.graphic} src={penroseTorusURL} />
      </div>
      <div className={`${styles.card} ${styles.chatCard}`}>
        <h4 className={styles.cardTitle}>{getLocale('chatCardTitle')}</h4>
        <p>
          {getLocale('chatCardDesc')}
        </p>
        <img className={styles.graphic} src={radiantStarburstURL} />
      </div>
    </div>
  )
}

export default WelcomeGuide
