// Copyright (c) 2023 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/components/brave_viewer/browser/content/brave_viewer_tab_helper.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "brave/components/brave_viewer/browser/core/brave_viewer_service.h"
#include "brave/components/brave_viewer/common/features.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"

namespace brave_viewer {

// static
void BraveViewerTabHelper::MaybeCreateForWebContents(
    content::WebContents* contents,
    const int32_t world_id) {
  if (!base::FeatureList::IsEnabled(brave_viewer::features::kBraveViewer)) {
    return;
  }

  if (!SameDomainOrHost(
          contents->GetLastCommittedURL(),
          url::Origin::CreateFromNormalizedTuple("https", "youtube.com", 443),
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES)) {
    return;
  }

  brave_viewer::BraveViewerTabHelper::CreateForWebContents(contents, world_id);
}

BraveViewerTabHelper::BraveViewerTabHelper(content::WebContents* web_contents,
                                           const int32_t world_id)
    : WebContentsObserver(web_contents),
      content::WebContentsUserData<BraveViewerTabHelper>(*web_contents),
      world_id_(world_id),
      brave_viewer_service_(BraveViewerService::GetInstance()) {
  DCHECK(brave_viewer_service_);
}

BraveViewerTabHelper::~BraveViewerTabHelper() = default;

void BraveViewerTabHelper::OnTestScriptResult(
    const content::GlobalRenderFrameHostId& render_frame_host_id,
    base::Value value) {
  if (value.GetIfBool().value_or(false)) {
    // TODO check state and/or trigger Brave Viewer dialog here
  }
}

void BraveViewerTabHelper::InsertTestScript(
    const content::GlobalRenderFrameHostId& render_frame_host_id,
    std::string test_script) {
  InsertScriptInPage(
      render_frame_host_id, test_script,
      base::BindOnce(&BraveViewerTabHelper::OnTestScriptResult,
                     weak_factory_.GetWeakPtr(), render_frame_host_id));
}

void BraveViewerTabHelper::InsertScriptInPage(
    const content::GlobalRenderFrameHostId& render_frame_host_id,
    const std::string& script,
    content::RenderFrameHost::JavaScriptResultCallback cb) {
  content::RenderFrameHost* render_frame_host =
      content::RenderFrameHost::FromID(render_frame_host_id);

  // Check if render_frame_host is still valid and if starting rfh is the same.
  if (render_frame_host &&
      render_frame_host_id ==
          web_contents()->GetPrimaryMainFrame()->GetGlobalId()) {
    GetRemote(render_frame_host)
        ->RequestAsyncExecuteScript(
            world_id_, base::UTF8ToUTF16(script),
            blink::mojom::UserActivationOption::kDoNotActivate,
            blink::mojom::PromiseResultOption::kAwait, std::move(cb));
  } else {
    VLOG(2) << "render_frame_host is invalid.";
    return;
  }
}

mojo::AssociatedRemote<script_injector::mojom::ScriptInjector>&
BraveViewerTabHelper::GetRemote(content::RenderFrameHost* rfh) {
  if (!script_injector_remote_.is_bound()) {
    rfh->GetRemoteAssociatedInterfaces()->GetInterface(
        &script_injector_remote_);
  }
  return script_injector_remote_;
}

void BraveViewerTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted() ||
      navigation_handle->IsSameDocument()) {
    return;
  }

  should_process_ =
      navigation_handle->GetRestoreType() == content::RestoreType::kNotRestored;
}

void BraveViewerTabHelper::DocumentOnLoadCompletedInPrimaryMainFrame() {
  DCHECK(brave_viewer_service_);
  // Make sure it gets reset.
  bool should_process = should_process_;
  should_process_ = false;
  if (!should_process) {
    return;
  }
  auto url = web_contents()->GetLastCommittedURL();

  content::GlobalRenderFrameHostId render_frame_host_id =
      web_contents()->GetPrimaryMainFrame()->GetGlobalId();

  brave_viewer_service_->GetTestScript(
      url, base::BindOnce(&BraveViewerTabHelper::InsertTestScript,
                          weak_factory_.GetWeakPtr(), render_frame_host_id));
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(BraveViewerTabHelper);

}  // namespace brave_viewer
