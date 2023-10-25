/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

package org.chromium.chrome.browser.playlist;

import static com.google.android.exoplayer2.util.Assertions.checkNotNull;
import static com.google.android.exoplayer2.util.Util.castNonNull;

import static java.lang.Math.min;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.text.TextUtils;

import androidx.annotation.Nullable;
import androidx.fragment.app.FragmentTransaction;
import androidx.lifecycle.ViewModelProvider;

import com.brave.playlist.PlaylistViewModel;
import com.brave.playlist.enums.PlaylistItemEventEnum;
import com.brave.playlist.enums.PlaylistOptionsEnum;
import com.brave.playlist.fragment.AllPlaylistFragment;
import com.brave.playlist.fragment.PlaylistFragment;
import com.brave.playlist.listener.PlaylistOptionsListener;
import com.brave.playlist.model.DownloadProgressModel;
import com.brave.playlist.model.MoveOrCopyModel;
import com.brave.playlist.model.PlaylistItemEventModel;
import com.brave.playlist.model.PlaylistItemModel;
import com.brave.playlist.model.PlaylistModel;
import com.brave.playlist.model.PlaylistOptionsModel;
import com.brave.playlist.util.ConstantUtils;
import com.brave.playlist.util.HLSParsingUtil;
import com.brave.playlist.util.PlaylistUtils;
import com.brave.playlist.view.bottomsheet.MoveOrCopyToPlaylistBottomSheet;
import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.PlaybackException;
import com.google.android.exoplayer2.source.hls.playlist.HlsMediaPlaylist.Segment;
import com.google.android.exoplayer2.upstream.BaseDataSource;
import com.google.android.exoplayer2.upstream.DataSource;
import com.google.android.exoplayer2.upstream.DataSourceException;
import com.google.android.exoplayer2.upstream.DataSpec;
import com.google.android.exoplayer2.upstream.HttpDataSource;
import com.google.android.exoplayer2.upstream.HttpUtil;
import com.google.android.exoplayer2.upstream.TransferListener;
import com.google.android.exoplayer2.util.UriUtil;
import com.google.android.exoplayer2.util.Util;
import com.google.common.base.Predicate;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HttpHeaders;

import org.chromium.base.BraveFeatureList;
import org.chromium.base.Log;
import org.chromium.base.task.PostTask;
import org.chromium.base.task.TaskTraits;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.init.AsyncInitializationActivity;
import org.chromium.chrome.browser.playlist.PlaylistServiceObserverImpl.PlaylistServiceObserverImplDelegate;
import org.chromium.chrome.browser.playlist.PlaylistStreamingObserverImpl.PlaylistStreamingObserverImplDelegate;
import org.chromium.chrome.browser.playlist.download.DownloadService;
import org.chromium.chrome.browser.playlist.settings.BravePlaylistPreferences;
import org.chromium.chrome.browser.preferences.SharedPreferencesManager;
import org.chromium.chrome.browser.util.TabUtils;
import org.chromium.mojo.bindings.ConnectionErrorHandler;
import org.chromium.mojo.system.MojoException;
import org.chromium.playlist.mojom.Playlist;
import org.chromium.playlist.mojom.PlaylistEvent;
import org.chromium.playlist.mojom.PlaylistItem;
import org.chromium.playlist.mojom.PlaylistService;
import org.chromium.playlist.mojom.PlaylistStreamingObserver;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Queue;

public class PlaylistHostActivity extends AsyncInitializationActivity
        implements ConnectionErrorHandler, PlaylistOptionsListener,
                   PlaylistServiceObserverImplDelegate {
    private static final String TAG = "BravePlaylist";
    public static PlaylistService mPlaylistService;
    private PlaylistViewModel mPlaylistViewModel;
    private PlaylistServiceObserverImpl mPlaylistServiceObserver;

    @Override
    public void onConnectionError(MojoException e) {
        if (ChromeFeatureList.isEnabled(BraveFeatureList.BRAVE_PLAYLIST)
                && SharedPreferencesManager.getInstance().readBoolean(
                        BravePlaylistPreferences.PREF_ENABLE_PLAYLIST, true)) {
            mPlaylistService = null;
            initPlaylistService();
        }
    }

    private void initPlaylistService() {
        if (mPlaylistService != null) {
            mPlaylistService = null;
        }
        mPlaylistService = PlaylistServiceFactoryAndroid.getInstance().getPlaylistService(this);
        addPlaylistObserver();
    }

    private void addPlaylistObserver() {
        mPlaylistServiceObserver = new PlaylistServiceObserverImpl(this);
        mPlaylistService.addObserver(mPlaylistServiceObserver);
    }

    @Override
    protected void triggerLayoutInflation() {
        setContentView(R.layout.activity_playlist_host);
        onInitialLayoutInflationComplete();
    }

    @Override
    public void finishNativeInitialization() {
        super.finishNativeInitialization();
        if (ChromeFeatureList.isEnabled(BraveFeatureList.BRAVE_PLAYLIST)) {
            initPlaylistService();
        }
        mPlaylistViewModel =
                new ViewModelProvider(PlaylistHostActivity.this).get(PlaylistViewModel.class);

        mPlaylistViewModel.getCreatePlaylistOption().observe(
                PlaylistHostActivity.this, createPlaylistModel -> {
                    if (mPlaylistService == null) {
                        return;
                    }
                    Playlist playlist = new Playlist();
                    playlist.name = createPlaylistModel.getNewPlaylistId();
                    playlist.items = new PlaylistItem[0];
                    mPlaylistService.createPlaylist(playlist, createdPlaylist -> {
                        if (createPlaylistModel.isMoveOrCopy()
                                && PlaylistUtils.moveOrCopyModel != null) {
                            MoveOrCopyModel tempMoveOrCopyModel = PlaylistUtils.moveOrCopyModel;
                            PlaylistUtils.moveOrCopyModel = new MoveOrCopyModel(
                                    tempMoveOrCopyModel.getPlaylistOptionsEnum(),
                                    createdPlaylist.id, tempMoveOrCopyModel.getPlaylistItems());
                            mPlaylistViewModel.performMoveOrCopy(PlaylistUtils.moveOrCopyModel);
                        }
                    });
                });

        mPlaylistViewModel.getRenamePlaylistOption().observe(
                PlaylistHostActivity.this, renamePlaylistModel -> {
                    if (mPlaylistService == null) {
                        return;
                    }
                    mPlaylistService.renamePlaylist(renamePlaylistModel.getPlaylistId(),
                            renamePlaylistModel.getNewName(),
                            updatedPlaylist -> { loadPlaylist(updatedPlaylist.id); });
                });

        mPlaylistViewModel.getPlaylistToOpen().observe(
                PlaylistHostActivity.this, playlistId -> { showPlaylist(playlistId, true); });

        mPlaylistViewModel.getFetchPlaylistData().observe(PlaylistHostActivity.this, playlistId -> {
            if (mPlaylistService == null) {
                return;
            }
            if (playlistId.equals(ConstantUtils.ALL_PLAYLIST)) {
                loadAllPlaylists();
            } else {
                loadPlaylist(playlistId);
            }
        });

        mPlaylistViewModel.getReorderPlaylistItems().observe(
                PlaylistHostActivity.this, playlistItems -> {
                    if (mPlaylistService == null) {
                        return;
                    }
                    for (int i = 0; i < playlistItems.size(); i++) {
                        PlaylistItemModel playlistItemModel = playlistItems.get(i);
                        mPlaylistService.reorderItemFromPlaylist(playlistItemModel.getPlaylistId(),
                                playlistItemModel.getId(), (short) i, (result) -> {});
                    }
                });

        mPlaylistViewModel.getDeletePlaylistItems().observe(
                PlaylistHostActivity.this, playlistItems -> {
                    if (mPlaylistService == null) {
                        return;
                    }
                    for (PlaylistItemModel playlistItem : playlistItems.getItems()) {
                        mPlaylistService.removeItemFromPlaylist(
                                playlistItems.getId(), playlistItem.getId());
                    }
                });

        mPlaylistViewModel.getMoveOrCopyItems().observe(
                PlaylistHostActivity.this, moveOrCopyModel -> {
                    if (mPlaylistService == null) {
                        return;
                    }
                    if (moveOrCopyModel.getPlaylistOptionsEnum()
                                    == PlaylistOptionsEnum.MOVE_PLAYLIST_ITEM
                            || moveOrCopyModel.getPlaylistOptionsEnum()
                                    == PlaylistOptionsEnum.MOVE_PLAYLIST_ITEMS) {
                        for (PlaylistItemModel playlistItem : moveOrCopyModel.getPlaylistItems()) {
                            mPlaylistService.moveItem(playlistItem.getPlaylistId(),
                                    moveOrCopyModel.getToPlaylistId(), playlistItem.getId());
                        }
                    } else {
                        String[] playlistItemIds =
                                new String[moveOrCopyModel.getPlaylistItems().size()];
                        for (int i = 0; i < moveOrCopyModel.getPlaylistItems().size(); i++) {
                            playlistItemIds[i] = moveOrCopyModel.getPlaylistItems().get(i).getId();
                        }
                        mPlaylistService.copyItemToPlaylist(
                                playlistItemIds, moveOrCopyModel.getToPlaylistId());
                    }
                    if (moveOrCopyModel.getPlaylistItems().size() > 0) {
                        loadPlaylist(moveOrCopyModel.getPlaylistItems().get(0).getPlaylistId());
                    }
                });

        mPlaylistViewModel.getPlaylistOption().observe(
                PlaylistHostActivity.this, playlistOptionsModel -> {
                    if (mPlaylistService == null) {
                        return;
                    }
                    PlaylistOptionsEnum option = playlistOptionsModel.getOptionType();
                    if (option == PlaylistOptionsEnum.REMOVE_PLAYLIST_OFFLINE_DATA) {
                        if (playlistOptionsModel.getPlaylistModel() != null) {
                            mPlaylistService.removeLocalDataForItemsInPlaylist(
                                    playlistOptionsModel.getPlaylistModel().getId());
                        }
                    } else if (option == PlaylistOptionsEnum.DOWNLOAD_PLAYLIST_FOR_OFFLINE_USE) {
                        if (playlistOptionsModel.getPlaylistModel() != null) {
                            for (PlaylistItemModel playlistItemModel :
                                    playlistOptionsModel.getPlaylistModel().getItems()) {
                                mPlaylistService.recoverLocalDataForItem(playlistItemModel.getId(),
                                        true,
                                        playlistItem
                                        -> {

                                        });
                            }
                        }
                    } else if (option == PlaylistOptionsEnum.DELETE_PLAYLIST) {
                        if (playlistOptionsModel.getPlaylistModel() != null) {
                            mPlaylistService.removePlaylist(
                                    playlistOptionsModel.getPlaylistModel().getId());
                        }
                    } else if (option == PlaylistOptionsEnum.MOVE_PLAYLIST_ITEMS) {
                        showMoveOrCopyPlaylistBottomSheet();
                    } else if (option == PlaylistOptionsEnum.COPY_PLAYLIST_ITEMS) {
                        showMoveOrCopyPlaylistBottomSheet();
                    }
                });

        mPlaylistViewModel.getAllPlaylistOption().observe(
                PlaylistHostActivity.this, playlistOptionsModel -> {
                    if (mPlaylistService == null) {
                        return;
                    }
                    PlaylistOptionsEnum option = playlistOptionsModel.getOptionType();
                    if (option == PlaylistOptionsEnum.REMOVE_ALL_OFFLINE_DATA) {
                        if (playlistOptionsModel.getAllPlaylistModels() != null) {
                            for (PlaylistModel playlistModel :
                                    playlistOptionsModel.getAllPlaylistModels()) {
                                mPlaylistService.removeLocalDataForItemsInPlaylist(
                                        playlistModel.getId());
                            }
                        }
                    } else if (option
                            == PlaylistOptionsEnum.DOWNLOAD_ALL_PLAYLISTS_FOR_OFFLINE_USE) {
                        mPlaylistService.getAllPlaylists(playlists -> {
                            for (Playlist playlist : playlists) {
                                for (PlaylistItem playlistItem : playlist.items) {
                                    mPlaylistService.recoverLocalDataForItem(playlistItem.id, true,
                                            tempPlaylistItem
                                            -> {

                                            });
                                }
                            }
                        });
                    }
                });

        mPlaylistViewModel.getPlaylistItemOption().observe(
                PlaylistHostActivity.this, playlistItemOption -> {
                    if (mPlaylistService == null) {
                        return;
                    }
                    PlaylistOptionsEnum option = playlistItemOption.getOptionType();
                    if (option == PlaylistOptionsEnum.MOVE_PLAYLIST_ITEM) {
                        showMoveOrCopyPlaylistBottomSheet();
                    } else if (option == PlaylistOptionsEnum.COPY_PLAYLIST_ITEM) {
                        showMoveOrCopyPlaylistBottomSheet();
                    } else if (option == PlaylistOptionsEnum.DELETE_ITEMS_OFFLINE_DATA) {
                        mPlaylistService.removeLocalDataForItem(
                                playlistItemOption.getPlaylistItemModel().getId());
                        // Playlist item will be updated based on event
                    } else if (option == PlaylistOptionsEnum.OPEN_IN_NEW_TAB) {
                        openPlaylistInTab(
                                false, playlistItemOption.getPlaylistItemModel().getPageSource());
                    } else if (option == PlaylistOptionsEnum.OPEN_IN_PRIVATE_TAB) {
                        openPlaylistInTab(
                                true, playlistItemOption.getPlaylistItemModel().getPageSource());
                    } else if (option == PlaylistOptionsEnum.DELETE_PLAYLIST_ITEM) {
                        mPlaylistService.removeItemFromPlaylist(playlistItemOption.getPlaylistId(),
                                playlistItemOption.getPlaylistItemModel().getId());
                        loadPlaylist(playlistItemOption.getPlaylistId());
                    } else if (option == PlaylistOptionsEnum.RECOVER_PLAYLIST_ITEM) {
                        mPlaylistService.recoverLocalDataForItem(
                                playlistItemOption.getPlaylistItemModel().getId(), true,
                                playlistItem -> {});
                    }
                });

        if (getIntent() != null) {
            if (!TextUtils.isEmpty(getIntent().getAction())
                    && getIntent().getAction().equals(ConstantUtils.PLAYLIST_ACTION)
                    && !TextUtils.isEmpty(
                            getIntent().getStringExtra(ConstantUtils.CURRENT_PLAYLIST_ID))
                    && !TextUtils.isEmpty(
                            getIntent().getStringExtra(ConstantUtils.CURRENT_PLAYING_ITEM_ID))) {
                showPlaylistForPlayer(getIntent().getStringExtra(ConstantUtils.CURRENT_PLAYLIST_ID),
                        getIntent().getStringExtra(ConstantUtils.CURRENT_PLAYING_ITEM_ID));
            } else {
                String playlistId = getIntent().getStringExtra(ConstantUtils.PLAYLIST_ID);
                if (!TextUtils.isEmpty(playlistId)
                        && playlistId.equals(ConstantUtils.ALL_PLAYLIST)) {
                    showAllPlaylistsFragment();
                } else {
                    showPlaylist(playlistId, false);
                }
            }
        }
    }

    private void showPlaylist(String playlistId, boolean shouldFallback) {
        if (mPlaylistService == null) {
            return;
        }
        loadPlaylist(playlistId);
        PlaylistFragment playlistFragment = new PlaylistFragment();
        FragmentTransaction transaction = getSupportFragmentManager().beginTransaction().replace(
                R.id.fragment_container_view_tag, playlistFragment);
        if (shouldFallback) {
            transaction.addToBackStack(AllPlaylistFragment.class.getSimpleName());
        }
        transaction.commit();
    }

    private void showPlaylistForPlayer(String playlistId, String playlistItemId) {
        if (mPlaylistService == null) {
            return;
        }
        loadPlaylist(playlistId);
        PlaylistFragment playlistFragment = new PlaylistFragment();
        Bundle fragmentBundle = new Bundle();
        fragmentBundle.putString(ConstantUtils.CURRENT_PLAYING_ITEM_ID, playlistItemId);
        playlistFragment.setArguments(fragmentBundle);
        FragmentTransaction transaction = getSupportFragmentManager().beginTransaction().replace(
                R.id.fragment_container_view_tag, playlistFragment);
        transaction.commit();
    }

    private void loadPlaylist(String playlistId) {
        if (mPlaylistService == null) {
            return;
        }
        mPlaylistService.getPlaylist(playlistId, playlist -> {
            List<PlaylistItemModel> playlistItems = new ArrayList();
            for (PlaylistItem playlistItem : playlist.items) {
                PlaylistItemModel playlistItemModel = new PlaylistItemModel(playlistItem.id,
                        playlist.id, playlistItem.name, playlistItem.pageSource.url,
                        playlistItem.mediaPath.url, playlistItem.hlsMediaPath.url,
                        playlistItem.mediaSource.url, playlistItem.thumbnailPath.url,
                        playlistItem.author, playlistItem.duration, playlistItem.lastPlayedPosition,
                        playlistItem.mediaFileBytes, playlistItem.cached, false);
                playlistItems.add(playlistItemModel);
            }
            PlaylistModel playlistModel =
                    new PlaylistModel(playlist.id, playlist.name, playlistItems);
            if (mPlaylistViewModel != null) {
                mPlaylistViewModel.setPlaylistData(playlistModel);
            }
        });
    }

    private void loadAllPlaylists() {
        if (mPlaylistService == null) {
            return;
        }
        mPlaylistService.getAllPlaylists(playlists -> {
            List<PlaylistModel> allPlaylists = new ArrayList();
            for (Playlist playlist : playlists) {
                List<PlaylistItemModel> playlistItems = new ArrayList();
                for (PlaylistItem playlistItem : playlist.items) {
                    PlaylistItemModel playlistItemModel =
                            new PlaylistItemModel(playlistItem.id, playlist.id, playlistItem.name,
                                    playlistItem.pageSource.url, playlistItem.mediaPath.url,
                                    playlistItem.hlsMediaPath.url, playlistItem.mediaSource.url,
                                    playlistItem.thumbnailPath.url, playlistItem.author,
                                    playlistItem.duration, playlistItem.lastPlayedPosition,
                                    playlistItem.mediaFileBytes, playlistItem.cached, false);
                    playlistItems.add(playlistItemModel);
                }
                PlaylistModel playlistModel =
                        new PlaylistModel(playlist.id, playlist.name, playlistItems);
                allPlaylists.add(playlistModel);
            }
            if (mPlaylistViewModel != null) {
                mPlaylistViewModel.setAllPlaylistData(allPlaylists);
            }
        });
    }

    private void showAllPlaylistsFragment() {
        AllPlaylistFragment allPlaylistFragment = new AllPlaylistFragment();
        getSupportFragmentManager()
                .beginTransaction()
                .replace(R.id.fragment_container_view_tag, allPlaylistFragment)
                .commit();
    }

    private void showMoveOrCopyPlaylistBottomSheet() {
        loadAllPlaylists();
        new MoveOrCopyToPlaylistBottomSheet().show(getSupportFragmentManager(), null);
    }

    @Override
    public void onPlaylistOptionClicked(PlaylistOptionsModel playlistOptionsModel) {
        if (PlaylistOptionsEnum.DELETE_PLAYLIST == playlistOptionsModel.getOptionType()
                && mPlaylistService != null && playlistOptionsModel.getPlaylistModel() != null) {
            mPlaylistService.removePlaylist(playlistOptionsModel.getPlaylistModel().getId());
            finish();
        }
    }

    private void openPlaylistInTab(boolean isIncognito, String url) {
        finish();
        TabUtils.openUrlInNewTab(isIncognito, url);
    }

    private void updatePlaylistItem(String playlistId, PlaylistItem playlistItem) {
        if (mPlaylistViewModel == null) {
            return;
        }
        PlaylistItemModel playlistItemModel = new PlaylistItemModel(playlistItem.id, playlistId,
                playlistItem.name, playlistItem.pageSource.url, playlistItem.mediaPath.url,
                playlistItem.hlsMediaPath.url, playlistItem.mediaSource.url,
                playlistItem.thumbnailPath.url, playlistItem.author, playlistItem.duration,
                playlistItem.lastPlayedPosition, (long) playlistItem.mediaFileBytes,
                playlistItem.cached, false);
        mPlaylistViewModel.updatePlaylistItem(playlistItemModel);
    }

    @Override
    public void onItemCached(PlaylistItem playlistItem) {
        updatePlaylistItem(ConstantUtils.DEFAULT_PLAYLIST, playlistItem);
    }

    @Override
    public void onItemUpdated(PlaylistItem playlistItem) {
        updatePlaylistItem(ConstantUtils.DEFAULT_PLAYLIST, playlistItem);
    }

    @Override
    public void onPlaylistUpdated(Playlist playlist) {
        // Used only for reorder items
        loadPlaylist(playlist.id);
    }

    @Override
    public void onEvent(int eventType, String id) {
        if (eventType == PlaylistEvent.LIST_CREATED || eventType == PlaylistEvent.LIST_REMOVED) {
            loadAllPlaylists();
        }
    }

    @Override
    public void onMediaFileDownloadProgressed(String id, long totalBytes, long receivedBytes,
            byte percentComplete, String timeRemaining) {
        if (mPlaylistViewModel != null) {
            mPlaylistViewModel.updateDownloadProgress(
                    new DownloadProgressModel(id, totalBytes, receivedBytes, "" + percentComplete));
        }
    }

    @Override
    public void onDestroy() {
        if (mPlaylistService != null) {
            mPlaylistService.close();
        }
        if (mPlaylistServiceObserver != null) {
            mPlaylistServiceObserver.close();
            mPlaylistServiceObserver.destroy();
            mPlaylistServiceObserver = null;
        }
        super.onDestroy();
    }

    @Override
    public boolean shouldStartGpuProcess() {
        return true;
    }
}
