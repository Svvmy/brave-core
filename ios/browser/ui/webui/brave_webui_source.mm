// Copyright (c) 2023 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include "brave/ios/browser/ui/webui/brave_webui_source.h"

#include <string_view>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/strings/utf_string_conversions.h"
#include "brave/components/constants/url_constants.h"
#include "brave/components/webui/webui_resources.h"
#include "build/build_config.h"
#include "components/grit/components_resources.h"
#include "ios/chrome/browser/shared/model/browser_state/chrome_browser_state.h"
#import "ios/web/public/webui/web_ui_ios_data_source.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/webui/resource_path.h"

namespace {

void AddLocalizedStringsBulk(
    web::WebUIIOSDataSource* html_source,
    const std::vector<brave::WebUISimpleItem>& simple_items) {
  for (auto simple_item : simple_items) {
    html_source->AddLocalizedString(std::string(simple_item.name),
                                    simple_item.id);
  }
}

void AddResourcePaths(web::WebUIIOSDataSource* html_source,
                      const std::vector<brave::WebUISimpleItem>& simple_items) {
  for (auto simple_item : simple_items) {
    html_source->AddResourcePath(std::string(simple_item.name), simple_item.id);
  }
}

void CustomizeWebUIHTMLSource(web::WebUIIOS* web_ui,
                              const std::string& name,
                              web::WebUIIOSDataSource* source) {
  const auto& resources = brave::GetWebUIResources();
  if (auto iter = resources.find(name); iter != resources.end()) {
    AddResourcePaths(source, iter->second);
  }

  const auto& localized_strings = brave::GetWebUILocalizedStrings();
  if (auto iter = localized_strings.find(name);
      iter != localized_strings.end()) {
    AddLocalizedStringsBulk(source, iter->second);
  }
}

web::WebUIIOSDataSource* CreateWebUIDataSource(
    web::WebUIIOS* web_ui,
    const std::string& name,
    const webui::ResourcePath* resource_map,
    size_t resource_map_size,
    int html_resource_id,
    bool disable_trusted_types_csp) {
  web::WebUIIOSDataSource* source = web::WebUIIOSDataSource::Create(name);
  web::WebUIIOSDataSource::Add(ChromeBrowserState::FromWebUIIOS(web_ui),
                               source);

  source->UseStringsJs();
  source->AddResourcePaths(base::make_span(resource_map, resource_map_size));
  source->SetDefaultResource(html_resource_id);
  CustomizeWebUIHTMLSource(web_ui, name, source);
  return source;
}

}  // namespace

namespace brave {
web::WebUIIOSDataSource* CreateAndAddWebUIDataSource(
    web::WebUIIOS* web_ui,
    const std::string& name,
    const webui::ResourcePath* resource_map,
    size_t resource_map_size,
    int html_resource_id,
    bool disable_trusted_types_csp) {
  web::WebUIIOSDataSource* data_source =
      CreateWebUIDataSource(web_ui, name, resource_map, resource_map_size,
                            html_resource_id, disable_trusted_types_csp);
  return data_source;
}

}  // namespace brave
