/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <map>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/stringprintf.h"
#include "bat/ledger/internal/ledger_impl.h"
#include "bat/ledger/internal/attestation/attestation_desktop.h"
#include "bat/ledger/internal/request/request_attestation.h"
#include "net/http/http_status_code.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace braveledger_attestation {

AttestationDesktop::AttestationDesktop(bat_ledger::LedgerImpl* ledger) :
    Attestation(ledger) {
}

AttestationDesktop::~AttestationDesktop() = default;

void AttestationDesktop::ParseCaptchaResponse(
    const std::string& response,
    base::Value* result) {
  base::Optional<base::Value> value = base::JSONReader::Read(response);
  if (!value || !value->is_dict()) {
    return;
  }

  base::DictionaryValue* dictionary = nullptr;
  if (!value->GetAsDictionary(&dictionary)) {
    return;
  }

  const auto* captcha_id = dictionary->FindStringKey("captchaId");
  if (!captcha_id) {
    return;
  }

  const auto* hint = dictionary->FindStringKey("hint");
  if (!hint) {
    return;
  }

  result->SetStringKey("hint", *hint);
  result->SetStringKey("captchaId", *captcha_id);
}

void AttestationDesktop::ParseClaimSolution(
    const std::string& response,
    base::Value* result) {
  base::Optional<base::Value> value = base::JSONReader::Read(response);
  if (!value || !value->is_dict()) {
    return;
  }

  base::DictionaryValue* dictionary = nullptr;
  if (!value->GetAsDictionary(&dictionary)) {
    return;
  }

  const auto* captcha_id = dictionary->FindStringKey("captchaId");
  if (!captcha_id) {
    return;
  }

  const auto x = dictionary->FindIntKey("x");
  if (!x) {
    return;
  }

  const auto y = dictionary->FindIntKey("y");
  if (!y) {
    return;
  }

  result->SetIntKey("x", *x);
  result->SetIntKey("y", *y);
  result->SetStringKey("captchaId", *captcha_id);
}

void AttestationDesktop::Start(
    const std::string& payload,
    StartCallback callback) {
  auto url_callback = std::bind(&AttestationDesktop::OnStart,
      this,
      _1,
      _2,
      _3,
      callback);

  const std::string url =
      braveledger_request_util::GetStartAttestationDesktopUrl();

  base::Value body(base::Value::Type::DICTIONARY);
  body.SetStringKey("paymentId", ledger_->GetPaymentId());

  std::string json;
  base::JSONWriter::Write(body, &json);

  ledger_->LoadURL(
      url,
      std::vector<std::string>(),
      json,
      "application/json; charset=utf-8",
      ledger::UrlMethod::POST,
      url_callback);
}

void AttestationDesktop::OnStart(
    const ledger::URLResponse& response,
    StartCallback callback) {
  BLOG(6, ledger::UrlResponseToString(__func__, response));

  if (response.code  != net::HTTP_OK) {
    callback(ledger::Result::LEDGER_ERROR, "");
    return;
  }

  DownloadCaptchaImage(ledger::Result::LEDGER_OK, response, callback);
}

void AttestationDesktop::DownloadCaptchaImage(
    const ledger::Result result,
    const std::string& response,
    StartCallback callback) {
  base::Value dictionary(base::Value::Type::DICTIONARY);
  ParseCaptchaResponse(response, &dictionary);

  if (dictionary.DictEmpty()) {
    callback(ledger::Result::LEDGER_ERROR, "");
    return;
  }

  const auto* id = dictionary.FindStringKey("captchaId");
  if (!id) {
    callback(ledger::Result::LEDGER_ERROR, "");
    return;
  }

  const std::string url = braveledger_request_util::GetCaptchaUrl(*id);
  auto url_callback = std::bind(&AttestationDesktop::OnDownloadCaptchaImage,
      this,
      _1,
      _2,
      _3,
      response,
      callback);

  ledger_->LoadURL(
      url,
      std::vector<std::string>(),
      "",
      "",
      ledger::UrlMethod::GET,
      url_callback);
}

void AttestationDesktop::OnDownloadCaptchaImage(
    const ledger::URLResponse& response,
    const std::string& captcha_response,
    StartCallback callback) {
  BLOG(6, ledger::UrlResponseToString(__func__, response_status_code,
      "<PNG>", headers));

  base::Value dictionary(base::Value::Type::DICTIONARY);
  ParseCaptchaResponse(captcha_response, &dictionary);

  if (response.code  != net::HTTP_OK || dictionary.DictEmpty()) {
    callback(ledger::Result::LEDGER_ERROR, "");
    return;
  }

  std::string encoded_image;
  base::Base64Encode(response, &encoded_image);
  encoded_image =
      base::StringPrintf("data:image/jpeg;base64,%s", encoded_image.c_str());
  dictionary.SetStringKey("captchaImage", encoded_image);

  std::string json;
  base::JSONWriter::Write(dictionary, &json);
  callback(ledger::Result::LEDGER_OK, json);
}

void AttestationDesktop::Confirm(
    const std::string& solution,
    ConfirmCallback callback) {
  base::Value parsed_solution(base::Value::Type::DICTIONARY);
  ParseClaimSolution(solution, &parsed_solution);

  if (parsed_solution.DictSize() != 3) {
    callback(ledger::Result::LEDGER_ERROR);
    return;
  }

  base::Value dictionary(base::Value::Type::DICTIONARY);
  base::Value solution_dict(base::Value::Type::DICTIONARY);
  solution_dict.SetIntKey("x", *parsed_solution.FindIntKey("x"));
  solution_dict.SetIntKey("y", *parsed_solution.FindIntKey("y"));
  dictionary.SetKey("solution", std::move(solution_dict));
  std::string payload;
  base::JSONWriter::Write(dictionary, &payload);

  const auto* id = parsed_solution.FindStringKey("captchaId");
  if (!id) {
    callback(ledger::Result::LEDGER_ERROR);
    return;
  }

  const std::string url =
      braveledger_request_util::GetClaimAttestationDesktopUrl(*id);
  auto url_callback = std::bind(&AttestationDesktop::OnConfirm,
      this,
      _1,
      _2,
      _3,
      callback);

  ledger_->LoadURL(
      url,
      std::vector<std::string>(),
      payload,
      "application/json; charset=utf-8",
      ledger::UrlMethod::PUT,
      url_callback);
}

void AttestationDesktop::OnConfirm(
    const ledger::URLResponse& response,
    ConfirmCallback callback) {
  BLOG(6, ledger::UrlResponseToString(__func__, response));

  if (response_status_code == net::HTTP_OK) {
    callback(ledger::Result::LEDGER_OK);
    return;
  }

  if (response_status_code == net::HTTP_UNAUTHORIZED ||
      response_status_code == net::HTTP_BAD_REQUEST ) {
    callback(ledger::Result::CAPTCHA_FAILED);
    return;
  }

  callback(ledger::Result::LEDGER_ERROR);
}

}  // namespace braveledger_attestation
