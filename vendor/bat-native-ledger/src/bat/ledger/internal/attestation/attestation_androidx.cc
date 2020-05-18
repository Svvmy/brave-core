/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "bat/ledger/internal/ledger_impl.h"
#include "bat/ledger/internal/attestation/attestation_androidx.h"
#include "bat/ledger/internal/request/request_attestation.h"
#include "net/http/http_status_code.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace braveledger_attestation {

AttestationAndroid::AttestationAndroid(bat_ledger::LedgerImpl* ledger) :
    Attestation(ledger) {
}

AttestationAndroid::~AttestationAndroid() = default;

void AttestationAndroid::ParseClaimSolution(
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

  const auto* nonce = dictionary->FindStringKey("nonce");
  if (!nonce) {
    return;
  }

  const auto* token = dictionary->FindStringKey("token");
  if (!token) {
    return;
  }

  result->SetStringKey("nonce", *nonce);
  result->SetStringKey("token", *token);
}

void AttestationAndroid::Start(
    const std::string& payload,
    StartCallback callback) {
  auto url_callback = std::bind(&AttestationAndroid::OnStart,
      this,
      _1,
      _2,
      _3,
      callback);

  const std::string url =
      braveledger_request_util::GetStartAttestationAndroidUrl();

  auto payment_id = base::Value(ledger_->GetPaymentId());
  base::Value payment_ids(base::Value::Type::LIST);
  payment_ids.Append(std::move(payment_id));

  base::Value body(base::Value::Type::DICTIONARY);
  body.SetKey("paymentIds", std::move(payment_ids));

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

void AttestationAndroid::OnStart(
    const ledger::URLResponse& response,
    StartCallback callback) {
  BLOG(6, ledger::UrlResponseToString(__func__, response));

  if (response.code  != net::HTTP_OK) {
    callback(ledger::Result::LEDGER_ERROR, "");
    return;
  }

  callback(ledger::Result::LEDGER_OK, response);
}

void AttestationAndroid::Confirm(
    const std::string& solution,
    ConfirmCallback callback)  {
  base::Value parsed_solution(base::Value::Type::DICTIONARY);
  ParseClaimSolution(solution, &parsed_solution);

  if (parsed_solution.DictSize() != 2) {
    callback(ledger::Result::LEDGER_ERROR);
    return;
  }

  base::Value dictionary(base::Value::Type::DICTIONARY);
  dictionary.SetStringKey("token", *parsed_solution.FindStringKey("token"));
  std::string payload;
  base::JSONWriter::Write(dictionary, &payload);

  const std::string nonce = *parsed_solution.FindStringKey("nonce");
  const std::string url =
      braveledger_request_util::GetConfirmAttestationAndroidUrl(nonce);

  auto url_callback = std::bind(&AttestationAndroid::OnConfirm,
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

void AttestationAndroid::OnConfirm(
    const ledger::URLResponse& response,
    ConfirmCallback callback) {
  BLOG(6, ledger::UrlResponseToString(__func__, response));

  if (response.code  != net::HTTP_OK) {
    callback(ledger::Result::LEDGER_ERROR);
    return;
  }

  callback(ledger::Result::LEDGER_OK);
}

}  // namespace braveledger_attestation
