/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdint.h>
#include <memory>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/containers/contains.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/notreached.h"
#include "base/ranges/algorithm.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/mock_callback.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "brave/components/brave_wallet/browser/blockchain_registry.h"
#include "brave/components/brave_wallet/browser/brave_wallet_constants.h"
#include "brave/components/brave_wallet/browser/brave_wallet_prefs.h"
#include "brave/components/brave_wallet/browser/brave_wallet_utils.h"
#include "brave/components/brave_wallet/browser/ens_resolver_task.h"
#include "brave/components/brave_wallet/browser/eth_data_builder.h"
#include "brave/components/brave_wallet/browser/json_rpc_service.h"
#include "brave/components/brave_wallet/browser/json_rpc_service_test_utils.h"
#include "brave/components/brave_wallet/browser/keyring_service.h"
#include "brave/components/brave_wallet/browser/pref_names.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "brave/components/brave_wallet/common/eth_abi_utils.h"
#include "brave/components/brave_wallet/common/eth_address.h"
#include "brave/components/brave_wallet/common/features.h"
#include "brave/components/brave_wallet/common/hash_utils.h"
#include "brave/components/brave_wallet/common/hex_utils.h"
#include "brave/components/brave_wallet/common/test_utils.h"
#include "brave/components/brave_wallet/common/value_conversion_utils.h"
#include "brave/components/constants/brave_services_key.h"
#include "brave/components/decentralized_dns/core/constants.h"
#include "brave/components/decentralized_dns/core/utils.h"
#include "brave/components/ipfs/ipfs_service.h"
#include "brave/components/ipfs/ipfs_utils.h"
#include "brave/components/ipfs/pref_names.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/browser/storage_partition.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "services/data_decoder/public/cpp/test_support/in_process_data_decoder.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"
#include "url/origin.h"

using testing::ElementsAreArray;

MATCHER_P(MatchesCIDv1URL, ipfs_url, "") {
  return ipfs::ContentHashToCIDv1URL(arg).spec() == ipfs_url;
}

namespace brave_wallet {

namespace {

void GetErrorCodeMessage(base::Value formed_response,
                         mojom::ProviderError* error,
                         std::string* error_message) {
  if (!formed_response.is_dict()) {
    *error = mojom::ProviderError::kSuccess;
    error_message->clear();
    return;
  }
  const base::Value* code = formed_response.FindKey("code");
  if (code) {
    *error = static_cast<mojom::ProviderError>(code->GetInt());
  }
  const base::Value* message = formed_response.FindKey("message");
  if (message) {
    *error_message = message->GetString();
  }
}

std::string GetGasFilEstimateResponse(int64_t value) {
  std::string response =
      R"({
          "id": 1,
          "jsonrpc": "2.0",
          "result": {
              "CID": {
                "/": "bafy2bzacebefvj6623fkmfwazpvg7qxgomhicefeb6tunc7wbvd2ee4uppfkw"
              },
              "From": "t1h5tg3bhp5r56uzgjae2373znti6ygq4agkx4hzq",
              "GasFeeCap": "101520",
              "GasLimit": {gas_limit},
              "GasPremium": "100466",
              "Method": 0,
              "Nonce": 1,
              "Params": "",
              "To": "t1tquwkjo6qvweah2g2yikewr7y5dyjds42pnrn3a",
              "Value": "1000000000000000000",
              "Version": 0
          }
      })";
  base::ReplaceSubstringsAfterOffset(&response, 0, "{gas_limit}",
                                     std::to_string(value));
  return response;
}

std::string GetFilStateSearchMsgLimitedResponse(int64_t value) {
  std::string response =
      R"({
        "id": 1,
        "jsonrpc": "2.0",
        "result":{
            "Height": 22389,
            "Message":
            {
                "/": "bafy2bzacebundyopm3trenj47hxkwiqn2cbvvftz3fss4dxuttu2u6xbbtkqy"
            },
            "Receipt":
            {
                "ExitCode": {exit_code},
                "GasUsed": 1749648,
                "Return": null
            },
            "ReturnDec": null,
            "TipSet":
            [
                {
                    "/": "bafy2bzacednkg6htmwwlkewl5wr2nezsovfgx5xb56l2uthz32uraqlmtsuzc"
                }
            ]
        }
      }
    )";
  base::ReplaceSubstringsAfterOffset(&response, 0, "{exit_code}",
                                     std::to_string(value));
  return response;
}

void UpdateCustomNetworks(PrefService* prefs,
                          std::vector<base::Value::Dict>* values) {
  DictionaryPrefUpdate update(prefs, kBraveWalletCustomNetworks);
  base::Value* dict = update.Get();
  ASSERT_TRUE(dict);
  base::Value* list = dict->FindKey(kEthereumPrefKey);
  if (!list) {
    list = dict->SetKey(kEthereumPrefKey, base::Value(base::Value::Type::LIST));
  }
  ASSERT_TRUE(list);
  auto& list_value = list->GetList();
  list_value.clear();
  for (auto& it : *values) {
    list_value.Append(std::move(it));
  }
}

void OnRequestResponse(bool* callback_called,
                       bool expected_success,
                       const std::string& expected_response,
                       base::Value id,
                       base::Value formed_response,
                       const bool reject,
                       const std::string& first_allowed_account,
                       const bool update_bind_js_properties) {
  *callback_called = true;
  std::string response;
  base::JSONWriter::Write(formed_response, &response);
  mojom::ProviderError error = mojom::ProviderError::kUnknown;
  std::string error_message;
  GetErrorCodeMessage(std::move(formed_response), &error, &error_message);
  bool success = error == brave_wallet::mojom::ProviderError::kSuccess;
  EXPECT_EQ(expected_success, success);
  if (!success) {
    response = "";
  }
  EXPECT_EQ(expected_response, response);
}

void OnStringResponse(bool* callback_called,
                      brave_wallet::mojom::ProviderError expected_error,
                      const std::string& expected_error_message,
                      const std::string& expected_response,
                      const std::string& response,
                      brave_wallet::mojom::ProviderError error,
                      const std::string& error_message) {
  *callback_called = true;
  EXPECT_EQ(expected_response, response);
  EXPECT_EQ(expected_error, error);
  EXPECT_EQ(expected_error_message, error_message);
}

void OnBoolResponse(bool* callback_called,
                    brave_wallet::mojom::ProviderError expected_error,
                    const std::string& expected_error_message,
                    bool expected_response,
                    bool response,
                    brave_wallet::mojom::ProviderError error,
                    const std::string& error_message) {
  *callback_called = true;
  EXPECT_EQ(expected_response, response);
  EXPECT_EQ(expected_error, error);
  EXPECT_EQ(expected_error_message, error_message);
}

void OnEthUint256Response(bool* callback_called,
                          brave_wallet::mojom::ProviderError expected_error,
                          const std::string& expected_error_message,
                          uint256_t expected_response,
                          uint256_t response,
                          brave_wallet::mojom::ProviderError error,
                          const std::string& error_message) {
  *callback_called = true;
  EXPECT_EQ(expected_response, response);
  EXPECT_EQ(expected_error, error);
  EXPECT_EQ(expected_error_message, error_message);
}

void OnFilUint256Response(
    bool* callback_called,
    brave_wallet::mojom::FilecoinProviderError expected_error,
    const std::string& expected_error_message,
    uint256_t expected_response,
    uint256_t response,
    brave_wallet::mojom::FilecoinProviderError error,
    const std::string& error_message) {
  *callback_called = true;
  EXPECT_EQ(expected_response, response);
  EXPECT_EQ(expected_error, error);
  EXPECT_EQ(expected_error_message, error_message);
}

class TestJsonRpcServiceObserver
    : public brave_wallet::mojom::JsonRpcServiceObserver {
 public:
  TestJsonRpcServiceObserver(base::OnceClosure callback,
                             const std::string& expected_chain_id,
                             mojom::CoinType expected_coin,
                             const std::string& expected_error) {
    callback_ = std::move(callback);
    expected_chain_id_ = expected_chain_id;
    expected_coin_ = expected_coin;
    expected_error_ = expected_error;
  }

  TestJsonRpcServiceObserver(const std::string& expected_chain_id,
                             mojom::CoinType expected_coin,
                             bool expected_is_eip1559) {
    expected_chain_id_ = expected_chain_id;
    expected_coin_ = expected_coin;
    expected_is_eip1559_ = expected_is_eip1559;
  }

  void Reset(const std::string& expected_chain_id, bool expected_is_eip1559) {
    expected_chain_id_ = expected_chain_id;
    expected_is_eip1559_ = expected_is_eip1559;
    chain_changed_called_ = false;
    is_eip1559_changed_called_ = false;
  }

  void OnAddEthereumChainRequestCompleted(const std::string& chain_id,
                                          const std::string& error) override {
    EXPECT_EQ(chain_id, expected_chain_id_);
    EXPECT_EQ(error, expected_error_);
    std::move(callback_).Run();
  }

  void ChainChangedEvent(const std::string& chain_id,
                         mojom::CoinType coin) override {
    chain_changed_called_ = true;
    EXPECT_EQ(chain_id, expected_chain_id_);
    EXPECT_EQ(coin, expected_coin_);
  }

  void OnIsEip1559Changed(const std::string& chain_id,
                          bool is_eip1559) override {
    is_eip1559_changed_called_ = true;
    EXPECT_EQ(chain_id, expected_chain_id_);
    EXPECT_EQ(is_eip1559, expected_is_eip1559_);
  }

  bool is_eip1559_changed_called() {
    base::RunLoop().RunUntilIdle();
    return is_eip1559_changed_called_;
  }

  bool chain_changed_called() {
    base::RunLoop().RunUntilIdle();
    return chain_changed_called_;
  }

  ::mojo::PendingRemote<brave_wallet::mojom::JsonRpcServiceObserver>
  GetReceiver() {
    return observer_receiver_.BindNewPipeAndPassRemote();
  }

  base::OnceClosure callback_;
  std::string expected_chain_id_;
  mojom::CoinType expected_coin_;
  std::string expected_error_;
  bool expected_is_eip1559_;
  bool chain_changed_called_ = false;
  bool is_eip1559_changed_called_ = false;
  mojo::Receiver<brave_wallet::mojom::JsonRpcServiceObserver>
      observer_receiver_{this};
};

constexpr char https_metadata_response[] =
    R"({"attributes":[{"trait_type":"Feet","value":"Green Shoes"},{"trait_type":"Legs","value":"Tan Pants"},{"trait_type":"Suspenders","value":"White Suspenders"},{"trait_type":"Upper Body","value":"Indigo Turtleneck"},{"trait_type":"Sleeves","value":"Long Sleeves"},{"trait_type":"Hat","value":"Yellow / Blue Pointy Beanie"},{"trait_type":"Eyes","value":"White Nerd Glasses"},{"trait_type":"Mouth","value":"Toothpick"},{"trait_type":"Ears","value":"Bing Bong Stick"},{"trait_type":"Right Arm","value":"Swinging"},{"trait_type":"Left Arm","value":"Diamond Hand"},{"trait_type":"Background","value":"Blue"}],"description":"5,000 animated Invisible Friends hiding in the metaverse. A collection by Markus Magnusson & Random Character Collective.","image":"https://rcc.mypinata.cloud/ipfs/QmXmuSenZRnofhGMz2NyT3Yc4Zrty1TypuiBKDcaBsNw9V/1817.gif","name":"Invisible Friends #1817"})";

absl::optional<base::Value> ToValue(const network::ResourceRequest& request) {
  base::StringPiece request_string(request.request_body->elements()
                                       ->at(0)
                                       .As<network::DataElementBytes>()
                                       .AsStringPiece());
  return base::JSONReader::Read(request_string,
                                base::JSONParserOptions::JSON_PARSE_RFC);
}

std::vector<brave_wallet::mojom::NetworkInfoPtr> GetAllEthCustomChains(
    PrefService* prefs) {
  return GetAllCustomChains(prefs, brave_wallet::mojom::CoinType::ETH);
}

}  // namespace

class JsonRpcServiceUnitTest : public testing::Test {
 public:
  JsonRpcServiceUnitTest() = default;

  void SetUp() override {
    Test::SetUp();
    shared_url_loader_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &url_loader_factory_);
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(
              brave_wallet::GetNetworkURL(prefs(), mojom::kLocalhostChainId,
                                          mojom::CoinType::ETH)
                  .spec(),
              "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
              "\"0x000000000000000000000000000000000000000000000000000000000000"
              "0020000000000000000000000000000000000000000000000000000000000000"
              "0026e3010170122008ab7bf21b73828364305ef6b7c676c1f5a73e18ab4f93be"
              "ec7e21e0bc84010e000000000000000000000000000000000000000000000000"
              "0000\"}");
        }));

    decentralized_dns::RegisterLocalStatePrefs(local_state_prefs_.registry());
    brave_wallet::RegisterProfilePrefs(prefs_.registry());
    brave_wallet::RegisterProfilePrefsForMigration(prefs_.registry());
    ipfs::IpfsService::RegisterProfilePrefs(prefs_.registry());
    json_rpc_service_ = std::make_unique<JsonRpcService>(
        shared_url_loader_factory_, &prefs_, &local_state_prefs_);
    SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH);
    SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::SOL);
    SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL);
  }

  ~JsonRpcServiceUnitTest() override = default;

  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory() {
    return shared_url_loader_factory_;
  }

  PrefService* prefs() { return &prefs_; }
  PrefService* local_state_prefs() { return &local_state_prefs_; }

  GURL GetNetwork(const std::string& chain_id, mojom::CoinType coin) {
    return brave_wallet::GetNetworkURL(prefs(), chain_id, coin);
  }

  bool GetIsEip1559FromPrefs(const std::string& chain_id) {
    if (chain_id == mojom::kLocalhostChainId)
      return prefs()->GetBoolean(kSupportEip1559OnLocalhostChain);
    const base::Value* custom_networks =
        prefs()
            ->GetDictionary(kBraveWalletCustomNetworks)
            ->FindKey(kEthereumPrefKey);
    if (!custom_networks)
      return false;

    for (const auto& chain : custom_networks->GetList()) {
      if (!chain.is_dict())
        continue;

      const std::string* id = chain.FindStringKey("chainId");
      if (!id || *id != chain_id)
        continue;

      return chain.FindBoolKey("is_eip1559").value_or(false);
    }

    return false;
  }
  void SetEthChainIdInterceptor(const GURL& network_url,
                                const std::string& chain_id) {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, network_url, chain_id](const network::ResourceRequest& request) {
          base::StringPiece request_string(request.request_body->elements()
                                               ->at(0)
                                               .As<network::DataElementBytes>()
                                               .AsStringPiece());
          url_loader_factory_.ClearResponses();
          if (request_string.find("eth_chainId") != std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url.spec(),
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"" + chain_id +
                    "\"}");
          }
        }));
  }
  void SetEthChainIdInterceptorWithBrokenResponse(const GURL& network_url) {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, network_url](const network::ResourceRequest& request) {
          base::StringPiece request_string(request.request_body->elements()
                                               ->at(0)
                                               .As<network::DataElementBytes>()
                                               .AsStringPiece());
          url_loader_factory_.ClearResponses();
          if (request_string.find("eth_chainId") != std::string::npos) {
            url_loader_factory_.AddResponse(network_url.spec(),
                                            "{\"jsonrpc\":\"");
          }
        }));
  }

  void SetUDENSInterceptor(const std::string& chain_id) {
    GURL network_url = AddInfuraProjectId(
        GetNetworkURL(prefs(), chain_id, mojom::CoinType::ETH));
    ASSERT_TRUE(network_url.is_valid());
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, network_url](const network::ResourceRequest& request) {
          base::StringPiece request_string(request.request_body->elements()
                                               ->at(0)
                                               .As<network::DataElementBytes>()
                                               .AsStringPiece());
          url_loader_factory_.ClearResponses();
          if (request_string.find(GetFunctionHash("resolver(bytes32)")) !=
              std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url.spec(),
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                "\"0x0000000000000000000000004976fb03c32e5b8cfe2b6ccb31c09ba78e"
                "baba41\"}");
          } else if (request_string.find(GetFunctionHash(
                         "contenthash(bytes32)")) != std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url.spec(),
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                "\"0x0000000000000000000000000000000000000000000000000000000000"
                "00002000000000000000000000000000000000000000000000000000000000"
                "00000026e3010170122023e0160eec32d7875c19c5ac7c03bc1f306dc26008"
                "0d621454bc5f631e7310a70000000000000000000000000000000000000000"
                "000000000000\"}");
          } else if (request_string.find(GetFunctionHash("addr(bytes32)")) !=
                     std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url.spec(),
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                "\"0x000000000000000000000000983110309620d911731ac0932219af0609"
                "1b6744\"}");
          } else if (request_string.find(GetFunctionHash(
                         "get(string,uint256)")) != std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url.spec(),
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                "\"0x0000000000000000000000000000000000000000000000000000000000"
                "00002000000000000000000000000000000000000000000000000000000000"
                "0000002a307838616144343433323141383662313730383739643741323434"
                "63316538643336306339394464413800000000000000000000000000000000"
                "000000000000\"}");
          } else {
            url_loader_factory_.AddResponse(request.url.spec(), "",
                                            net::HTTP_REQUEST_TIMEOUT);
          }
        }));
  }

  void SetENSZeroAddressInterceptor(const std::string& chain_id) {
    GURL network_url = AddInfuraProjectId(
        GetNetworkURL(prefs(), chain_id, mojom::CoinType::ETH));
    ASSERT_TRUE(network_url.is_valid());
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, network_url](const network::ResourceRequest& request) {
          base::StringPiece request_string(request.request_body->elements()
                                               ->at(0)
                                               .As<network::DataElementBytes>()
                                               .AsStringPiece());
          url_loader_factory_.ClearResponses();
          if (request_string.find(GetFunctionHash("resolver(bytes32)")) !=
              std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url.spec(),
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                "\"0x0000000000000000000000004976fb03c32e5b8cfe2b6ccb31c09ba78e"
                "baba41\"}");
          } else if (request_string.find(GetFunctionHash("addr(bytes32)")) !=
                     std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url.spec(),
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                "\"0x0000000000000000000000000000000000000000000000000000000000"
                "000000\"}");
          } else {
            url_loader_factory_.AddResponse(request.url.spec(), "",
                                            net::HTTP_REQUEST_TIMEOUT);
          }
        }));
  }

  void SetTokenMetadataInterceptor(
      const std::string& interface_id,
      const std::string& chain_id,
      const std::string& supports_interface_provider_response,
      const std::string& token_uri_provider_response = "",
      const std::string& metadata_response = "",
      net::HttpStatusCode supports_interface_status = net::HTTP_OK,
      net::HttpStatusCode token_uri_status = net::HTTP_OK,
      net::HttpStatusCode metadata_status = net::HTTP_OK) {
    GURL network_url = GetNetworkURL(prefs(), chain_id, mojom::CoinType::ETH);
    ASSERT_TRUE(network_url.is_valid());
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, interface_id, supports_interface_provider_response,
         token_uri_provider_response, metadata_response,
         supports_interface_status, token_uri_status, metadata_status,
         network_url](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          if (request.method ==
              "POST") {  // An eth_call, either to supportsInterface or tokenURI
            base::StringPiece request_string(
                request.request_body->elements()
                    ->at(0)
                    .As<network::DataElementBytes>()
                    .AsStringPiece());
            bool is_supports_interface_req =
                request_string.find(GetFunctionHash(
                    "supportsInterface(bytes4)")) != std::string::npos;
            if (is_supports_interface_req) {
              ASSERT_NE(request_string.find(interface_id.substr(2)),
                        std::string::npos);
              EXPECT_EQ(request.url.spec(), network_url);
              url_loader_factory_.AddResponse(
                  network_url.spec(), supports_interface_provider_response,
                  supports_interface_status);
              return;
            } else {
              std::string function_hash;
              if (interface_id == kERC721MetadataInterfaceId) {
                function_hash = GetFunctionHash("tokenURI(uint256)");
              } else {
                function_hash = GetFunctionHash("uri(uint256)");
              }
              ASSERT_NE(request_string.find(function_hash), std::string::npos);
              url_loader_factory_.AddResponse(network_url.spec(),
                                              token_uri_provider_response,
                                              token_uri_status);
              return;
            }
          } else {  // A HTTP GET to fetch the metadata json from the web
            url_loader_factory_.AddResponse(request.url.spec(),
                                            metadata_response, metadata_status);
            return;
          }
        }));
  }

  void SetInterceptor(const GURL& expected_url,
                      const std::string& expected_method,
                      const std::string& expected_cache_header,
                      const std::string& content) {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, expected_url, expected_method, expected_cache_header,
         content](const network::ResourceRequest& request) {
          EXPECT_EQ(request.url, expected_url);
          std::string header_value;
          EXPECT_EQ(request.headers.GetHeader("X-Eth-Method", &header_value),
                    !expected_method.empty());
          EXPECT_EQ(expected_method, header_value);
          if (expected_method == "eth_blockNumber") {
            EXPECT_TRUE(
                request.headers.GetHeader("X-Eth-Block", &header_value));
            EXPECT_EQ(expected_cache_header, header_value);
          } else if (expected_method == "eth_getBlockByNumber") {
            EXPECT_EQ(
                request.headers.GetHeader("X-eth-get-block", &header_value),
                !expected_cache_header.empty());
            EXPECT_EQ(expected_cache_header, header_value);
          }
          EXPECT_TRUE(request.headers.GetHeader("x-brave-key", &header_value));
          EXPECT_EQ(BUILDFLAG(BRAVE_SERVICES_KEY), header_value);
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(request.url.spec(), content);
        }));
  }

  void SetInvalidJsonInterceptor() {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(request.url.spec(), "Answer is 42");
        }));
  }

  void SetHTTPRequestTimeoutInterceptor() {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(request.url.spec(), "",
                                          net::HTTP_REQUEST_TIMEOUT);
        }));
  }

  void SetFilecoinActorErrorJsonErrorResponse() {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(request.url.spec(),
                                          R"({
            "jsonrpc":"2.0",
            "id":1,
            "error": {
              "code": 1,
              "message": "resolution lookup failed"
            }
          })");
        }));
  }

  void SetLimitExceededJsonErrorResponse() {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(request.url.spec(),
                                          R"({
            "jsonrpc":"2.0",
            "id":1,
            "error": {
              "code":-32005,
              "message": "Request exceeds defined limit"
            }
          })");
        }));
  }

  void SetIsEip1559Interceptor(const GURL& expected_network, bool is_eip1559) {
    if (is_eip1559)
      SetInterceptor(
          expected_network, "eth_getBlockByNumber", "latest,false",
          "{\"jsonrpc\":\"2.0\",\"id\": \"0\",\"result\": "
          "{\"baseFeePerGas\":\"0x181f22e7a9\", \"gasLimit\":\"0x6691b8\"}}");
    else
      SetInterceptor(expected_network, "eth_getBlockByNumber", "latest,false",
                     "{\"jsonrpc\":\"2.0\",\"id\": \"0\",\"result\": "
                     "{\"gasLimit\":\"0x6691b8\"}}");
  }

  void ValidateStartWithNetwork(const std::string& chain_id,
                                const std::string& expected_id) {
    DictionaryPrefUpdate update(prefs(), kBraveWalletSelectedNetworks);
    base::Value* dict = update.Get();
    DCHECK(dict);
    dict->SetStringKey(kEthereumPrefKey, chain_id);
    JsonRpcService service(shared_url_loader_factory(), prefs());
    bool callback_is_called = false;
    service.GetChainId(
        mojom::CoinType::ETH,
        base::BindLambdaForTesting(
            [&callback_is_called, &expected_id](const std::string& chain_id) {
              EXPECT_EQ(chain_id, expected_id);
              callback_is_called = true;
            }));
    ASSERT_TRUE(callback_is_called);
  }

  bool SetNetwork(const std::string& chain_id, mojom::CoinType coin) {
    bool result;
    base::RunLoop run_loop;
    json_rpc_service_->SetNetwork(chain_id, coin,
                                  base::BindLambdaForTesting([&](bool success) {
                                    result = success;
                                    run_loop.Quit();
                                  }));
    run_loop.Run();
    return result;
  }

  void TestGetERC1155TokenBalance(const std::string& contract,
                                  const std::string& token_id,
                                  const std::string& account_address,
                                  const std::string& chain_id,
                                  const std::string& expected_response,
                                  mojom::ProviderError expected_error,
                                  const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetERC1155TokenBalance(
        contract, token_id, account_address, chain_id,
        base::BindLambdaForTesting([&](const std::string& response,
                                       mojom::ProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(response, expected_response);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void TestGetERC721Metadata(const std::string& contract,
                             const std::string& token_id,
                             const std::string& chain_id,
                             const std::string& expected_response,
                             mojom::ProviderError expected_error,
                             const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetERC721Metadata(
        contract, token_id, chain_id,
        base::BindLambdaForTesting([&](const std::string& response,
                                       mojom::ProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(response, expected_response);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void TestGetERC1155Metadata(const std::string& contract,
                              const std::string& token_id,
                              const std::string& chain_id,
                              const std::string& expected_response,
                              mojom::ProviderError expected_error,
                              const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetERC1155Metadata(
        contract, token_id, chain_id,
        base::BindLambdaForTesting([&](const std::string& response,
                                       mojom::ProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(response, expected_response);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void TestGetTokenMetadata(const std::string& contract,
                            const std::string& token_id,
                            const std::string& chain_id,
                            const std::string& interface_id,
                            const std::string& expected_response,
                            mojom::ProviderError expected_error,
                            const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetTokenMetadata(
        contract, token_id, chain_id, interface_id,
        base::BindLambdaForTesting([&](const std::string& response,
                                       mojom::ProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(response, expected_response);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void TestDiscoverAssets(
      const std::string& chain_id,
      const std::vector<std::string>& account_addresses,
      const std::vector<std::string>& expected_token_contract_addresses,
      std::vector<mojom::BlockchainTokenPtr> existing_user_assets,
      mojom::ProviderError expected_error,
      const std::string& expected_error_message) {
    base::RunLoop run_loop;
    std::vector<mojom::BlockchainTokenPtr> expected_tokens;
    json_rpc_service_->DiscoverAssets(
        chain_id, account_addresses, std::move(existing_user_assets),
        base::BindLambdaForTesting(
            [&](const std::vector<mojom::BlockchainTokenPtr> tokens,
                mojom::ProviderError error, const std::string& error_message) {
              EXPECT_EQ(tokens.size(),
                        expected_token_contract_addresses.size());
              for (size_t i = 0; i < expected_token_contract_addresses.size();
                   i++) {
                EXPECT_EQ(tokens[i]->contract_address,
                          expected_token_contract_addresses[i]);
              }
              EXPECT_EQ(error, expected_error);
              EXPECT_EQ(error_message, expected_error_message);
              run_loop.Quit();
            }));
    run_loop.Run();
  }

  void TestGetSolanaBalance(uint64_t expected_balance,
                            mojom::SolanaProviderError expected_error,
                            const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSolanaBalance(
        "test_public_key", mojom::kSolanaMainnet,
        base::BindLambdaForTesting([&](uint64_t balance,
                                       mojom::SolanaProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(balance, expected_balance);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }
  void GetFilBlockHeight(uint64_t expected_height,
                         mojom::FilecoinProviderError expected_error,
                         const std::string& expected_error_message) {
    bool callback_called = false;
    base::RunLoop run_loop;
    json_rpc_service_->GetFilBlockHeight(base::BindLambdaForTesting(
        [&](uint64_t height, mojom::FilecoinProviderError error,
            const std::string& error_message) {
          EXPECT_EQ(height, expected_height);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          callback_called = true;
          run_loop.Quit();
        }));
    run_loop.Run();
    EXPECT_TRUE(callback_called);
  }
  void GetFilStateSearchMsgLimited(const std::string& cid,
                                   uint64_t period,
                                   int64_t expected_exit_code,
                                   mojom::FilecoinProviderError expected_error,
                                   const std::string& expected_error_message) {
    bool callback_called = false;
    base::RunLoop run_loop;
    json_rpc_service_->GetFilStateSearchMsgLimited(
        cid, period,
        base::BindLambdaForTesting([&](int64_t exit_code,
                                       mojom::FilecoinProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(exit_code, expected_exit_code);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          callback_called = true;
          run_loop.Quit();
        }));
    run_loop.Run();
    EXPECT_TRUE(callback_called);
  }
  void GetSendFilecoinTransaction(const std::string& signed_tx,
                                  const std::string& expected_cid,
                                  mojom::FilecoinProviderError expected_error,
                                  const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->SendFilecoinTransaction(
        signed_tx,
        base::BindLambdaForTesting([&](const std::string& cid,
                                       mojom::FilecoinProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(cid, expected_cid);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);

          run_loop.Quit();
        }));
    run_loop.Run();
  }
  void TestGetSPLTokenAccountBalance(
      const std::string& expected_amount,
      uint8_t expected_decimals,
      const std::string& expected_ui_amount_string,
      mojom::SolanaProviderError expected_error,
      const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSPLTokenAccountBalance(
        "BrG44HdsEhzapvs8bEqzvkq4egwevS3fRE6ze2ENo6S8",
        "AQoKYV7tYpTrFZN6P5oUufbQKAUr9mNYGe1TTJC9wajM", mojom::kSolanaMainnet,
        base::BindLambdaForTesting([&](const std::string& amount,
                                       uint8_t decimals,
                                       const std::string& ui_amount_string,
                                       mojom::SolanaProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(amount, expected_amount);
          EXPECT_EQ(decimals, expected_decimals);
          EXPECT_EQ(ui_amount_string, expected_ui_amount_string);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void TestSendSolanaTransaction(const std::string& expected_tx_id,
                                 mojom::SolanaProviderError expected_error,
                                 const std::string& expected_error_message,
                                 const std::string& signed_tx = "signed_tx") {
    base::RunLoop run_loop;
    json_rpc_service_->SendSolanaTransaction(
        signed_tx, absl::nullopt,
        base::BindLambdaForTesting([&](const std::string& tx_id,
                                       mojom::SolanaProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(tx_id, expected_tx_id);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void TestGetSolanaLatestBlockhash(const std::string& expected_hash,
                                    uint64_t expected_last_valid_block_height,
                                    mojom::SolanaProviderError expected_error,
                                    const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSolanaLatestBlockhash(base::BindLambdaForTesting(
        [&](const std::string& hash, uint64_t last_valid_block_height,
            mojom::SolanaProviderError error,
            const std::string& error_message) {
          EXPECT_EQ(hash, expected_hash);
          EXPECT_EQ(last_valid_block_height, expected_last_valid_block_height);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void TestGetSolanaSignatureStatuses(
      const std::vector<std::string>& tx_signatures,
      const std::vector<absl::optional<SolanaSignatureStatus>>& expected_stats,
      mojom::SolanaProviderError expected_error,
      const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSolanaSignatureStatuses(
        tx_signatures,
        base::BindLambdaForTesting(
            [&](const std::vector<absl::optional<SolanaSignatureStatus>>& stats,
                mojom::SolanaProviderError error,
                const std::string& error_message) {
              EXPECT_EQ(stats, expected_stats);
              EXPECT_EQ(error, expected_error);
              EXPECT_EQ(error_message, expected_error_message);
              run_loop.Quit();
            }));
    run_loop.Run();
  }

  void TestGetSolanaAccountInfo(
      absl::optional<SolanaAccountInfo> expected_account_info,
      mojom::SolanaProviderError expected_error,
      const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSolanaAccountInfo(
        "vines1vzrYbzLMRdu58ou5XTby4qAqVRLmqo36NKPTg",
        base::BindLambdaForTesting(
            [&](absl::optional<SolanaAccountInfo> account_info,
                mojom::SolanaProviderError error,
                const std::string& error_message) {
              EXPECT_EQ(account_info, expected_account_info);
              EXPECT_EQ(error, expected_error);
              EXPECT_EQ(error_message, expected_error_message);
              run_loop.Quit();
            }));
    run_loop.Run();
  }

  void TestGetSolanaFeeForMessage(const std::string& message,
                                  uint64_t expected_tx_fee,
                                  mojom::SolanaProviderError expected_error,
                                  const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSolanaFeeForMessage(
        message, base::BindLambdaForTesting(
                     [&](uint64_t tx_fee, mojom::SolanaProviderError error,
                         const std::string& error_message) {
                       EXPECT_EQ(tx_fee, expected_tx_fee);
                       EXPECT_EQ(error, expected_error);
                       EXPECT_EQ(error_message, expected_error_message);
                       run_loop.Quit();
                     }));
    run_loop.Run();
  }

  void TestGetSolanaBlockHeight(uint64_t expected_block_height,
                                mojom::SolanaProviderError expected_error,
                                const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSolanaBlockHeight(base::BindLambdaForTesting(
        [&](uint64_t block_height, mojom::SolanaProviderError error,
            const std::string& error_message) {
          EXPECT_EQ(block_height, expected_block_height);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void GetFilEstimateGas(const std::string& from,
                         const std::string& to,
                         const std::string& value,
                         const std::string& expected_gas_premium,
                         const std::string& expected_gas_fee_cap,
                         int64_t expected_gas_limit,
                         mojom::FilecoinProviderError expected_error) {
    base::RunLoop loop;
    json_rpc_service_->GetFilEstimateGas(
        from, to, "", "", 0, 0, "", value,
        base::BindLambdaForTesting(
            [&](const std::string& gas_premium, const std::string& gas_fee_cap,
                int64_t gas_limit, mojom::FilecoinProviderError error,
                const std::string& error_message) {
              EXPECT_EQ(gas_premium, expected_gas_premium);
              EXPECT_EQ(gas_fee_cap, expected_gas_fee_cap);
              EXPECT_EQ(gas_limit, expected_gas_limit);
              EXPECT_EQ(error, expected_error);
              bool success =
                  mojom::FilecoinProviderError::kSuccess == expected_error;
              EXPECT_EQ(error_message.empty(), success);
              loop.Quit();
            }));
    loop.Run();
  }

  void AddEthereumChainForOrigin(mojom::NetworkInfoPtr chain,
                                 const url::Origin& origin,
                                 const std::string& expected_chain_id,
                                 mojom::ProviderError expected_error,
                                 const std::string& expected_error_message) {
    base::RunLoop loop;
    json_rpc_service_->AddEthereumChainForOrigin(
        std::move(chain), origin,
        base::BindLambdaForTesting([&](const std::string& chain_id,
                                       mojom::ProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(chain_id, expected_chain_id);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          loop.Quit();
        }));

    loop.Run();
  }

 protected:
  std::unique_ptr<JsonRpcService> json_rpc_service_;
  network::TestURLLoaderFactory url_loader_factory_;

 private:
  base::test::TaskEnvironment task_environment_;
  sync_preferences::TestingPrefServiceSyncable prefs_;
  sync_preferences::TestingPrefServiceSyncable local_state_prefs_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory_;
  data_decoder::test::InProcessDataDecoder in_process_data_decoder_;
};

TEST_F(JsonRpcServiceUnitTest, SetNetwork) {
  for (const auto& network :
       brave_wallet::GetAllKnownChains(prefs(), mojom::CoinType::ETH)) {
    bool callback_is_called = false;
    EXPECT_TRUE(SetNetwork(network->chain_id, mojom::CoinType::ETH));

    EXPECT_EQ(network->chain_id,
              GetCurrentChainId(prefs(), mojom::CoinType::ETH));
    const std::string& expected_id = network->chain_id;
    json_rpc_service_->GetChainId(
        mojom::CoinType::ETH,
        base::BindLambdaForTesting(
            [&callback_is_called, &expected_id](const std::string& chain_id) {
              EXPECT_EQ(chain_id, expected_id);
              callback_is_called = true;
            }));
    ASSERT_TRUE(callback_is_called);

    callback_is_called = false;
    const GURL expected_url = GetActiveEndpointUrl(*network);
    json_rpc_service_->GetNetworkUrl(
        mojom::CoinType::ETH,
        base::BindLambdaForTesting(
            [&callback_is_called, &expected_url](const std::string& spec) {
              EXPECT_EQ(url::Origin::Create(GURL(spec)),
                        url::Origin::Create(expected_url));
              callback_is_called = true;
            }));
    ASSERT_TRUE(callback_is_called);
  }
  base::RunLoop().RunUntilIdle();

  // Solana
  EXPECT_TRUE(SetNetwork(mojom::kSolanaMainnet, mojom::CoinType::SOL));
  ASSERT_EQ(mojom::kSolanaMainnet,
            GetCurrentChainId(prefs(), mojom::CoinType::SOL));
  EXPECT_FALSE(SetNetwork("0x1234", mojom::CoinType::SOL));
  EXPECT_TRUE(SetNetwork(mojom::kSolanaTestnet, mojom::CoinType::SOL));

  base::RunLoop run_loop;
  json_rpc_service_->GetChainId(
      mojom::CoinType::SOL,
      base::BindLambdaForTesting([&run_loop](const std::string& chain_id) {
        EXPECT_EQ(chain_id, mojom::kSolanaTestnet);
        run_loop.Quit();
      }));
  run_loop.Run();

  base::RunLoop run_loop2;
  json_rpc_service_->GetNetworkUrl(
      mojom::CoinType::SOL,
      base::BindLambdaForTesting([&run_loop2](const std::string& spec) {
        EXPECT_EQ(url::Origin::Create(GURL(spec)),
                  url::Origin::Create(GURL("https://api.testnet.solana.com")));
        run_loop2.Quit();
      }));
  run_loop2.Run();
}

TEST_F(JsonRpcServiceUnitTest, SetCustomNetwork) {
  std::vector<base::Value::Dict> values;
  mojom::NetworkInfo chain1 = GetTestNetworkInfo1();
  values.push_back(NetworkInfoToValue(chain1));

  mojom::NetworkInfo chain2 = GetTestNetworkInfo2();
  values.push_back(NetworkInfoToValue(chain2));
  UpdateCustomNetworks(prefs(), &values);

  bool callback_is_called = false;
  EXPECT_TRUE(SetNetwork(chain1.chain_id, mojom::CoinType::ETH));
  const std::string& expected_id = chain1.chain_id;
  json_rpc_service_->GetChainId(
      mojom::CoinType::ETH,
      base::BindLambdaForTesting(
          [&callback_is_called, &expected_id](const std::string& chain_id) {
            EXPECT_EQ(chain_id, expected_id);
            callback_is_called = true;
          }));
  ASSERT_TRUE(callback_is_called);
  callback_is_called = false;
  const GURL expected_url = GetActiveEndpointUrl(chain1);
  json_rpc_service_->GetNetworkUrl(
      mojom::CoinType::ETH,
      base::BindLambdaForTesting(
          [&callback_is_called, &expected_url](const std::string& spec) {
            EXPECT_EQ(url::Origin::Create(GURL(spec)),
                      url::Origin::Create(expected_url));
            callback_is_called = true;
          }));
  ASSERT_TRUE(callback_is_called);
  base::RunLoop().RunUntilIdle();
}

TEST_F(JsonRpcServiceUnitTest, GetAllNetworks) {
  std::vector<base::Value::Dict> values;
  mojom::NetworkInfo chain1 = GetTestNetworkInfo1();
  values.push_back(NetworkInfoToValue(chain1));

  mojom::NetworkInfo chain2 = GetTestNetworkInfo2();
  values.push_back(NetworkInfoToValue(chain2));
  UpdateCustomNetworks(prefs(), &values);

  std::vector<mojom::NetworkInfoPtr> expected_chains =
      GetAllChains(prefs(), mojom::CoinType::ETH);
  bool callback_is_called = false;
  json_rpc_service_->GetAllNetworks(
      mojom::CoinType::ETH,
      base::BindLambdaForTesting(
          [&callback_is_called,
           &expected_chains](std::vector<mojom::NetworkInfoPtr> chains) {
            EXPECT_EQ(expected_chains.size(), chains.size());

            for (size_t i = 0; i < chains.size(); i++) {
              ASSERT_TRUE(chains.at(i).Equals(expected_chains.at(i)));
            }
            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(callback_is_called);

  callback_is_called = false;
  json_rpc_service_->GetAllNetworks(
      mojom::CoinType::SOL,
      base::BindLambdaForTesting(
          [&callback_is_called](std::vector<mojom::NetworkInfoPtr> chains) {
            EXPECT_EQ(chains.size(), 4u);

            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(callback_is_called);
}

TEST_F(JsonRpcServiceUnitTest, GetCustomNetworks) {
  base::MockCallback<mojom::JsonRpcService::GetCustomNetworksCallback> callback;
  std::vector<base::Value::Dict> values;
  mojom::NetworkInfo chain1 = GetTestNetworkInfo1(mojom::kMainnetChainId);
  values.push_back(NetworkInfoToValue(chain1));

  mojom::NetworkInfo chain2 = GetTestNetworkInfo1("0x123456");
  values.push_back(NetworkInfoToValue(chain2));
  EXPECT_CALL(callback, Run(ElementsAreArray(std::vector<std::string>{})));
  json_rpc_service_->GetCustomNetworks(mojom::CoinType::ETH, callback.Get());
  testing::Mock::VerifyAndClearExpectations(&callback);
  UpdateCustomNetworks(prefs(), &values);

  EXPECT_CALL(callback, Run(ElementsAreArray({"0x1", "0x123456"})));
  json_rpc_service_->GetCustomNetworks(mojom::CoinType::ETH, callback.Get());
  testing::Mock::VerifyAndClearExpectations(&callback);
}

TEST_F(JsonRpcServiceUnitTest, GetKnownNetworks) {
  base::MockCallback<mojom::JsonRpcService::GetKnownNetworksCallback> callback;
  std::vector<base::Value::Dict> values;
  mojom::NetworkInfo chain1 = GetTestNetworkInfo1(mojom::kMainnetChainId);
  values.push_back(NetworkInfoToValue(chain1));
  UpdateCustomNetworks(prefs(), &values);

  EXPECT_CALL(callback,
              Run(ElementsAreArray({"0x1", "0x89", "0x38", "0xa4ec", "0xa86a",
                                    "0xfa", "0xa", "0x4e454152", "0x4", "0x3",
                                    "0x5", "0x2a", "0x539"})));
  json_rpc_service_->GetKnownNetworks(mojom::CoinType::ETH, callback.Get());
  testing::Mock::VerifyAndClearExpectations(&callback);
}

TEST_F(JsonRpcServiceUnitTest, GetHiddenNetworks) {
  base::MockCallback<mojom::JsonRpcService::GetHiddenNetworksCallback> callback;

  EXPECT_CALL(callback, Run(ElementsAreArray<std::string>({})));
  json_rpc_service_->GetHiddenNetworks(mojom::CoinType::ETH, callback.Get());
  testing::Mock::VerifyAndClearExpectations(&callback);

  AddHiddenNetwork(prefs(), mojom::CoinType::ETH, "0x123");
  EXPECT_CALL(callback, Run(ElementsAreArray({"0x123"})));
  json_rpc_service_->GetHiddenNetworks(mojom::CoinType::ETH, callback.Get());
  testing::Mock::VerifyAndClearExpectations(&callback);

  RemoveHiddenNetwork(prefs(), mojom::CoinType::ETH, "0x123");
  EXPECT_CALL(callback, Run(ElementsAreArray<std::string>({})));
  json_rpc_service_->GetHiddenNetworks(mojom::CoinType::ETH, callback.Get());
  testing::Mock::VerifyAndClearExpectations(&callback);
}

TEST_F(JsonRpcServiceUnitTest, EnsGetContentHash) {
  {
    base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
    EXPECT_CALL(
        callback,
        Run(MatchesCIDv1URL(
                "ipfs://"
                "bafybeibd4ala53bs26dvygofvr6ahpa7gbw4eyaibvrbivf4l5rr44yqu4"),
            false, mojom::ProviderError::kSuccess, ""));

    SetUDENSInterceptor(mojom::kMainnetChainId);
    json_rpc_service_->EnsGetContentHash("brantly.eth", callback.Get());
    base::RunLoop().RunUntilIdle();
  }

  {
    base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
    EXPECT_CALL(
        callback,
        Run(std::vector<uint8_t>(), false, mojom::ProviderError::kInternalError,
            l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
    SetHTTPRequestTimeoutInterceptor();
    json_rpc_service_->EnsGetContentHash("brantly.eth", callback.Get());
    base::RunLoop().RunUntilIdle();
  }

  {
    base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
    EXPECT_CALL(
        callback,
        Run(std::vector<uint8_t>(), false, mojom::ProviderError::kParsingError,
            l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR)));
    SetInvalidJsonInterceptor();
    json_rpc_service_->EnsGetContentHash("brantly.eth", callback.Get());
    base::RunLoop().RunUntilIdle();
  }

  {
    base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
    EXPECT_CALL(callback, Run(std::vector<uint8_t>(), false,
                              mojom::ProviderError::kLimitExceeded,
                              "Request exceeds defined limit"));
    SetLimitExceededJsonErrorResponse();
    json_rpc_service_->EnsGetContentHash("brantly.eth", callback.Get());
    base::RunLoop().RunUntilIdle();
  }
}

TEST_F(JsonRpcServiceUnitTest, EnsGetEthAddr) {
  SetUDENSInterceptor(mojom::kMainnetChainId);
  EXPECT_TRUE(SetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH));

  base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
  EXPECT_CALL(callback, Run("0x983110309620D911731Ac0932219af06091b6744", false,
                            mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->EnsGetEthAddr("brantly-test.eth", nullptr, callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(JsonRpcServiceUnitTest, EnsGetEthAddr_ZeroAddress) {
  SetENSZeroAddressInterceptor(mojom::kMainnetChainId);
  EXPECT_TRUE(SetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH));

  base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
  EXPECT_CALL(callback,
              Run("", false, mojom::ProviderError::kInvalidParams,
                  l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS)));
  json_rpc_service_->EnsGetEthAddr("brantly-test.eth", nullptr, callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(JsonRpcServiceUnitTest, AddEthereumChainApproved) {
  mojom::NetworkInfo chain = GetTestNetworkInfo1("0x111");
  bool callback_is_called = false;
  mojom::ProviderError expected = mojom::ProviderError::kSuccess;
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());
  SetEthChainIdInterceptor(GetActiveEndpointUrl(chain), "0x111");
  json_rpc_service_->AddChain(
      chain.Clone(),
      base::BindLambdaForTesting(
          [&callback_is_called, &expected](const std::string& chain_id,
                                           mojom::ProviderError error,
                                           const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, expected);
            ASSERT_TRUE(error_message.empty());
            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();

  bool failed_callback_is_called = false;
  mojom::ProviderError expected_error =
      mojom::ProviderError::kUserRejectedRequest;
  json_rpc_service_->AddChain(
      chain.Clone(),
      base::BindLambdaForTesting([&failed_callback_is_called, &expected_error](
                                     const std::string& chain_id,
                                     mojom::ProviderError error,
                                     const std::string& error_message) {
        ASSERT_FALSE(chain_id.empty());
        EXPECT_EQ(error, expected_error);
        ASSERT_FALSE(error_message.empty());
        failed_callback_is_called = true;
      }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(failed_callback_is_called);

  json_rpc_service_->AddEthereumChainRequestCompleted("0x111", true);

  ASSERT_TRUE(callback_is_called);
  ASSERT_TRUE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());

  // Prefs should be updated.
  ASSERT_EQ(GetAllEthCustomChains(prefs()).size(), 1u);
  EXPECT_EQ(GetAllEthCustomChains(prefs())[0], chain.Clone());

  const base::Value* assets_pref =
      prefs()->GetDictionary(kBraveWalletUserAssets);
  const base::Value* list = assets_pref->FindPath("ethereum.0x111");
  ASSERT_TRUE(list->is_list());
  const base::Value::List& asset_list = list->GetList();
  ASSERT_EQ(asset_list.size(), 1u);

  EXPECT_EQ(*asset_list[0].FindStringKey("address"), "");
  EXPECT_EQ(*asset_list[0].FindStringKey("name"), "symbol_name");
  EXPECT_EQ(*asset_list[0].FindStringKey("symbol"), "symbol");
  EXPECT_EQ(*asset_list[0].FindBoolKey("is_erc20"), false);
  EXPECT_EQ(*asset_list[0].FindBoolKey("is_erc721"), false);
  EXPECT_EQ(*asset_list[0].FindIntKey("decimals"), 11);
  EXPECT_EQ(*asset_list[0].FindStringKey("logo"), "https://url1.com");
  EXPECT_EQ(*asset_list[0].FindBoolKey("visible"), true);

  callback_is_called = false;
  json_rpc_service_->AddEthereumChainRequestCompleted("0x111", true);
  ASSERT_FALSE(callback_is_called);
}

TEST_F(JsonRpcServiceUnitTest, AddEthereumChainApprovedForOrigin) {
  mojom::NetworkInfo chain = GetTestNetworkInfo1("0x111");

  base::RunLoop loop;
  std::unique_ptr<TestJsonRpcServiceObserver> observer(
      new TestJsonRpcServiceObserver(loop.QuitClosure(), "0x111",
                                     mojom::CoinType::ETH, ""));

  json_rpc_service_->AddObserver(observer->GetReceiver());

  mojo::PendingRemote<brave_wallet::mojom::JsonRpcServiceObserver> receiver;
  mojo::MakeSelfOwnedReceiver(std::move(observer),
                              receiver.InitWithNewPipeAndPassReceiver());

  bool callback_is_called = false;
  mojom::ProviderError expected = mojom::ProviderError::kSuccess;
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());
  SetEthChainIdInterceptor(GetActiveEndpointUrl(chain), "0x111");
  json_rpc_service_->AddEthereumChainForOrigin(
      chain.Clone(), url::Origin::Create(GURL("https://brave.com")),
      base::BindLambdaForTesting(
          [&callback_is_called, &expected](const std::string& chain_id,
                                           mojom::ProviderError error,
                                           const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, expected);
            ASSERT_TRUE(error_message.empty());
            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  json_rpc_service_->AddEthereumChainRequestCompleted("0x111", true);
  loop.Run();

  ASSERT_TRUE(callback_is_called);
  ASSERT_TRUE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());

  // Prefs should be updated.
  ASSERT_EQ(GetAllEthCustomChains(prefs()).size(), 1u);
  EXPECT_EQ(GetAllEthCustomChains(prefs())[0], chain.Clone());

  const base::Value* assets_pref =
      prefs()->GetDictionary(kBraveWalletUserAssets);
  const base::Value* list = assets_pref->FindPath("ethereum.0x111");
  ASSERT_TRUE(list->is_list());
  const base::Value::List& asset_list = list->GetList();
  ASSERT_EQ(asset_list.size(), 1u);

  EXPECT_EQ(*asset_list[0].FindStringKey("address"), "");
  EXPECT_EQ(*asset_list[0].FindStringKey("name"), "symbol_name");
  EXPECT_EQ(*asset_list[0].FindStringKey("symbol"), "symbol");
  EXPECT_EQ(*asset_list[0].FindBoolKey("is_erc20"), false);
  EXPECT_EQ(*asset_list[0].FindBoolKey("is_erc721"), false);
  EXPECT_EQ(*asset_list[0].FindIntKey("decimals"), 11);
  EXPECT_EQ(*asset_list[0].FindStringKey("logo"), "https://url1.com");
  EXPECT_EQ(*asset_list[0].FindBoolKey("visible"), true);

  callback_is_called = false;
  json_rpc_service_->AddEthereumChainRequestCompleted("0x111", true);
  ASSERT_FALSE(callback_is_called);
}

TEST_F(JsonRpcServiceUnitTest, AddEthereumChainForOriginRejected) {
  mojom::NetworkInfo chain = GetTestNetworkInfo1("0x111");

  base::RunLoop loop;
  std::unique_ptr<TestJsonRpcServiceObserver> observer(
      new TestJsonRpcServiceObserver(
          loop.QuitClosure(), "0x111", mojom::CoinType::ETH,
          l10n_util::GetStringUTF8(IDS_WALLET_USER_REJECTED_REQUEST)));

  json_rpc_service_->AddObserver(observer->GetReceiver());

  mojo::PendingRemote<brave_wallet::mojom::JsonRpcServiceObserver> receiver;
  mojo::MakeSelfOwnedReceiver(std::move(observer),
                              receiver.InitWithNewPipeAndPassReceiver());

  bool callback_is_called = false;
  mojom::ProviderError expected = mojom::ProviderError::kSuccess;
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());
  SetEthChainIdInterceptor(GetActiveEndpointUrl(chain), "0x111");
  json_rpc_service_->AddEthereumChainForOrigin(
      chain.Clone(), url::Origin::Create(GURL("https://brave.com")),
      base::BindLambdaForTesting(
          [&callback_is_called, &expected](const std::string& chain_id,
                                           mojom::ProviderError error,
                                           const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, expected);
            ASSERT_TRUE(error_message.empty());
            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  json_rpc_service_->AddEthereumChainRequestCompleted("0x111", false);
  loop.Run();
  ASSERT_TRUE(callback_is_called);
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());
}

TEST_F(JsonRpcServiceUnitTest, AddChain) {
  {
    mojom::NetworkInfo chain = GetTestNetworkInfo1("0x111");
    ASSERT_FALSE(GetNetworkURL(prefs(), chain.chain_id, mojom::CoinType::ETH)
                     .is_valid());
    SetEthChainIdInterceptor(GetActiveEndpointUrl(chain), chain.chain_id);

    base::MockCallback<mojom::JsonRpcService::AddChainCallback> callback;
    EXPECT_CALL(callback, Run("0x111", mojom::ProviderError::kSuccess, ""));

    json_rpc_service_->AddChain(chain.Clone(), callback.Get());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(
        GURL("https://url1.com"),
        GetChain(prefs(), "0x111", mojom::CoinType::ETH)->rpc_endpoints[0]);
  }

  {
    mojom::NetworkInfo chain = GetTestNetworkInfo1(mojom::kFilecoinTestnet);
    chain.coin = mojom::CoinType::FIL;

    base::MockCallback<mojom::JsonRpcService::AddChainCallback> callback;
    EXPECT_CALL(callback, Run(mojom::kFilecoinTestnet,
                              mojom::ProviderError::kSuccess, ""));

    json_rpc_service_->AddChain(chain.Clone(), callback.Get());
    // No need to RunUntilIdle, callback is resolved synchronously.
    EXPECT_EQ(GURL("https://url1.com"),
              GetChain(prefs(), mojom::kFilecoinTestnet, mojom::CoinType::FIL)
                  ->rpc_endpoints[0]);
  }

  {
    // Only known networks are allowed.
    mojom::NetworkInfo chain = GetTestNetworkInfo1("0x123");
    chain.coin = mojom::CoinType::FIL;

    base::MockCallback<mojom::JsonRpcService::AddChainCallback> callback;
    EXPECT_CALL(callback,
                Run("0x123", mojom::ProviderError::kInternalError,
                    l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));

    json_rpc_service_->AddChain(chain.Clone(), callback.Get());
    // No need to RunUntilIdle, callback is resolved synchronously.
  }

  {
    mojom::NetworkInfo chain = GetTestNetworkInfo1(mojom::kSolanaMainnet);
    chain.coin = mojom::CoinType::SOL;

    base::MockCallback<mojom::JsonRpcService::AddChainCallback> callback;
    EXPECT_CALL(callback,
                Run(mojom::kSolanaMainnet, mojom::ProviderError::kSuccess, ""));

    json_rpc_service_->AddChain(chain.Clone(), callback.Get());
    // No need to RunUntilIdle, callback is resolved synchronously.
    EXPECT_EQ(GURL("https://url1.com"),
              GetChain(prefs(), mojom::kSolanaMainnet, mojom::CoinType::SOL)
                  ->rpc_endpoints[0]);
  }

  {
    // Only known networks are allowed.
    mojom::NetworkInfo chain = GetTestNetworkInfo1("0x123");
    chain.coin = mojom::CoinType::SOL;

    base::MockCallback<mojom::JsonRpcService::AddChainCallback> callback;
    EXPECT_CALL(callback,
                Run("0x123", mojom::ProviderError::kInternalError,
                    l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));

    json_rpc_service_->AddChain(chain.Clone(), callback.Get());
    // No need to RunUntilIdle, callback is resolved synchronously.
  }
}

TEST_F(JsonRpcServiceUnitTest, AddEthereumChainError) {
  mojom::NetworkInfo chain = GetTestNetworkInfo1("0x111");

  bool callback_is_called = false;
  mojom::ProviderError expected = mojom::ProviderError::kSuccess;
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), chain.chain_id, mojom::CoinType::ETH)
          .is_valid());
  SetEthChainIdInterceptor(GetActiveEndpointUrl(chain), chain.chain_id);
  json_rpc_service_->AddChain(
      chain.Clone(),
      base::BindLambdaForTesting(
          [&callback_is_called, &expected](const std::string& chain_id,
                                           mojom::ProviderError error,
                                           const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, expected);
            ASSERT_TRUE(error_message.empty());
            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(callback_is_called);

  // Add a same chain.
  bool third_callback_is_called = false;
  mojom::ProviderError third_expected =
      mojom::ProviderError::kUserRejectedRequest;
  json_rpc_service_->AddChain(
      chain.Clone(),
      base::BindLambdaForTesting([&third_callback_is_called, &third_expected](
                                     const std::string& chain_id,
                                     mojom::ProviderError error,
                                     const std::string& error_message) {
        ASSERT_FALSE(chain_id.empty());
        EXPECT_EQ(error, third_expected);
        EXPECT_EQ(error_message, l10n_util::GetStringUTF8(
                                     IDS_SETTINGS_WALLET_NETWORKS_EXISTS));
        third_callback_is_called = true;
      }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(third_callback_is_called);

  // new chain, not valid rpc url
  mojom::NetworkInfo chain4("0x444", "chain_name4", {"https://url4.com"},
                            {"https://url4.com"}, 0, {GURL("https://url4.com")},
                            "symbol_name", "symbol", 11, mojom::CoinType::ETH,
                            false);
  bool fourth_callback_is_called = false;
  mojom::ProviderError fourth_expected =
      mojom::ProviderError::kUserRejectedRequest;
  auto network_url = GetActiveEndpointUrl(chain4);
  SetEthChainIdInterceptor(network_url, "0x555");
  json_rpc_service_->AddChain(
      chain4.Clone(),
      base::BindLambdaForTesting(
          [&fourth_callback_is_called, &fourth_expected, &network_url](
              const std::string& chain_id, mojom::ProviderError error,
              const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, fourth_expected);
            EXPECT_EQ(error_message,
                      l10n_util::GetStringFUTF8(
                          IDS_BRAVE_WALLET_ETH_CHAIN_ID_FAILED,
                          base::ASCIIToUTF16(network_url.spec())));
            fourth_callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(fourth_callback_is_called);

  // new chain, broken validation response
  mojom::NetworkInfo chain5("0x444", "chain_name5", {"https://url5.com"},
                            {"https://url5.com"}, 0, {GURL("https://url5.com")},
                            "symbol_name", "symbol", 11, mojom::CoinType::ETH,
                            false);
  bool fifth_callback_is_called = false;
  mojom::ProviderError fifth_expected =
      mojom::ProviderError::kUserRejectedRequest;
  network_url = GetActiveEndpointUrl(chain5);
  SetEthChainIdInterceptorWithBrokenResponse(network_url);
  json_rpc_service_->AddChain(
      chain5.Clone(),
      base::BindLambdaForTesting(
          [&fifth_callback_is_called, &fifth_expected, &network_url](
              const std::string& chain_id, mojom::ProviderError error,
              const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, fifth_expected);
            EXPECT_EQ(error_message,
                      l10n_util::GetStringFUTF8(
                          IDS_BRAVE_WALLET_ETH_CHAIN_ID_FAILED,
                          base::ASCIIToUTF16(GURL(network_url).spec())));
            fifth_callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(fifth_callback_is_called);
}

TEST_F(JsonRpcServiceUnitTest, AddEthereumChainForOriginError) {
  mojom::NetworkInfo chain = GetTestNetworkInfo1("0x1");
  auto origin = url::Origin::Create(GURL("https://brave.com"));

  // Known eth chain should be rejected.
  ASSERT_TRUE(
      brave_wallet::GetNetworkURL(prefs(), chain.chain_id, mojom::CoinType::ETH)
          .is_valid());
  AddEthereumChainForOrigin(
      chain.Clone(), origin, chain.chain_id,
      mojom::ProviderError::kUserRejectedRequest,
      l10n_util::GetStringUTF8(IDS_SETTINGS_WALLET_NETWORKS_EXISTS));

  // Try to add a custom chain.
  chain.chain_id = "0x111";
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), chain.chain_id, mojom::CoinType::ETH)
          .is_valid());
  SetEthChainIdInterceptor(GetActiveEndpointUrl(chain), chain.chain_id);
  AddEthereumChainForOrigin(chain.Clone(), origin, chain.chain_id,
                            mojom::ProviderError::kSuccess, "");

  // Other chain with same origin that has a pending request should be rejected.
  auto chain2 = chain.Clone();
  chain2->chain_id = "0x222";
  AddEthereumChainForOrigin(
      chain2->Clone(), origin, chain2->chain_id,
      mojom::ProviderError::kUserRejectedRequest,
      l10n_util::GetStringUTF8(IDS_WALLET_ALREADY_IN_PROGRESS_ERROR));

  // Try to add same chain with other origin should get rejected.
  AddEthereumChainForOrigin(
      chain.Clone(), url::Origin::Create(GURL("https://others.com")),
      chain.chain_id, mojom::ProviderError::kUserRejectedRequest,
      l10n_util::GetStringUTF8(IDS_WALLET_ALREADY_IN_PROGRESS_ERROR));

  auto network_url = GetActiveEndpointUrl(chain);
  // New chain, not valid rpc url.
  {
    base::RunLoop loop;
    std::unique_ptr<TestJsonRpcServiceObserver> observer(
        new TestJsonRpcServiceObserver(
            loop.QuitClosure(), "0x333", mojom::CoinType::ETH,
            l10n_util::GetStringFUTF8(IDS_BRAVE_WALLET_ETH_CHAIN_ID_FAILED,
                                      base::ASCIIToUTF16(network_url.spec()))));

    json_rpc_service_->AddObserver(observer->GetReceiver());

    mojo::PendingRemote<brave_wallet::mojom::JsonRpcServiceObserver> receiver;
    mojo::MakeSelfOwnedReceiver(std::move(observer),
                                receiver.InitWithNewPipeAndPassReceiver());

    chain.chain_id = "0x333";
    AddEthereumChainForOrigin(
        chain.Clone(), url::Origin::Create(GURL("https://others2.com")),
        chain.chain_id, mojom::ProviderError::kSuccess, "");
    SetEthChainIdInterceptor(GetActiveEndpointUrl(chain), "0x555");
    json_rpc_service_->AddEthereumChainRequestCompleted(chain.chain_id, true);
    loop.Run();
  }

  // New chain, broken validation response.
  {
    base::RunLoop loop;
    std::unique_ptr<TestJsonRpcServiceObserver> observer(
        new TestJsonRpcServiceObserver(
            loop.QuitClosure(), "0x444", mojom::CoinType::ETH,
            l10n_util::GetStringFUTF8(
                IDS_BRAVE_WALLET_ETH_CHAIN_ID_FAILED,
                base::ASCIIToUTF16(GURL(network_url).spec()))));

    json_rpc_service_->AddObserver(observer->GetReceiver());

    mojo::PendingRemote<brave_wallet::mojom::JsonRpcServiceObserver> receiver;
    mojo::MakeSelfOwnedReceiver(std::move(observer),
                                receiver.InitWithNewPipeAndPassReceiver());

    chain.chain_id = "0x444";
    AddEthereumChainForOrigin(
        chain.Clone(), url::Origin::Create(GURL("https://others3.com")),
        chain.chain_id, mojom::ProviderError::kSuccess, "");
    SetEthChainIdInterceptorWithBrokenResponse(GetActiveEndpointUrl(chain));
    json_rpc_service_->AddEthereumChainRequestCompleted(chain.chain_id, true);
    loop.Run();
  }
}

TEST_F(JsonRpcServiceUnitTest, StartWithNetwork) {
  ValidateStartWithNetwork(std::string(), std::string());
  ValidateStartWithNetwork("SomeBadChainId", std::string());
  ValidateStartWithNetwork(brave_wallet::mojom::kRopstenChainId,
                           brave_wallet::mojom::kRopstenChainId);
}

TEST_F(JsonRpcServiceUnitTest, Request) {
  bool callback_called = false;
  std::string request =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_blockNumber\",\"params\":"
      "[]}";
  std::string result = "\"0xb539d5\"";
  std::string expected_response =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":" + result + "}";
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH),
                 "eth_blockNumber", "true", expected_response);
  json_rpc_service_->Request(
      request, true, base::Value(), mojom::CoinType::ETH,
      base::BindOnce(&OnRequestResponse, &callback_called, true /* success */,
                     result));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  request =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getBlockByNumber\","
      "\"params\":"
      "[\"0x5BAD55\",true]}";
  result = "\"0xb539d5\"";
  expected_response =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":" + result + "}";
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH),
                 "eth_getBlockByNumber", "0x5BAD55,true", expected_response);
  json_rpc_service_->Request(
      request, true, base::Value(), mojom::CoinType::ETH,
      base::BindOnce(&OnRequestResponse, &callback_called, true /* success */,
                     result));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->Request(
      request, true, base::Value(), mojom::CoinType::ETH,
      base::BindOnce(&OnRequestResponse, &callback_called, false /* success */,
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, Request_BadHeaderValues) {
  std::string request =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_blockNumber\n\","
      "\"params\":"
      "[]}";
  std::string mock_response =
      R"({"jsonrpc":"2.0",
          "id":1,
          "error":": {
            "code": -32601,
            "message": "unsupported method"
          }})";
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH), "",
                 "", mock_response);
  bool callback_called = false;
  json_rpc_service_->Request(
      request, true, base::Value(), mojom::CoinType::ETH,
      base::BindOnce(&OnRequestResponse, &callback_called, false, ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetBalance) {
  bool callback_called = false;
  SetInterceptor(GetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH),
                 "eth_getBalance", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0xb539d5\"}");
  json_rpc_service_->GetBalance(
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::CoinType::ETH,
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", "0xb539d5"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetBalance(
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::CoinType::ETH,
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->GetBalance(
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::CoinType::ETH,
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetBalance(
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::CoinType::ETH, "",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetBalance(
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::CoinType::ETH,
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  std::string json = R"({"jsonrpc":"2.0","id":1,"result":"100000"})";
  SetInterceptor(GetNetwork(mojom::kFilecoinMainnet, mojom::CoinType::FIL),
                 "Filecoin.WalletBalance", "", json);
  json_rpc_service_->GetBalance(
      "addr", mojom::CoinType::FIL, mojom::kFilecoinMainnet,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", "100000"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInterceptor(GetNetwork(mojom::kFilecoinTestnet, mojom::CoinType::FIL),
                 "Filecoin.WalletBalance", "", json);
  json_rpc_service_->GetBalance(
      "addr", mojom::CoinType::FIL, mojom::kFilecoinTestnet,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", "100000"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetFeeHistory) {
  std::string json =
      R"(
      {
        "jsonrpc":"2.0",
        "id":1,
        "result": {
          "baseFeePerGas": [
            "0x215d00b8c8",
            "0x24beaded75"
          ],
          "gasUsedRatio": [
            0.020687709938714324
          ],
          "oldestBlock": "0xd6b1b0",
          "reward": [
            [
              "0x77359400",
              "0x77359400",
              "0x2816a6cfb"
            ]
          ]
        }
      })";

  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH),
                 "eth_feeHistory", "", json);
  base::RunLoop run_loop;
  json_rpc_service_->GetFeeHistory(base::BindLambdaForTesting(
      [&](const std::vector<std::string>& base_fee_per_gas,
          const std::vector<double>& gas_used_ratio,
          const std::string& oldest_block,
          const std::vector<std::vector<std::string>>& reward,
          mojom::ProviderError error, const std::string& error_message) {
        EXPECT_EQ(error, mojom::ProviderError::kSuccess);
        EXPECT_TRUE(error_message.empty());
        EXPECT_EQ(base_fee_per_gas,
                  (std::vector<std::string>{"0x215d00b8c8", "0x24beaded75"}));
        EXPECT_EQ(gas_used_ratio, (std::vector<double>{0.020687709938714324}));
        EXPECT_EQ(oldest_block, "0xd6b1b0");
        EXPECT_EQ(reward, (std::vector<std::vector<std::string>>{
                              {"0x77359400", "0x77359400", "0x2816a6cfb"}}));
        run_loop.Quit();
      }));
  run_loop.Run();

  SetHTTPRequestTimeoutInterceptor();
  base::RunLoop run_loop2;
  json_rpc_service_->GetFeeHistory(base::BindLambdaForTesting(
      [&](const std::vector<std::string>& base_fee_per_gas,
          const std::vector<double>& gas_used_ratio,
          const std::string& oldest_block,
          const std::vector<std::vector<std::string>>& reward,
          mojom::ProviderError error, const std::string& error_message) {
        EXPECT_EQ(error, mojom::ProviderError::kInternalError);
        EXPECT_EQ(error_message,
                  l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
        run_loop2.Quit();
      }));
  run_loop2.Run();

  SetInvalidJsonInterceptor();
  base::RunLoop run_loop3;
  json_rpc_service_->GetFeeHistory(base::BindLambdaForTesting(
      [&](const std::vector<std::string>& base_fee_per_gas,
          const std::vector<double>& gas_used_ratio,
          const std::string& oldest_block,
          const std::vector<std::vector<std::string>>& reward,
          mojom::ProviderError error, const std::string& error_message) {
        EXPECT_EQ(error, mojom::ProviderError::kParsingError);
        EXPECT_EQ(error_message,
                  l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));
        run_loop3.Quit();
      }));
  run_loop3.Run();

  SetLimitExceededJsonErrorResponse();
  base::RunLoop run_loop4;
  json_rpc_service_->GetFeeHistory(base::BindLambdaForTesting(
      [&](const std::vector<std::string>& base_fee_per_gas,
          const std::vector<double>& gas_used_ratio,
          const std::string& oldest_block,
          const std::vector<std::vector<std::string>>& reward,
          mojom::ProviderError error, const std::string& error_message) {
        EXPECT_EQ(error, mojom::ProviderError::kLimitExceeded);
        EXPECT_EQ(error_message, "Request exceeds defined limit");
        run_loop4.Quit();
      }));
  run_loop4.Run();
}

TEST_F(JsonRpcServiceUnitTest, GetERC20TokenBalance) {
  bool callback_called = false;
  SetInterceptor(
      GetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH), "eth_call", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x00000000000000000000000000000000000000000000000166e12cfce39a0000\""
      "}");

  json_rpc_service_->GetERC20TokenBalance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "",
                     "0x166e12cfce39a0000"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetERC20TokenBalance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->GetERC20TokenBalance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetERC20TokenBalance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid input should fail.
  callback_called = false;
  json_rpc_service_->GetERC20TokenBalance(
      "", "", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC20TokenBalance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0x4e02f254184E904300e0775E4b8eeCB1", "",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetERC20TokenAllowance) {
  bool callback_called = false;
  SetInterceptor(
      GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH), "eth_call",
      "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x00000000000000000000000000000000000000000000000166e12cfce39a0000\""
      "}");

  json_rpc_service_->GetERC20TokenAllowance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460f",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460a",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "",
                     "0x166e12cfce39a0000"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetERC20TokenAllowance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460f",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460a",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->GetERC20TokenAllowance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460f",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460a",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetERC20TokenAllowance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460f",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460a",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid input should fail.
  callback_called = false;
  json_rpc_service_->GetERC20TokenAllowance(
      "", "", "",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

class UnstoppableDomainsUnitTest : public JsonRpcServiceUnitTest {
 public:
  using GetEthAddrCallback =
      mojom::JsonRpcService::UnstoppableDomainsGetEthAddrCallback;
  using ResolveDnsCallback =
      JsonRpcService::UnstoppableDomainsResolveDnsCallback;

  // Eth Mainnet: brad.crypto -> 0x8aaD44321A86b170879d7A244c1e8d360c99DdA8
  static constexpr char k0x8aaD44Addr[] =
      "0x8aaD44321A86b170879d7A244c1e8d360c99DdA8";

  // Plygon: javajobs.crypto -> 0x3a2f3f7aab82d69036763cfd3f755975f84496e6
  static constexpr char k0x3a2f3fAddr[] =
      "0x3a2f3f7aab82d69036763cfd3f755975f84496e6";

  void SetEthResponse(const std::string& response) {
    SetResponse(GetUnstoppableDomainsRpcUrl(mojom::kMainnetChainId), response);
  }
  void SetPolygonResponse(const std::string& response) {
    SetResponse(GetUnstoppableDomainsRpcUrl(mojom::kPolygonMainnetChainId),
                response);
  }

  std::string DnsIpfsResponse() const {
    return MakeJsonRpcStringArrayResponse(
        {"ipfs_hash", "", "", "", "", "https://brave.com"});
  }

  std::string DnsBraveResponse() const {
    return MakeJsonRpcStringArrayResponse(
        {"", "", "", "", "", "https://brave.com"});
  }

  std::string DnsEmptyResponse() const {
    return MakeJsonRpcStringArrayResponse({"", "", "", "", "", ""});
  }

 private:
  void SetResponse(const GURL& rpc_url, const std::string& response) {
    if (response.empty()) {
      EXPECT_TRUE(url_loader_factory_.SimulateResponseForPendingRequest(
          AddInfuraProjectId(rpc_url).spec(), "", net::HTTP_REQUEST_TIMEOUT));
      return;
    }

    EXPECT_TRUE(url_loader_factory_.SimulateResponseForPendingRequest(
        AddInfuraProjectId(rpc_url).spec(), response, net::HTTP_OK));
  }
};

TEST_F(UnstoppableDomainsUnitTest, GetEthAddr_PolygonNetworkError) {
  base::MockCallback<GetEthAddrCallback> callback;
  EXPECT_CALL(callback,
              Run("", mojom::ProviderError::kInternalError,
                  l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
  json_rpc_service_->UnstoppableDomainsGetEthAddr("brad.crypto",
                                                  callback.Get());
  SetEthResponse("");
  SetPolygonResponse("");
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback,
              Run("", mojom::ProviderError::kInternalError,
                  l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
  json_rpc_service_->UnstoppableDomainsGetEthAddr("brad.crypto",
                                                  callback.Get());
  SetEthResponse(MakeJsonRpcStringResponse(k0x8aaD44Addr));
  SetPolygonResponse("");
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback,
              Run("", mojom::ProviderError::kParsingError,
                  l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR)));
  json_rpc_service_->UnstoppableDomainsGetEthAddr("brad.crypto",
                                                  callback.Get());
  SetEthResponse(MakeJsonRpcStringResponse(k0x8aaD44Addr));
  SetPolygonResponse("Not a json");
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback,
              Run("", mojom::ProviderError::kLimitExceeded, "Error!"));
  json_rpc_service_->UnstoppableDomainsGetEthAddr("brad.crypto",
                                                  callback.Get());
  SetEthResponse(MakeJsonRpcStringResponse(k0x8aaD44Addr));
  SetPolygonResponse(MakeJsonRpcErrorResponse(-32005, "Error!"));
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);
}

TEST_F(UnstoppableDomainsUnitTest, GetEthAddr_PolygonResult) {
  base::MockCallback<GetEthAddrCallback> callback;
  EXPECT_CALL(callback, Run(k0x3a2f3fAddr, mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->UnstoppableDomainsGetEthAddr("javajobs.crypto",
                                                  callback.Get());
  SetEthResponse("");
  SetPolygonResponse(MakeJsonRpcStringResponse(k0x3a2f3fAddr));
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback, Run(k0x3a2f3fAddr, mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->UnstoppableDomainsGetEthAddr("javajobs.crypto",
                                                  callback.Get());
  SetEthResponse(MakeJsonRpcStringResponse(k0x8aaD44Addr));
  SetPolygonResponse(MakeJsonRpcStringResponse(k0x3a2f3fAddr));
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback, Run(k0x3a2f3fAddr, mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->UnstoppableDomainsGetEthAddr("javajobs.crypto",
                                                  callback.Get());
  SetEthResponse(MakeJsonRpcStringResponse(""));
  SetPolygonResponse(MakeJsonRpcStringResponse(k0x3a2f3fAddr));
  base::RunLoop().RunUntilIdle();
}

TEST_F(UnstoppableDomainsUnitTest, GetEthAddr_FallbackToEthMainnet) {
  base::MockCallback<GetEthAddrCallback> callback;
  EXPECT_CALL(callback, Run(k0x8aaD44Addr, mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->UnstoppableDomainsGetEthAddr("brad.crypto",
                                                  callback.Get());
  SetEthResponse(MakeJsonRpcStringResponse(k0x8aaD44Addr));
  SetPolygonResponse(MakeJsonRpcStringResponse(""));
  base::RunLoop().RunUntilIdle();
}

TEST_F(UnstoppableDomainsUnitTest, GetEthAddr_FallbackToEthMainnetError) {
  base::MockCallback<GetEthAddrCallback> callback;
  EXPECT_CALL(callback,
              Run("", mojom::ProviderError::kInternalError,
                  l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
  json_rpc_service_->UnstoppableDomainsGetEthAddr("brad.crypto",
                                                  callback.Get());
  SetEthResponse("");
  SetPolygonResponse(MakeJsonRpcStringResponse(""));
  base::RunLoop().RunUntilIdle();
}

TEST_F(UnstoppableDomainsUnitTest, GetEthAddr_InvalidDomain) {
  base::MockCallback<GetEthAddrCallback> callback;
  EXPECT_CALL(callback,
              Run("", mojom::ProviderError::kInvalidParams,
                  l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS)));
  json_rpc_service_->UnstoppableDomainsGetEthAddr("brad.test", callback.Get());
  EXPECT_EQ(0, url_loader_factory_.NumPending());
  base::RunLoop().RunUntilIdle();
}

TEST_F(UnstoppableDomainsUnitTest, GetEthAddr_ManyCalls) {
  base::MockCallback<GetEthAddrCallback> callback1;
  EXPECT_CALL(callback1,
              Run(k0x3a2f3fAddr, mojom::ProviderError::kSuccess, ""));
  base::MockCallback<GetEthAddrCallback> callback2;
  EXPECT_CALL(callback2,
              Run(k0x3a2f3fAddr, mojom::ProviderError::kSuccess, ""));
  base::MockCallback<GetEthAddrCallback> callback3;
  EXPECT_CALL(callback3,
              Run(k0x8aaD44Addr, mojom::ProviderError::kSuccess, ""));

  EXPECT_EQ(0, url_loader_factory_.NumPending());
  json_rpc_service_->UnstoppableDomainsGetEthAddr("javajobs.crypto",
                                                  callback1.Get());
  EXPECT_EQ(2, url_loader_factory_.NumPending());
  json_rpc_service_->UnstoppableDomainsGetEthAddr("javajobs.crypto",
                                                  callback2.Get());
  EXPECT_EQ(2, url_loader_factory_.NumPending());  // No new requests.
  json_rpc_service_->UnstoppableDomainsGetEthAddr("another.crypto",
                                                  callback3.Get());
  EXPECT_EQ(4, url_loader_factory_.NumPending());

  // This will resolve javajobs.crypto requests.
  SetEthResponse(MakeJsonRpcStringResponse(k0x8aaD44Addr));
  SetPolygonResponse(MakeJsonRpcStringResponse(k0x3a2f3fAddr));

  // This will resolve another.crypto requests.
  SetEthResponse(MakeJsonRpcStringResponse(k0x8aaD44Addr));
  SetPolygonResponse(MakeJsonRpcStringResponse(""));

  base::RunLoop().RunUntilIdle();
}

TEST_F(UnstoppableDomainsUnitTest, ResolveDns_PolygonNetworkError) {
  base::MockCallback<ResolveDnsCallback> callback;
  EXPECT_CALL(callback,
              Run(GURL(), mojom::ProviderError::kInternalError,
                  l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback.Get());
  SetEthResponse("");
  SetPolygonResponse("");
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback,
              Run(GURL(), mojom::ProviderError::kInternalError,
                  l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback.Get());
  SetEthResponse(DnsBraveResponse());
  SetPolygonResponse("");
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback,
              Run(GURL(), mojom::ProviderError::kParsingError,
                  l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR)));
  json_rpc_service_->UnstoppableDomainsResolveDns("brad.crypto",
                                                  callback.Get());
  SetEthResponse(DnsBraveResponse());
  SetPolygonResponse("Not a json");
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback,
              Run(GURL(), mojom::ProviderError::kLimitExceeded, "Error!"));
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback.Get());
  SetEthResponse(DnsBraveResponse());
  SetPolygonResponse(MakeJsonRpcErrorResponse(-32005, "Error!"));
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);
}

TEST_F(UnstoppableDomainsUnitTest, ResolveDns_PolygonResult) {
  base::MockCallback<ResolveDnsCallback> callback;
  EXPECT_CALL(callback, Run(GURL("https://brave.com"),
                            mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback.Get());
  SetEthResponse("");
  SetPolygonResponse(DnsBraveResponse());
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback, Run(GURL("https://brave.com"),
                            mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback.Get());
  SetEthResponse(DnsIpfsResponse());
  SetPolygonResponse(DnsBraveResponse());
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback, Run(GURL("https://brave.com"),
                            mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback.Get());
  SetEthResponse(DnsEmptyResponse());
  SetPolygonResponse(DnsBraveResponse());
  base::RunLoop().RunUntilIdle();
}

TEST_F(UnstoppableDomainsUnitTest, ResolveDns_FallbackToEthMainnet) {
  base::MockCallback<ResolveDnsCallback> callback;
  EXPECT_CALL(callback, Run(GURL("ipfs://ipfs_hash"),
                            mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback.Get());
  SetEthResponse(DnsIpfsResponse());
  SetPolygonResponse(DnsEmptyResponse());
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback, Run(GURL("https://brave.com"),
                            mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback.Get());
  SetEthResponse(DnsBraveResponse());
  SetPolygonResponse(
      MakeJsonRpcStringArrayResponse({"", "", "", "", "", "invalid url"}));
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);
}

TEST_F(UnstoppableDomainsUnitTest, ResolveDns_FallbackToEthMainnetError) {
  base::MockCallback<ResolveDnsCallback> callback;
  EXPECT_CALL(callback,
              Run(GURL(), mojom::ProviderError::kInternalError,
                  l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback.Get());
  SetEthResponse("");
  SetPolygonResponse(DnsEmptyResponse());
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);

  EXPECT_CALL(callback, Run(GURL(), mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback.Get());
  SetEthResponse(
      MakeJsonRpcStringArrayResponse({"", "", "", "", "", "invalid url"}));
  SetPolygonResponse(DnsEmptyResponse());
  base::RunLoop().RunUntilIdle();
  testing::Mock::VerifyAndClearExpectations(&callback);
}

TEST_F(UnstoppableDomainsUnitTest, ResolveDns_InvalidDomain) {
  base::MockCallback<ResolveDnsCallback> callback;
  EXPECT_CALL(callback,
              Run(GURL(), mojom::ProviderError::kInvalidParams,
                  l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS)));
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.test", callback.Get());
  EXPECT_EQ(0, url_loader_factory_.NumPending());
  base::RunLoop().RunUntilIdle();
}

TEST_F(UnstoppableDomainsUnitTest, ResolveDns_ManyCalls) {
  base::MockCallback<ResolveDnsCallback> callback1;
  EXPECT_CALL(callback1, Run(GURL("https://brave.com"),
                             mojom::ProviderError::kSuccess, ""));
  base::MockCallback<ResolveDnsCallback> callback2;
  EXPECT_CALL(callback2, Run(GURL("https://brave.com"),
                             mojom::ProviderError::kSuccess, ""));
  base::MockCallback<ResolveDnsCallback> callback3;
  EXPECT_CALL(callback3, Run(GURL("ipfs://ipfs_hash"),
                             mojom::ProviderError::kSuccess, ""));

  EXPECT_EQ(0, url_loader_factory_.NumPending());
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback1.Get());
  EXPECT_EQ(2, url_loader_factory_.NumPending());
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.crypto",
                                                  callback2.Get());
  EXPECT_EQ(2, url_loader_factory_.NumPending());  // No new requests.
  json_rpc_service_->UnstoppableDomainsResolveDns("brave.x", callback3.Get());
  EXPECT_EQ(4, url_loader_factory_.NumPending());

  // This will resolve brave.crypto requests.
  SetEthResponse(DnsIpfsResponse());
  SetPolygonResponse(DnsBraveResponse());

  // This will resolve brave.x requests.
  SetEthResponse(DnsBraveResponse());
  SetPolygonResponse(DnsIpfsResponse());

  base::RunLoop().RunUntilIdle();
}

TEST_F(JsonRpcServiceUnitTest, GetIsEip1559) {
  bool callback_called = false;
  GURL expected_network =
      GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH);
  // Successful path when the network is EIP1559
  SetIsEip1559Interceptor(expected_network, true);
  json_rpc_service_->GetIsEip1559(
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", true));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Successful path when the network is not EIP1559
  callback_called = false;
  SetIsEip1559Interceptor(expected_network, false);
  json_rpc_service_->GetIsEip1559(
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetIsEip1559(base::BindOnce(
      &OnBoolResponse, &callback_called, mojom::ProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->GetIsEip1559(base::BindOnce(
      &OnBoolResponse, &callback_called, mojom::ProviderError::kParsingError,
      l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetIsEip1559(base::BindOnce(
      &OnBoolResponse, &callback_called, mojom::ProviderError::kLimitExceeded,
      "Request exceeds defined limit", false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, UpdateIsEip1559NotCalledForKnownChains) {
  TestJsonRpcServiceObserver observer(mojom::kMainnetChainId,
                                      mojom::CoinType::ETH, false);
  json_rpc_service_->AddObserver(observer.GetReceiver());
  EXPECT_TRUE(
      SetNetwork(brave_wallet::mojom::kMainnetChainId, mojom::CoinType::ETH));
  EXPECT_FALSE(observer.is_eip1559_changed_called());
}

TEST_F(JsonRpcServiceUnitTest, UpdateIsEip1559LocalhostChain) {
  TestJsonRpcServiceObserver observer(mojom::kLocalhostChainId,
                                      mojom::CoinType::ETH, true);
  json_rpc_service_->AddObserver(observer.GetReceiver());
  GURL expected_network =
      GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH);
  // Switching to localhost should update is_eip1559 to true when is_eip1559 is
  // true in the RPC response.
  EXPECT_FALSE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));
  SetIsEip1559Interceptor(expected_network, true);
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_TRUE(observer.is_eip1559_changed_called());
  EXPECT_TRUE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));

  // Switching to localhost should update is_eip1559 to false when is_eip1559
  // is false in the RPC response.
  observer.Reset(mojom::kLocalhostChainId, false);
  SetIsEip1559Interceptor(expected_network, false);
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_TRUE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));

  // Switch to localhost again without changing is_eip1559 should not trigger
  // event.
  observer.Reset(mojom::kLocalhostChainId, false);
  EXPECT_FALSE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));
  SetIsEip1559Interceptor(expected_network, false);
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_FALSE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));

  // OnEip1559Changed will not be called if RPC fails.
  observer.Reset(mojom::kLocalhostChainId, false);
  SetHTTPRequestTimeoutInterceptor();
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_FALSE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));
}

TEST_F(JsonRpcServiceUnitTest, UpdateIsEip1559CustomChain) {
  std::vector<base::Value::Dict> values;
  mojom::NetworkInfo chain1 = GetTestNetworkInfo1();
  values.push_back(brave_wallet::NetworkInfoToValue(chain1));

  mojom::NetworkInfo chain2 = GetTestNetworkInfo2();
  values.push_back(brave_wallet::NetworkInfoToValue(chain2));
  UpdateCustomNetworks(prefs(), &values);

  // Switch to chain1 should trigger is_eip1559 being updated to true when
  // is_eip1559 is true in the RPC response.
  TestJsonRpcServiceObserver observer(chain1.chain_id, mojom::CoinType::ETH,
                                      true);
  json_rpc_service_->AddObserver(observer.GetReceiver());

  EXPECT_FALSE(GetIsEip1559FromPrefs(chain1.chain_id));
  SetIsEip1559Interceptor(GetActiveEndpointUrl(chain1), true);
  EXPECT_TRUE(SetNetwork(chain1.chain_id, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_TRUE(observer.is_eip1559_changed_called());
  EXPECT_TRUE(GetIsEip1559FromPrefs(chain1.chain_id));

  // Switch to chain2 should trigger is_eip1559 being updated to false when
  // is_eip1559 is false in the RPC response.
  observer.Reset(chain2.chain_id, false);
  EXPECT_TRUE(GetIsEip1559FromPrefs(chain2.chain_id));
  SetIsEip1559Interceptor(GetActiveEndpointUrl(chain2), false);
  EXPECT_TRUE(SetNetwork(chain2.chain_id, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_TRUE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(chain2.chain_id));

  // Switch to chain2 again without changing is_eip1559 should not trigger
  // event.
  observer.Reset(chain2.chain_id, false);
  EXPECT_FALSE(GetIsEip1559FromPrefs(chain2.chain_id));
  SetIsEip1559Interceptor(GetActiveEndpointUrl(chain2), false);
  EXPECT_TRUE(SetNetwork(chain2.chain_id, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_FALSE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(chain2.chain_id));

  // OnEip1559Changed will not be called if RPC fails.
  observer.Reset(chain2.chain_id, false);
  SetHTTPRequestTimeoutInterceptor();
  EXPECT_TRUE(SetNetwork(chain2.chain_id, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_FALSE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(chain2.chain_id));
}

TEST_F(JsonRpcServiceUnitTest, GetEthAddrInvalidDomain) {
  const std::vector<std::string> invalid_domains = {"", ".eth", "-brave.eth",
                                                    "brave-.eth", "b.eth"};

  for (const auto& domain : invalid_domains) {
    {
      base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
      EXPECT_CALL(callback,
                  Run("", false, mojom::ProviderError::kInvalidParams,
                      l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS)));

      json_rpc_service_->EnsGetEthAddr(domain, nullptr, callback.Get());
      base::RunLoop().RunUntilIdle();
    }

    {
      base::MockCallback<JsonRpcService::UnstoppableDomainsGetEthAddrCallback>
          callback;
      EXPECT_CALL(callback,
                  Run("", mojom::ProviderError::kInvalidParams,
                      l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS)));

      json_rpc_service_->UnstoppableDomainsGetEthAddr(domain, callback.Get());
      base::RunLoop().RunUntilIdle();
    }
  }
}

TEST_F(JsonRpcServiceUnitTest, IsValidDomain) {
  std::vector<std::string> valid_domains = {"brave.eth", "test.brave.eth",
                                            "brave-test.test-dev.eth"};
  for (const auto& domain : valid_domains)
    EXPECT_TRUE(JsonRpcService::IsValidDomain(domain))
        << domain << " should be valid";

  std::vector<std::string> invalid_domains = {
      "",      ".eth",    "-brave.eth",      "brave-.eth",     "brave.e-th",
      "b.eth", "brave.e", "-brave.test.eth", "brave-.test.eth"};
  for (const auto& domain : invalid_domains)
    EXPECT_FALSE(JsonRpcService::IsValidDomain(domain))
        << domain << " should be invalid";
}

TEST_F(JsonRpcServiceUnitTest, IsValidUnstoppableDomain) {
  // clang-format off
  std::vector<std::string> valid_domains = {
      "test.crypto",
      "test.x",
      "test.coin",
      "test.nft",
      "test.dao",
      "test.wallet",
      "test.blockchain",
      "test.bitcoin",
      "brave.zil",
      "a.crypto",
      "1.crypto",
      "-.crypto",
  };
  std::vector<std::string> invalid_domains = {
      "",
      ".",
      "crypto.",
      "crypto.1",
      ".crypto",
      "crypto.brave",
      "brave.crypto-",
      "brave.test.crypto",
      "test.888",
  };
  // clang-format on
  for (const auto& domain : valid_domains)
    EXPECT_TRUE(JsonRpcService::IsValidUnstoppableDomain(domain))
        << domain << " should be valid";

  for (const auto& domain : invalid_domains)
    EXPECT_FALSE(JsonRpcService::IsValidUnstoppableDomain(domain))
        << domain << " should be invalid";
}

TEST_F(JsonRpcServiceUnitTest, GetERC721OwnerOf) {
  bool callback_called = false;

  json_rpc_service_->GetERC721OwnerOf(
      "", "0x1", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1", "",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetInterceptor(
      GetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH), "eth_call", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x000000000000000000000000983110309620d911731ac0932219af0609"
      "1b6744\"}");

  callback_called = false;
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      mojom::kMainnetChainId,
      base::BindOnce(
          &OnStringResponse, &callback_called, mojom::ProviderError::kSuccess,
          "",
          "0x983110309620D911731Ac0932219af06091b6744"));  // checksum address
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetInvalidJsonInterceptor();
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetTokenMetadata) {
  const std::string https_token_uri_response = R"({
      "jsonrpc":"2.0",
      "id":1,
      "result":"0x0000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000002468747470733a2f2f696e76697369626c65667269656e64732e696f2f6170692f3138313700000000000000000000000000000000000000000000000000000000"
  })";
  const std::string http_token_uri_response = R"({
      "jsonrpc":"2.0",
      "id":1,
      "result":"0x00000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000020687474703a2f2f696e76697369626c65667269656e64732e696f2f6170692f31"
  })";
  const std::string data_token_uri_response = R"({
      "jsonrpc":"2.0",
      "id":1,
      "result": "0x00000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000135646174613a6170706c69636174696f6e2f6a736f6e3b6261736536342c65794a686448527961574a316447567a496a6f69496977695a47567a59334a7063485270623234694f694a4f623234675a6e56755a326c696247556762476c7662694973496d6c745957646c496a6f695a474630595470706257466e5a53397a646d6372654731734f324a68633255324e43785153453479576e6c434e474a586548566a656a4270595568534d474e4562335a4d4d32517a5a486b314d3031354e585a6a62574e3254577042643031444f58706b62574e7053556861634670595a454e694d326335535770425a3031445154464e5245466e546c524264306c714e44686a5230597759554e436131425453576c4d656a513454444e4f4d6c70364e4430694c434a755957316c496a6f69546b5a4d496e303d0000000000000000000000"
  })";
  const std::string data_token_uri_response_invalid_json = R"({
    "jsonrpc":"2.0",
    "id":1,
    "result":"0x00000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000085646174613a6170706c69636174696f6e2f6a736f6e3b6261736536342c65794a755957316c496a6f69546b5a4d49697767496d526c63324e796158423061573975496a6f69546d397549475a31626d6470596d786c49477870623234694c43416959585230636d6c696458526c637949364969497349434a706257466e5a5349364969493d000000000000000000000000000000000000000000000000000000"
  })";
  const std::string data_token_uri_response_empty_string = R"({
    "jsonrpc":"2.0",
    "id":1,
    "result":"0x0000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000001d646174613a6170706c69636174696f6e2f6a736f6e3b6261736536342c000000"
  })";
  const std::string interface_supported_response = R"({
      "jsonrpc":"2.0",
      "id":1,
      "result": "0x0000000000000000000000000000000000000000000000000000000000000001"
  })";
  const std::string exceeds_limit_json = R"({
    "jsonrpc":"2.0",
    "id":1,
    "error": {
      "code":-32005,
      "message": "Request exceeds defined limit"
    }
  })";
  const std::string interface_not_supported_response = R"({
      "jsonrpc":"2.0",
      "id":1,
      "result":"0x0000000000000000000000000000000000000000000000000000000000000000"
  })";
  const std::string invalid_json =
      "It might make sense just to get some in case it catches on";
  const std::string https_metadata_response =
      R"({"attributes":[{"trait_type":"Feet","value":"Green Shoes"},{"trait_type":"Legs","value":"Tan Pants"},{"trait_type":"Suspenders","value":"White Suspenders"},{"trait_type":"Upper Body","value":"Indigo Turtleneck"},{"trait_type":"Sleeves","value":"Long Sleeves"},{"trait_type":"Hat","value":"Yellow / Blue Pointy Beanie"},{"trait_type":"Eyes","value":"White Nerd Glasses"},{"trait_type":"Mouth","value":"Toothpick"},{"trait_type":"Ears","value":"Bing Bong Stick"},{"trait_type":"Right Arm","value":"Swinging"},{"trait_type":"Left Arm","value":"Diamond Hand"},{"trait_type":"Background","value":"Blue"}],"description":"5,000 animated Invisible Friends hiding in the metaverse. A collection by Markus Magnusson & Random Character Collective.","image":"https://rcc.mypinata.cloud/ipfs/QmXmuSenZRnofhGMz2NyT3Yc4Zrty1TypuiBKDcaBsNw9V/1817.gif","name":"Invisible Friends #1817"})";
  const std::string ipfs_token_uri_response = R"({
      "jsonrpc":"2.0",
      "id":1,
      "result":"0x0000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000003a697066733a2f2f516d65536a53696e4870506e6d586d73704d6a776958794e367a533445397a63636172694752336a7863615774712f31383137000000000000"
  })";
  const std::string ipfs_metadata_response =
      R"({"attributes":[{"trait_type":"Mouth","value":"Bored Cigarette"},{"trait_type":"Fur","value":"Gray"},{"trait_type":"Background","value":"Aquamarine"},{"trait_type":"Clothes","value":"Tuxedo Tee"},{"trait_type":"Hat","value":"Bayc Hat Black"},{"trait_type":"Eyes","value":"Coins"}],"image":"ipfs://QmQ82uDT3JyUMsoZuaFBYuEucF654CYE5ktPUrnA5d4VDH"})";

  // Invalid inputs
  // (1/3) Invalid contract address
  TestGetTokenMetadata("", "0x1", mojom::kMainnetChainId,
                       kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kInvalidParams,
                       l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));

  // (2/3) Invalid token ID
  TestGetTokenMetadata("0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kInvalidParams,
                       l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));

  // (3/3) Invalid chain ID
  TestGetTokenMetadata("0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1", "",
                       kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kInvalidParams,
                       l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));

  // Mismatched
  // (4/4) Unknown interfaceID
  TestGetTokenMetadata("0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1", "",
                       kERC721InterfaceId, "",
                       mojom::ProviderError::kInvalidParams,
                       l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));

  // Valid inputs
  // (1/3) HTTP URI
  SetTokenMetadataInterceptor(
      kERC721MetadataInterfaceId, mojom::kMainnetChainId,
      interface_supported_response, https_token_uri_response,
      https_metadata_response);
  TestGetTokenMetadata("0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId,
                       https_metadata_response, mojom::ProviderError::kSuccess,
                       "");

  // (2/3) IPFS URI
  SetTokenMetadataInterceptor(kERC721MetadataInterfaceId,
                              mojom::kLocalhostChainId,
                              interface_supported_response,
                              ipfs_token_uri_response, ipfs_metadata_response);
  TestGetTokenMetadata("0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
                       mojom::kLocalhostChainId, kERC721MetadataInterfaceId,
                       ipfs_metadata_response, mojom::ProviderError::kSuccess,
                       "");

  // (3/3) Data URI
  SetTokenMetadataInterceptor(
      kERC721MetadataInterfaceId, mojom::kMainnetChainId,
      interface_supported_response, data_token_uri_response);
  TestGetTokenMetadata(
      "0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
      mojom::kMainnetChainId, kERC721MetadataInterfaceId,
      R"({"attributes":"","description":"Non fungible lion","image":"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCA1MDAgNTAwIj48cGF0aCBkPSIiLz48L3N2Zz4=","name":"NFL"})",
      mojom::ProviderError::kSuccess, "");

  // Invalid supportsInterface response
  // (1/4) Timeout
  SetTokenMetadataInterceptor(
      kERC721MetadataInterfaceId, mojom::kMainnetChainId,
      interface_supported_response, https_token_uri_response, "",
      net::HTTP_REQUEST_TIMEOUT);
  TestGetTokenMetadata("0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kInternalError,
                       l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));

  // (2/4) Invalid JSON
  SetTokenMetadataInterceptor(kERC721MetadataInterfaceId,
                              mojom::kMainnetChainId, invalid_json);
  TestGetTokenMetadata("0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kParsingError,
                       l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // (3/4) Request exceeds provider limit
  SetTokenMetadataInterceptor(kERC721MetadataInterfaceId,
                              mojom::kMainnetChainId, exceeds_limit_json);
  TestGetTokenMetadata("0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kLimitExceeded,
                       "Request exceeds defined limit");

  // (4/4) Interface not supported
  SetTokenMetadataInterceptor(kERC721MetadataInterfaceId,
                              mojom::kMainnetChainId,
                              interface_not_supported_response);
  TestGetTokenMetadata(
      "0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
      mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
      mojom::ProviderError::kMethodNotSupported,
      l10n_util::GetStringUTF8(IDS_WALLET_METHOD_NOT_SUPPORTED_ERROR));

  // Invalid tokenURI response (6 total)
  // (1/6) Timeout
  SetTokenMetadataInterceptor(
      kERC721MetadataInterfaceId, mojom::kMainnetChainId,
      interface_supported_response, https_token_uri_response, "", net::HTTP_OK,
      net::HTTP_REQUEST_TIMEOUT);
  TestGetTokenMetadata("0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kInternalError,
                       l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));

  // (2/6) Invalid Provider JSON
  SetTokenMetadataInterceptor(kERC721MetadataInterfaceId,
                              mojom::kMainnetChainId,
                              interface_supported_response, invalid_json);
  TestGetTokenMetadata("0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kParsingError,
                       l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // (3/6) Invalid JSON in data URI
  SetTokenMetadataInterceptor(
      kERC721MetadataInterfaceId, mojom::kMainnetChainId,
      interface_supported_response, data_token_uri_response_invalid_json);
  TestGetTokenMetadata("0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kParsingError,
                       l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // (4/6) Empty string as JSON in data URI
  SetTokenMetadataInterceptor(
      kERC721MetadataInterfaceId, mojom::kMainnetChainId,
      interface_supported_response, data_token_uri_response_empty_string);
  TestGetTokenMetadata("0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kParsingError,
                       l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // (5/6) Request exceeds limit
  SetTokenMetadataInterceptor(kERC721MetadataInterfaceId,
                              mojom::kMainnetChainId,
                              interface_supported_response, exceeds_limit_json);
  TestGetTokenMetadata("0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kLimitExceeded,
                       "Request exceeds defined limit");

  // (6/6) URI scheme is not suported (HTTP)
  SetTokenMetadataInterceptor(
      kERC721MetadataInterfaceId, mojom::kMainnetChainId,
      interface_supported_response, http_token_uri_response);
  TestGetTokenMetadata(
      "0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
      mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
      mojom::ProviderError::kMethodNotSupported,
      l10n_util::GetStringUTF8(IDS_WALLET_METHOD_NOT_SUPPORTED_ERROR));

  // Invalid metadata response (2 total)
  // (1/2) Timeout
  SetTokenMetadataInterceptor(
      kERC721MetadataInterfaceId, mojom::kMainnetChainId,
      interface_supported_response, https_token_uri_response,
      https_metadata_response, net::HTTP_OK, net::HTTP_OK,
      net::HTTP_REQUEST_TIMEOUT);
  TestGetTokenMetadata("0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kInternalError,
                       l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));

  // (2/2) Invalid JSON
  SetTokenMetadataInterceptor(
      kERC721MetadataInterfaceId, mojom::kMainnetChainId,
      interface_supported_response, ipfs_token_uri_response, invalid_json);
  TestGetTokenMetadata("0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
                       mojom::kMainnetChainId, kERC721MetadataInterfaceId, "",
                       mojom::ProviderError::kParsingError,
                       l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // ERC1155
  SetTokenMetadataInterceptor(
      kERC1155MetadataInterfaceId, mojom::kMainnetChainId,
      interface_supported_response, https_token_uri_response,
      https_metadata_response);
  TestGetTokenMetadata("0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
                       mojom::kMainnetChainId, kERC1155MetadataInterfaceId,
                       https_metadata_response, mojom::ProviderError::kSuccess,
                       "");
}

TEST_F(JsonRpcServiceUnitTest, GetERC721Metadata) {
  // Ensure GetERC721Metadata passes the correct interface ID to
  // GetTokenMetadata
  SetTokenMetadataInterceptor(kERC721MetadataInterfaceId,
                              mojom::kMainnetChainId,
                              R"({
                                  "jsonrpc":"2.0",
                                  "id":1,
                                  "result": "0x0000000000000000000000000000000000000000000000000000000000000001"
                              })",
                              R"({
                                  "jsonrpc":"2.0",
                                  "id":1,
                                  "result":"0x0000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000002468747470733a2f2f696e76697369626c65667269656e64732e696f2f6170692f3138313700000000000000000000000000000000000000000000000000000000"
                              })",
                              https_metadata_response);
  TestGetERC721Metadata("0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
                        mojom::kMainnetChainId, https_metadata_response,
                        mojom::ProviderError::kSuccess, "");
}

TEST_F(JsonRpcServiceUnitTest, GetERC1155Metadata) {
  // Ensure GetERC1155Metadata passes the correct interface ID to
  // GetTokenMetadata
  SetTokenMetadataInterceptor(kERC1155MetadataInterfaceId,
                              mojom::kMainnetChainId,
                              R"({
                                  "jsonrpc":"2.0",
                                  "id":1,
                                  "result": "0x0000000000000000000000000000000000000000000000000000000000000001"
                              })",
                              R"({
                                  "jsonrpc":"2.0",
                                  "id":1,
                                  "result":"0x0000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000002468747470733a2f2f696e76697369626c65667269656e64732e696f2f6170692f3138313700000000000000000000000000000000000000000000000000000000"
                              })",
                              https_metadata_response);
  TestGetERC1155Metadata("0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
                         mojom::kMainnetChainId, https_metadata_response,
                         mojom::ProviderError::kSuccess, "");
}

TEST_F(JsonRpcServiceUnitTest, GetERC721Balance) {
  bool callback_called = false;

  // Invalid inputs.
  json_rpc_service_->GetERC721TokenBalance(
      "", "0x1", "0x983110309620D911731Ac0932219af06091b6744",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "",
      "0x983110309620D911731Ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1", "",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620D911731Ac0932219af06091b6744", "",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetInterceptor(
      GetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH), "eth_call", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x000000000000000000000000983110309620d911731ac0932219af0609"
      "1b6744\"}");

  // Owner gets balance 0x1.
  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620D911731Ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", "0x1"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Non-checksum address can get the same balance.
  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620d911731ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", "0x1"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Non-owner gets balance 0x0.
  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620d911731ac0932219af06091b7811", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", "0x0"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620d911731ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetInvalidJsonInterceptor();
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620d911731ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620d911731ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetERC1155TokenBalance) {
  TestGetERC1155TokenBalance(
      "", "0x0", "0x16e4476c8fddc552e3b1c4b8b56261d85977fe52",
      mojom::kMainnetChainId, "", mojom::ProviderError::kInvalidParams,
      l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));

  TestGetERC1155TokenBalance(
      "0x28472a58a490c5e09a238847f66a68a47cc76f0f", "0x0", "",
      mojom::kMainnetChainId, "", mojom::ProviderError::kInvalidParams,
      l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));

  TestGetERC1155TokenBalance(
      "0x28472a58a490c5e09a238847f66a68a47cc76f0f",
      "0x16e4476c8fddc552e3b1c4b8b56261d85977fe52", "", mojom::kMainnetChainId,
      "", mojom::ProviderError::kInvalidParams,
      l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));

  TestGetERC1155TokenBalance(
      "0x28472a58a490c5e09a238847f66a68a47cc76f0f", "0x0",
      "0x16e4476c8fddc552e3b1c4b8b56261d85977fe52", "", "",
      mojom::ProviderError::kInvalidParams,
      l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));

  SetHTTPRequestTimeoutInterceptor();
  TestGetERC1155TokenBalance(
      "0x28472a58a490c5e09a238847f66a68a47cc76f0f", "0x0",
      "0x16e4476c8fddc552e3b1c4b8b56261d85977fe52", mojom::kMainnetChainId, "",
      mojom::ProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));

  SetInvalidJsonInterceptor();
  TestGetERC1155TokenBalance(
      "0x28472a58a490c5e09a238847f66a68a47cc76f0f", "0x0",
      "0x16e4476c8fddc552e3b1c4b8b56261d85977fe52", mojom::kMainnetChainId, "",
      mojom::ProviderError::kParsingError,
      l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  SetLimitExceededJsonErrorResponse();
  TestGetERC1155TokenBalance(
      "0x28472a58a490c5e09a238847f66a68a47cc76f0f", "0x0",
      "0x16e4476c8fddc552e3b1c4b8b56261d85977fe52", mojom::kMainnetChainId, "",
      mojom::ProviderError::kLimitExceeded, "Request exceeds defined limit");
  SetInterceptor(
      GetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH), "eth_call", "",
      R"({"jsonrpc":"2.0","id":1,"result":"0x0000000000000000000000000000000000000000000000000000000000000001"})");

  TestGetERC1155TokenBalance(
      "0x28472a58a490c5e09a238847f66a68a47cc76f0f", "0xf",
      "0x16e4476c8fddc552e3b1c4b8b56261d85977fe52", mojom::kMainnetChainId,
      "0x0000000000000000000000000000000000000000000000000000000000000001",
      mojom::ProviderError::kSuccess, "");
}

TEST_F(JsonRpcServiceUnitTest, GetSupportsInterface) {
  // Successful, and does support the interface
  bool callback_called = false;
  SetInterceptor(GetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH),
                 "eth_call", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                 "\"0x000000000000000000000000000000000000000000000000000000000"
                 "0000001\"}");
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      mojom::kMainnetChainId,
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", true));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Successful, but does not support the interface
  callback_called = false;
  SetInterceptor(GetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH),
                 "eth_call", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                 "\"0x000000000000000000000000000000000000000000000000000000000"
                 "0000000\"}");
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      mojom::kMainnetChainId,
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid result, should be in hex form
  callback_called = false;
  SetInterceptor(GetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH),
                 "eth_call", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0\"}");
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      mojom::kMainnetChainId,
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR),
                     false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      mojom::kMainnetChainId,
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR),
                     false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      mojom::kMainnetChainId,
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR),
                     false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      mojom::kMainnetChainId,
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, DiscoverAssets) {
  auto* blockchain_registry = BlockchainRegistry::GetInstance();
  TokenListMap token_list_map;

  std::string get_logs_response;
  std::string response;

  // Unsupported chainId is not supported
  TestDiscoverAssets(
      mojom::kPolygonMainnetChainId,
      {"0xB4B2802129071b2B9eBb8cBB01EA1E4D14B34961"}, {}, {},
      mojom::ProviderError::kMethodNotSupported,
      l10n_util::GetStringUTF8(IDS_WALLET_METHOD_NOT_SUPPORTED_ERROR));

  // Empty address is invalid
  TestDiscoverAssets(mojom::kMainnetChainId, {}, {}, {},
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));

  // Invalid address is invalid
  TestDiscoverAssets(mojom::kMainnetChainId, {"0xinvalid"}, {}, {},
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));

  // Invalid json response triggers parsing error
  auto expected_network =
      GetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH);
  ASSERT_TRUE(ParseTokenList(R"( {
     "0x0d8775f648430679a709e98d2b0cb6250d2887ef": {
       "name": "Basic Attention Token",
       "logo": "bat.svg",
       "erc20": true,
       "symbol": "BAT",
       "decimals": 18
     }
    })",
                             &token_list_map, mojom::CoinType::ETH));
  blockchain_registry->UpdateTokenList(std::move(token_list_map));
  SetInterceptor(expected_network, "eth_getLogs", "",
                 "invalid eth_getLogs response");
  TestDiscoverAssets(mojom::kMainnetChainId,
                     {"0xB4B2802129071b2B9eBb8cBB01EA1E4D14B34961"}, {}, {},
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // Limit exceeded response triggers parsing error
  SetLimitExceededJsonErrorResponse();
  TestDiscoverAssets(mojom::kMainnetChainId,
                     {"0xB4B2802129071b2B9eBb8cBB01EA1E4D14B34961"}, {}, {},
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // Invalid logs (missing addresses) triggers parsing error
  get_logs_response = R"(
  {"jsonrpc": "2.0",
   "id": 1,
   "result": [
     {
       "blockHash": "0xaefb023131aa58e533c09c0eae29c280460d3976f5235a1ff53159ef37f73073",
       "blockNumber": "0xa72603",
       "data": "0x000000000000000000000000000000000000000000000006e83695ab1f893c00",
       "logIndex": "0x14",
       "removed": false,
       "topics": [
         "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef",
         "0x000000000000000000000000897bb1e945f5aa7ed7f81646e7991eaba63aa4b0",
         "0x000000000000000000000000b4b2802129071b2b9ebb8cbb01ea1e4d14b34961"
       ],
       "transactionHash": "0x5c655301d386f45af116a4aef418491ee27b71ac30be70a593ccffa3754797d4",
       "transactionIndex": "0xa"
     },
   ]
  })";
  SetInterceptor(expected_network, "eth_getLogs", "", response);
  TestDiscoverAssets(mojom::kMainnetChainId,
                     {"0xB4B2802129071b2B9eBb8cBB01EA1E4D14B34961"}, {}, {},
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // All valid
  ASSERT_TRUE(ParseTokenList(R"(
     {
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef": {
        "name": "Basic Attention Token",
        "logo": "bat.svg",
        "erc20": true,
        "symbol": "BAT",
        "decimals": 18
      },
      "0x6b175474e89094c44da98b954eedeac495271d0f": {
        "name": "Dai Stablecoin",
        "logo": "dai.svg",
        "erc20": true,
        "symbol": "DAI",
        "decimals": 18,
        "chainId": "0x1"
      }
     })",
                             &token_list_map, mojom::CoinType::ETH));
  blockchain_registry->UpdateTokenList(std::move(token_list_map));

  response = R"(
   {"jsonrpc": "2.0",
    "id": 1,
    "result": [
      {
        "address": "0x6b175474e89094c44da98b954eedeac495271d0f",
        "blockHash": "0x2961ceb6c16bab72a55f79e394a35f2bf1c62b30446e3537280f7c22c3115e6e",
        "blockNumber": "0xd6464c",
        "data": "0x00000000000000000000000000000000000000000000000555aff1f0fae8c000",
        "logIndex": "0x159",
        "removed": false,
        "topics": [
          "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef",
          "0x000000000000000000000000503828976d22510aad0201ac7ec88293211d23da",
          "0x000000000000000000000000b4b2802129071b2b9ebb8cbb01ea1e4d14b34961"
        ],
        "transactionHash": "0x2e652b70966c6a05f4b3e68f20d6540b7a5ab712385464a7ccf62774d39b7066",
        "transactionIndex": "0x9f"
      },
    ]
   })";
  SetInterceptor(expected_network, "eth_getLogs", "", response);
  std::vector<mojom::BlockchainTokenPtr> user_assets;
  mojom::BlockchainTokenPtr user_asset = mojom::BlockchainToken::New(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef", "Basic Attention Token",
      "bat.png", true, false, "BAT", 18, true, "", "", "0x1",
      mojom::CoinType::ETH);
  user_assets.push_back(std::move(user_asset));
  TestDiscoverAssets(
      mojom::kMainnetChainId, {"0xB4B2802129071b2B9eBb8cBB01EA1E4D14B34961"},
      {"0x6b175474e89094c44da98b954eedeac495271d0f"}, std::move(user_assets),
      mojom::ProviderError::kSuccess, "");
}

TEST_F(JsonRpcServiceUnitTest, Reset) {
  std::vector<base::Value::Dict> values;
  mojom::NetworkInfo chain = GetTestNetworkInfo1("0x1");
  values.push_back(brave_wallet::NetworkInfoToValue(chain));
  UpdateCustomNetworks(prefs(), &values);

  ASSERT_FALSE(GetAllEthCustomChains(prefs()).empty());
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH));
  prefs()->SetBoolean(kSupportEip1559OnLocalhostChain, true);
  EXPECT_TRUE(prefs()->HasPrefPath(kBraveWalletCustomNetworks));
  EXPECT_EQ(GetCurrentChainId(prefs(), mojom::CoinType::ETH),
            mojom::kLocalhostChainId);
  // This isn't valid data for these maps but we are just checking to make sure
  // it gets cleared
  json_rpc_service_->add_chain_pending_requests_["1"] =
      mojom::AddChainRequest::New();
  json_rpc_service_->switch_chain_requests_[url::Origin()] = "";
  json_rpc_service_->switch_chain_callbacks_[url::Origin()] =
      base::BindLambdaForTesting(
          [&](base::Value id, base::Value formed_response, const bool reject,
              const std::string& first_allowed_account,
              const bool update_bind_js_properties) {});

  json_rpc_service_->Reset();

  ASSERT_TRUE(GetAllEthCustomChains(prefs()).empty());
  EXPECT_FALSE(prefs()->HasPrefPath(kBraveWalletCustomNetworks));
  EXPECT_EQ(GetCurrentChainId(prefs(), mojom::CoinType::ETH),
            mojom::kMainnetChainId);
  EXPECT_FALSE(prefs()->HasPrefPath(kSupportEip1559OnLocalhostChain));
  EXPECT_TRUE(json_rpc_service_->add_chain_pending_requests_.empty());
  EXPECT_TRUE(json_rpc_service_->switch_chain_requests_.empty());
  EXPECT_TRUE(json_rpc_service_->switch_chain_callbacks_.empty());
}

TEST_F(JsonRpcServiceUnitTest, GetSolanaBalance) {
  auto expected_network =
      GetNetwork(mojom::kSolanaMainnet, mojom::CoinType::SOL);
  SetInterceptor(expected_network, "getBalance", "",
                 R"({"jsonrpc":"2.0","id":1,"result":{
                      "context":{"slot":106921266},"value":18446744073709551615}})");
  TestGetSolanaBalance(UINT64_MAX, mojom::SolanaProviderError::kSuccess, "");

  // Response parsing error
  SetInterceptor(expected_network, "getBalance", "",
                 R"({"jsonrpc":"2.0","id":1,"result":"0"})");
  TestGetSolanaBalance(0u, mojom::SolanaProviderError::kParsingError,
                       l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor(expected_network, "getBalance", "",
                 R"({"jsonrpc":"2.0","id":1,"error":{
                      "code":-32601, "message": "method does not exist"}})");
  TestGetSolanaBalance(0u, mojom::SolanaProviderError::kMethodNotFound,
                       "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSolanaBalance(0u, mojom::SolanaProviderError::kInternalError,
                       l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, GetSPLTokenAccountBalance) {
  auto expected_network =
      GetNetwork(mojom::kSolanaMainnet, mojom::CoinType::SOL);
  SetInterceptor(
      expected_network, "getTokenAccountBalance", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "{\"context\":{\"slot\":1069},\"value\":{\"amount\":\"9864\","
      "\"decimals\":2,\"uiAmount\":98.64,\"uiAmountString\":\"98.64\"}}}");
  TestGetSPLTokenAccountBalance("9864", 2u, "98.64",
                                mojom::SolanaProviderError::kSuccess, "");

  // Treat non-existed account as 0 balance.
  SetInterceptor(expected_network, "getTokenAccountBalance", "",
                 R"({"jsonrpc":"2.0","id":1,"error":
                    {"code":-32602, "message": "Invalid param: could not find account"}})");
  TestGetSPLTokenAccountBalance("0", 0u, "0",
                                mojom::SolanaProviderError::kSuccess, "");

  // Response parsing error
  SetInterceptor(expected_network, "getTokenAccountBalance", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0\"}");
  TestGetSPLTokenAccountBalance(
      "", 0u, "", mojom::SolanaProviderError::kParsingError,
      l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor(expected_network, "getTokenAccountBalance", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":"
                 "{\"code\":-32601, \"message\": \"method does not exist\"}}");
  TestGetSPLTokenAccountBalance("", 0u, "",
                                mojom::SolanaProviderError::kMethodNotFound,
                                "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSPLTokenAccountBalance(
      "", 0u, "", mojom::SolanaProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, SendSolanaTransaction) {
  TestSendSolanaTransaction(
      "", mojom::SolanaProviderError::kInvalidParams,
      l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
      "" /* signed_tx */);

  auto expected_network_url =
      GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::SOL);
  SetInterceptor(
      expected_network_url, "sendTransaction", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"2id3YC2jK9G5Wo2phDx4gJVAew8DcY5NAojnVuao8rkxwPYPe8cSwE5GzhEgJA2y8fVjDE"
      "o6iR6ykBvDxrTQrtpb\"}");

  TestSendSolanaTransaction(
      "2id3YC2jK9G5Wo2phDx4gJVAew8DcY5NAojnVuao8rkxwPYPe8cSwE5GzhEgJA2y8fVjDEo6"
      "iR6ykBvDxrTQrtpb",
      mojom::SolanaProviderError::kSuccess, "");

  // Response parsing error
  SetInterceptor(expected_network_url, "sendTransaction", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":0}");
  TestSendSolanaTransaction("", mojom::SolanaProviderError::kParsingError,
                            l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor(expected_network_url, "sendTransaction", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":"
                 "{\"code\":-32601, \"message\": \"method does not exist\"}}");
  TestSendSolanaTransaction("", mojom::SolanaProviderError::kMethodNotFound,
                            "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestSendSolanaTransaction(
      "", mojom::SolanaProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, GetSolanaLatestBlockhash) {
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::SOL));
  auto expected_network_url =
      GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::SOL);
  SetInterceptor(expected_network_url, "getLatestBlockhash", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                 "{\"context\":{\"slot\":1069},\"value\":{\"blockhash\":"
                 "\"EkSnNWid2cvwEVnVx9aBqawnmiCNiDgp3gUdkDPTKN1N\", "
                 "\"lastValidBlockHeight\":18446744073709551615}}}");

  TestGetSolanaLatestBlockhash("EkSnNWid2cvwEVnVx9aBqawnmiCNiDgp3gUdkDPTKN1N",
                               UINT64_MAX, mojom::SolanaProviderError::kSuccess,
                               "");

  // Response parsing error
  SetInterceptor(expected_network_url, "getLatestBlockhash", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0\"}");
  TestGetSolanaLatestBlockhash(
      "", 0, mojom::SolanaProviderError::kParsingError,
      l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor(expected_network_url, "getLatestBlockhash", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":"
                 "{\"code\":-32601, \"message\": \"method does not exist\"}}");
  TestGetSolanaLatestBlockhash("", 0,
                               mojom::SolanaProviderError::kMethodNotFound,
                               "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSolanaLatestBlockhash(
      "", 0, mojom::SolanaProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, MigrateMultichainNetworks) {
  prefs()->ClearPref(kBraveWalletCustomNetworks);
  prefs()->ClearPref(kBraveWalletSelectedNetworks);

  absl::optional<base::Value> old_custom_networks = base::JSONReader::Read(R"([
    {
        "blockExplorerUrls": [
            "https://thaichain.io"
        ],
        "chainId": "0x7",
        "chainName": "ThaiChain",
        "iconUrls": [],
        "is_eip1559": false,
        "nativeCurrency": {
            "decimals": 18,
            "name": "ThaiChain Ether",
            "symbol": "TCH"
        },
        "rpcUrls": [
            "https://rpc.dome.cloud"
        ]
    },
    {
        "blockExplorerUrls": [
            "https://ubiqscan.io"
        ],
        "chainId": "0x8",
        "chainName": "Ubiq",
        "iconUrls": [],
        "is_eip1559": false,
        "nativeCurrency": {
            "decimals": 18,
            "name": "Ubiq Ether",
            "symbol": "UBQ"
        },
        "rpcUrls": [
            "https://rpc.octano.dev",
            "https://pyrus2.ubiqscan.io"
        ]
    }
  ])");
  prefs()->Set(kBraveWalletCustomNetworksDeprecated, *old_custom_networks);
  prefs()->SetString(kBraveWalletCurrentChainId, "0x3");

  JsonRpcService::MigrateMultichainNetworks(prefs());

  const base::Value* new_custom_networks =
      prefs()->GetDictionary(kBraveWalletCustomNetworks);
  ASSERT_TRUE(new_custom_networks);
  const base::Value* eth_custom_networks =
      new_custom_networks->FindKey(kEthereumPrefKey);
  ASSERT_TRUE(eth_custom_networks);
  EXPECT_EQ(*eth_custom_networks, *old_custom_networks);

  const base::Value* selected_networks =
      prefs()->GetDictionary(kBraveWalletSelectedNetworks);
  ASSERT_TRUE(selected_networks);
  const std::string* eth_selected_networks =
      selected_networks->FindStringKey(kEthereumPrefKey);
  ASSERT_TRUE(eth_selected_networks);
  EXPECT_EQ(*eth_selected_networks, "0x3");
  const std::string* sol_selected_networks =
      selected_networks->FindStringKey(kSolanaPrefKey);
  ASSERT_TRUE(sol_selected_networks);
  EXPECT_EQ(*sol_selected_networks, mojom::kSolanaMainnet);

  const std::string* fil_selected_networks =
      selected_networks->FindStringKey(kFilecoinPrefKey);
  ASSERT_TRUE(fil_selected_networks);
  EXPECT_EQ(*fil_selected_networks, mojom::kFilecoinMainnet);

  EXPECT_FALSE(prefs()->HasPrefPath(kBraveWalletCustomNetworksDeprecated));
  EXPECT_FALSE(prefs()->HasPrefPath(kBraveWalletCurrentChainId));
}

TEST_F(JsonRpcServiceUnitTest, GetSolanaSignatureStatuses) {
  std::string json = R"(
      {"jsonrpc":2.0, "id":1, "result":
        {
          "context": {"slot": 82},
          "value": [
            {
              "slot": 18446744073709551615,
              "confirmations": 10,
              "err": null,
              "confirmationStatus": "confirmed"
            },
            {
              "slot": 72,
              "confirmations": 18446744073709551615,
              "err": null,
              "confirmationStatus": "confirmed"
            },
            {
              "slot": 1092,
              "confirmations": null,
              "err": {"InstructionError":[0,{"Custom":1}]},
              "confirmationStatus": "finalized"
            },
            null
          ]
        }
      }
  )";
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::SOL));
  auto expected_network_url =
      GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::SOL);
  SetInterceptor(expected_network_url, "getSignatureStatuses", "", json);

  std::vector<std::string> tx_sigs = {
      "5VERv8NMvzbJMEkV8xnrLkEaWRtSz9CosKDYjCJjBRnbJLgp8uirBgmQpjKhoR4tjF3ZpRzr"
      "FmBV6UjKdiSZkQUW",
      "5j7s6NiJS3JAkvgkoc18WVAsiSaci2pxB2A6ueCJP4tprA2TFg9wSyTLeYouxPBJEMzJinEN"
      "TkpA52YStRW5Dia7",
      "4VERv8NMvzbJMEkV8xnrLkEaWRtSz9CosKDYjCJjBRnbJLgp8uirBgmQpjKhoR4tjF3ZpRzr"
      "FmBV6UjKdiSZkQUW",
      "45j7s6NiJS3JAkvgkoc18WVAsiSaci2pxB2A6ueCJP4tprA2TFg9wSyTLeYouxPBJEMzJinE"
      "NTkpA52YStRW5Dia7"};

  std::vector<absl::optional<SolanaSignatureStatus>> expected_statuses(
      {SolanaSignatureStatus(UINT64_MAX, 10u, "", "confirmed"),
       SolanaSignatureStatus(72u, UINT64_MAX, "", "confirmed"),
       SolanaSignatureStatus(
           1092u, 0u, R"({"InstructionError":[0,{"Custom":1}]})", "finalized"),
       absl::nullopt});
  TestGetSolanaSignatureStatuses(tx_sigs, expected_statuses,
                                 mojom::SolanaProviderError::kSuccess, "");

  // Response parsing error
  SetInterceptor(expected_network_url, "getSignatureStatuses", "",
                 R"({"jsonrpc":"2.0","id":1,"result":"0"})");
  TestGetSolanaSignatureStatuses(
      tx_sigs, std::vector<absl::optional<SolanaSignatureStatus>>(),
      mojom::SolanaProviderError::kParsingError,
      l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor(expected_network_url, "getSignatureStatuses", "",
                 R"({"jsonrpc":"2.0","id":1,"error":{
                      "code":-32601, "message": "method does not exist"}})");
  TestGetSolanaSignatureStatuses(
      tx_sigs, std::vector<absl::optional<SolanaSignatureStatus>>(),
      mojom::SolanaProviderError::kMethodNotFound, "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSolanaSignatureStatuses(
      tx_sigs, std::vector<absl::optional<SolanaSignatureStatus>>(),
      mojom::SolanaProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, GetSolanaAccountInfo) {
  std::string json = R"(
    {
      "jsonrpc":"2.0","id":1,
      "result": {
        "context":{"slot":123065869},
        "value":{
          "data":["SEVMTE8gV09STEQ=","base64"],
          "executable":false,
          "lamports":18446744073709551615,
          "owner":"11111111111111111111111111111111",
          "rentEpoch":18446744073709551615
        }
      }
    }
  )";
  auto expected_network_url =
      GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::SOL);

  SetInterceptor(expected_network_url, "getAccountInfo", "", json);

  SolanaAccountInfo expected_info;
  expected_info.lamports = UINT64_MAX;
  expected_info.owner = "11111111111111111111111111111111";
  expected_info.data = "SEVMTE8gV09STEQ=";
  expected_info.executable = false;
  expected_info.rent_epoch = UINT64_MAX;
  TestGetSolanaAccountInfo(expected_info, mojom::SolanaProviderError::kSuccess,
                           "");

  // value can be null for an account not on chain.
  SetInterceptor(
      expected_network_url, "getAccountInfo", "",
      R"({"jsonrpc":"2.0","result":{"context":{"slot":123121238},"value":null},"id":1})");
  TestGetSolanaAccountInfo(absl::nullopt, mojom::SolanaProviderError::kSuccess,
                           "");

  // Response parsing error
  SetInterceptor(expected_network_url, "getAccountInfo", "",
                 R"({"jsonrpc":"2.0","id":1,"result":"0"})");
  TestGetSolanaAccountInfo(absl::nullopt,
                           mojom::SolanaProviderError::kParsingError,
                           l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor(expected_network_url, "getAccountInfo", "",
                 R"({"jsonrpc":"2.0","id":1,"error":{
                      "code":-32601, "message": "method does not exist"}})");
  TestGetSolanaAccountInfo(absl::nullopt,
                           mojom::SolanaProviderError::kMethodNotFound,
                           "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSolanaAccountInfo(absl::nullopt,
                           mojom::SolanaProviderError::kInternalError,
                           l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, GetSolanaFeeForMessage) {
  std::string json = R"(
    {
      "jsonrpc":"2.0","id":1,
      "result": {
        "context":{"slot":123065869},
        "value": 18446744073709551615
      }
    }
  )";

  auto expected_network_url =
      GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::SOL);
  SetInterceptor(expected_network_url, "getFeeForMessage", "", json);
  std::string base64_encoded_string;
  base::Base64Encode("test", &base64_encoded_string);

  TestGetSolanaFeeForMessage(base64_encoded_string, UINT64_MAX,
                             mojom::SolanaProviderError::kSuccess, "");
  std::string base58_encoded_string = "JvSKSz9YHfqEQ8j";
  // Message has to be base64 encoded string and non-empty.
  TestGetSolanaFeeForMessage(
      "", 0, mojom::SolanaProviderError::kInvalidParams,
      l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));
  TestGetSolanaFeeForMessage(
      base58_encoded_string, 0, mojom::SolanaProviderError::kInvalidParams,
      l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));

  // value can be null for an account not on chain.
  SetInterceptor(expected_network_url, "getFeeForMessage", "",
                 R"({
                      "jsonrpc":"2.0",
                      "result":{
                      "context":{"slot":123121238},"value":null},"id":1
                    })");
  TestGetSolanaFeeForMessage(base64_encoded_string, 0,
                             mojom::SolanaProviderError::kSuccess, "");

  // Response parsing error
  SetInterceptor(expected_network_url, "getFeeForMessage", "",
                 R"({"jsonrpc":"2.0","id":1,"result":"0"})");
  TestGetSolanaFeeForMessage(
      base64_encoded_string, 0, mojom::SolanaProviderError::kParsingError,
      l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor(expected_network_url, "getFeeForMessage", "",
                 R"({
                      "jsonrpc":"2.0","id":1,
                      "error":
                        {"code":-32601, "message": "method does not exist"}
                    })");
  TestGetSolanaFeeForMessage(base64_encoded_string, 0,
                             mojom::SolanaProviderError::kMethodNotFound,
                             "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSolanaFeeForMessage(
      base64_encoded_string, 0, mojom::SolanaProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, GetEthTransactionCount) {
  bool callback_called = false;
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH),
                 "eth_getTransactionCount", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x1\"}");

  json_rpc_service_->GetEthTransactionCount(
      "0x4e02f254184E904300e0775E4b8eeCB1",
      base::BindOnce(&OnEthUint256Response, &callback_called,
                     mojom::ProviderError::kSuccess, "", 1));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetEthTransactionCount(
      "0x4e02f254184E904300e0775E4b8eeCB1",
      base::BindOnce(&OnEthUint256Response, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), 0));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->GetEthTransactionCount(
      "0x4e02f254184E904300e0775E4b8eeCB1",
      base::BindOnce(&OnEthUint256Response, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), 0));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetEthTransactionCount(
      "0x4e02f254184E904300e0775E4b8eeCB1",
      base::BindOnce(&OnEthUint256Response, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", 0));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetFilTransactionCount) {
  bool callback_called = false;
  SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL);
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.MpoolGetNonce", "",
                 R"({"jsonrpc":"2.0","id":1,"result":18446744073709551615})");

  json_rpc_service_->GetFilTransactionCount(
      "t1h4n7rphclbmwyjcp6jrdiwlfcuwbroxy3jvg33q",
      base::BindOnce(&OnFilUint256Response, &callback_called,
                     mojom::FilecoinProviderError::kSuccess, "", UINT64_MAX));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetFilTransactionCount(
      "t1h4n7rphclbmwyjcp6jrdiwlfcuwbroxy3jvg33q",
      base::BindOnce(&OnFilUint256Response, &callback_called,
                     mojom::FilecoinProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), 0));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.MpoolGetNonce", "", R"({"jsonrpc":"2.0","id":1})");
  json_rpc_service_->GetFilTransactionCount(
      "t1h4n7rphclbmwyjcp6jrdiwlfcuwbroxy3jvg33q",
      base::BindOnce(&OnFilUint256Response, &callback_called,
                     mojom::FilecoinProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), 0));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetFilecoinActorErrorJsonErrorResponse();
  json_rpc_service_->GetFilTransactionCount(
      "t1h4n7rphclbmwyjcp6jrdiwlfcuwbroxy3jvg33q",
      base::BindOnce(&OnFilUint256Response, &callback_called,
                     mojom::FilecoinProviderError::kActorNotFound,
                     "resolution lookup failed", 0));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetSolanaBlockHeight) {
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::SOL));
  auto expected_network_url =
      GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::SOL);
  SetInterceptor(expected_network_url, "getBlockHeight", "",
                 R"({"jsonrpc":"2.0", "id":1, "result":18446744073709551615})");

  TestGetSolanaBlockHeight(UINT64_MAX, mojom::SolanaProviderError::kSuccess,
                           "");

  // Response parsing error
  SetInterceptor(expected_network_url, "getBlockHeight", "",
                 R"({"jsonrpc":"2.0","id":1})");
  TestGetSolanaBlockHeight(0, mojom::SolanaProviderError::kParsingError,
                           l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor(expected_network_url, "getBlockHeight", "",
                 R"({"jsonrpc": "2.0", "id": 1,
                     "error": {
                       "code":-32601,
                       "message":"method does not exist"
                     }
                    })");
  TestGetSolanaBlockHeight(0, mojom::SolanaProviderError::kMethodNotFound,
                           "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSolanaBlockHeight(0, mojom::SolanaProviderError::kInternalError,
                           l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, GetFilEstimateGas) {
  SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL);
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.GasEstimateMessageGas", "",
                 GetGasFilEstimateResponse(INT64_MAX));

  GetFilEstimateGas("t1tquwkjo6qvweah2g2yikewr7y5dyjds42pnrn3a",
                    "t1h5tg3bhp5r56uzgjae2373znti6ygq4agkx4hzq",
                    "1000000000000000000", "100466", "101520", INT64_MAX,
                    mojom::FilecoinProviderError::kSuccess);

  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.GasEstimateMessageGas", "",
                 GetGasFilEstimateResponse(INT64_MIN));

  GetFilEstimateGas("t1tquwkjo6qvweah2g2yikewr7y5dyjds42pnrn3a",
                    "t1h5tg3bhp5r56uzgjae2373znti6ygq4agkx4hzq",
                    "1000000000000000000", "100466", "101520", INT64_MIN,
                    mojom::FilecoinProviderError::kSuccess);

  GetFilEstimateGas("", "t1h5tg3bhp5r56uzgjae2373znti6ygq4agkx4hzq",
                    "1000000000000000000", "", "", 0,
                    mojom::FilecoinProviderError::kInvalidParams);
  GetFilEstimateGas("t1tquwkjo6qvweah2g2yikewr7y5dyjds42pnrn3a", "",
                    "1000000000000000000", "", "", 0,
                    mojom::FilecoinProviderError::kInvalidParams);

  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.GasEstimateMessageGas", "", "");
  GetFilEstimateGas("t1tquwkjo6qvweah2g2yikewr7y5dyjds42pnrn3a",
                    "t1h5tg3bhp5r56uzgjae2373znti6ygq4agkx4hzq",
                    "1000000000000000000", "", "", 0,
                    mojom::FilecoinProviderError::kInternalError);
}

TEST_F(JsonRpcServiceUnitTest, GetFilChainHead) {
  SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL);
  std::string response = R"(
    { "id": 1, "jsonrpc": "2.0",
      "result": {
        "Blocks":[],
        "Cids": [{
              "/": "bafy2bzaceauxm7waysuftonc4vod6wk4trdjx2ibw233dos6jcvkf5nrhflju"
        }],
        "Height": 18446744073709551615
      }
    })";
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.ChainHead", "", response);
  GetFilBlockHeight(UINT64_MAX, mojom::FilecoinProviderError::kSuccess, "");
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.ChainHead", "", "");
  GetFilBlockHeight(0, mojom::FilecoinProviderError::kInternalError,
                    l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.ChainHead", "", R"(
    {"jsonrpc":"2.0","id":1,
      "error":{
        "code":-32602,
        "message":"wrong param count (method 'Filecoin.ChainHead'): 1 != 0"
      }
    })");
  GetFilBlockHeight(0, mojom::FilecoinProviderError::kInvalidParams,
                    "wrong param count (method 'Filecoin.ChainHead'): 1 != 0");
}

TEST_F(JsonRpcServiceUnitTest, GetFilStateSearchMsgLimited) {
  SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL);
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.StateSearchMsgLimited", "",
                 GetFilStateSearchMsgLimitedResponse(0));

  GetFilStateSearchMsgLimited(
      "bafy2bzacebundyopm3trenj47hxkwiqn2cbvvftz3fss4dxuttu2u6xbbtkqy", 30, 0,
      mojom::FilecoinProviderError::kSuccess, "");

  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.StateSearchMsgLimited", "", R"(
    {
        "id": 1,
        "jsonrpc": "2.0",
        "error":{
          "code":-32602,
          "message":"wrong param count"
        }
  })");
  GetFilStateSearchMsgLimited(
      "bafy2bzacebundyopm3trenj47hxkwiqn2cbvvftz3fss4dxuttu2u6xbbtkqy", 30, -1,
      mojom::FilecoinProviderError::kInvalidParams, "wrong param count");

  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.StateSearchMsgLimited", "", R"({,})");
  GetFilStateSearchMsgLimited(
      "bafy2bzacebundyopm3trenj47hxkwiqn2cbvvftz3fss4dxuttu2u6xbbtkqy", 30, -1,
      mojom::FilecoinProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));

  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.StateSearchMsgLimited", "",
                 GetFilStateSearchMsgLimitedResponse(INT64_MAX));
  GetFilStateSearchMsgLimited(
      "bafy2bzacebundyopm3trenj47hxkwiqn2cbvvftz3fss4dxuttu2u6xbbtkqy", 30,
      INT64_MAX, mojom::FilecoinProviderError::kSuccess, "");

  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.StateSearchMsgLimited", "",
                 GetFilStateSearchMsgLimitedResponse(INT64_MIN));
  GetFilStateSearchMsgLimited(
      "bafy2bzacebundyopm3trenj47hxkwiqn2cbvvftz3fss4dxuttu2u6xbbtkqy", 30,
      INT64_MIN, mojom::FilecoinProviderError::kSuccess, "");
}

TEST_F(JsonRpcServiceUnitTest, SendFilecoinTransaction) {
  SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL);
  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.MpoolPush", "",
                 R"({
                   "id": 1,
                   "jsonrpc": "2.0",
                   "result": {
                     "/": "cid"
                   }
                 })");
  GetSendFilecoinTransaction("{}", "cid",
                             mojom::FilecoinProviderError::kSuccess, "");

  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.MpoolPush", "", R"(
    {
        "id": 1,
        "jsonrpc": "2.0",
        "error":{
          "code":-32602,
          "message":"wrong param count"
        }
  })");
  GetSendFilecoinTransaction("{}", "",
                             mojom::FilecoinProviderError::kInvalidParams,
                             "wrong param count");

  SetInterceptor(GetNetwork(mojom::kLocalhostChainId, mojom::CoinType::FIL),
                 "Filecoin.MpoolPush", "", "");
  GetSendFilecoinTransaction(
      "{}", "", mojom::FilecoinProviderError::kParsingError,
      l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));
  GetSendFilecoinTransaction(
      "broken json", "", mojom::FilecoinProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
  GetSendFilecoinTransaction(
      "", "", mojom::FilecoinProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

class EthCallHandler {
 public:
  EthCallHandler(const EthAddress& to, const eth_abi::Bytes4& selector)
      : to_(to), selectors_({selector}) {}
  EthCallHandler(const EthAddress& to,
                 const std::vector<eth_abi::Bytes4>& selectors)
      : to_(to), selectors_(selectors) {}
  virtual ~EthCallHandler() = default;

  bool CallSupported(const EthAddress& to, eth_abi::Span call_data) {
    if (to != to_)
      return false;

    auto [selector, _] =
        eth_abi::ExtractFunctionSelectorAndArgsFromCall(call_data);

    for (const auto& s : selectors_) {
      if (base::ranges::equal(s, selector))
        return true;
    }
    return false;
  }

  virtual absl::optional<std::string> HandleEthCall(
      eth_abi::Span call_data) = 0;

 protected:
  EthAddress to_;
  std::vector<eth_abi::Bytes4> selectors_;
};

class EnsGetResolverHandler : public EthCallHandler {
 public:
  EnsGetResolverHandler(const std::string& host_name,
                        const EthAddress& resolver_address)
      : EthCallHandler(EthAddress::FromHex(GetEnsRegistryContractAddress(
                           mojom::kMainnetChainId)),
                       GetFunctionHashBytes4("resolver(bytes32)")),
        host_name_(host_name),
        resolver_address_(resolver_address) {}
  ~EnsGetResolverHandler() override = default;

  absl::optional<std::string> HandleEthCall(eth_abi::Span call_data) override {
    auto [_, args] = eth_abi::ExtractFunctionSelectorAndArgsFromCall(call_data);
    auto namehash_bytes = eth_abi::ExtractFixedBytesFromTuple(args, 32, 0);
    EXPECT_TRUE(namehash_bytes);

    if (!base::ranges::equal(*namehash_bytes, Namehash(host_name_))) {
      return MakeJsonRpcTupleResponse(
          eth_abi::TupleEncoder().AddAddress(EthAddress::ZeroAddress()));
    }

    return MakeJsonRpcTupleResponse(
        eth_abi::TupleEncoder().AddAddress(resolver_address_));
  }

 private:
  std::string host_name_;
  EthAddress resolver_address_;
};

class Ensip10SupportHandler : public EthCallHandler {
 public:
  explicit Ensip10SupportHandler(const EthAddress& resolver_address)
      : EthCallHandler(resolver_address,
                       GetFunctionHashBytes4("supportsInterface(bytes4)")) {}
  ~Ensip10SupportHandler() override = default;

  absl::optional<std::string> HandleEthCall(eth_abi::Span call_data) override {
    auto [_, args] = eth_abi::ExtractFunctionSelectorAndArgsFromCall(call_data);

    auto arg_selector = eth_abi::ExtractFixedBytesFromTuple(args, 4, 0);
    EXPECT_TRUE(arg_selector);
    EXPECT_TRUE(base::ranges::equal(*arg_selector, kResolveBytesBytesSelector));

    return MakeJsonRpcTupleResponse(
        eth_abi::TupleEncoder().AddUint256(uint256_t(result_value_)));
  }

  void DisableSupport() { result_value_ = 0; }

 private:
  uint256_t result_value_ = 1;
};

class EnsGetRecordHandler : public EthCallHandler {
 public:
  explicit EnsGetRecordHandler(const EthAddress& resolver_address,
                               const std::string& host_name,
                               const EthAddress& result_address,
                               const std::vector<uint8_t>& result_contenthash)
      : EthCallHandler(resolver_address,
                       {GetFunctionHashBytes4("addr(bytes32)"),
                        GetFunctionHashBytes4("contenthash(bytes32)")}),
        host_name_(host_name),
        result_address_(result_address),
        result_contenthash_(result_contenthash) {}
  ~EnsGetRecordHandler() override = default;

  absl::optional<std::string> HandleEthCall(eth_abi::Span call_data) override {
    auto [selector, args] =
        eth_abi::ExtractFunctionSelectorAndArgsFromCall(call_data);

    auto namehash_bytes = eth_abi::ExtractFixedBytesFromTuple(args, 32, 0);
    EXPECT_TRUE(namehash_bytes);
    bool host_matches =
        base::ranges::equal(*namehash_bytes, Namehash(host_name_));

    if (base::ranges::equal(selector, GetFunctionHashBytes4("addr(bytes32)"))) {
      auto eth_address = EthAddress::ZeroAddress();
      if (host_matches)
        eth_address = result_address_;

      return MakeJsonRpcTupleResponse(
          eth_abi::TupleEncoder().AddAddress(eth_address));
    } else if (base::ranges::equal(
                   selector, GetFunctionHashBytes4("contenthash(bytes32)"))) {
      std::vector<uint8_t> contenthash;
      if (host_matches)
        contenthash = result_contenthash_;

      return MakeJsonRpcTupleResponse(
          eth_abi::TupleEncoder().AddBytes(contenthash));
    }
    NOTREACHED();
    return absl::nullopt;
  }

 private:
  std::string host_name_;
  EthAddress result_address_;
  std::vector<uint8_t> result_contenthash_;
};

class Ensip10ResolveHandler : public EthCallHandler {
 public:
  Ensip10ResolveHandler(const EthAddress& resolver_address,
                        const std::string& host_name,
                        const GURL& gateway_url)
      : EthCallHandler(resolver_address,
                       GetFunctionHashBytes4("resolve(bytes,bytes)")),
        resolver_address_(resolver_address),
        host_name_(host_name),
        gateway_url_(gateway_url) {}

  ~Ensip10ResolveHandler() override = default;

  absl::optional<std::string> HandleEthCall(eth_abi::Span call_data) override {
    auto extra_data = eth_abi::TupleEncoder().AddString("extra data").Encode();

    // Sending `bytes` as callData argument to gateway. Gateway will decode it
    // and return requested ens record.
    auto offchain_lookup =
        eth_abi::TupleEncoder()
            .AddAddress(resolver_address_)          // address
            .AddStringArray({gateway_url_.spec()})  // urls
            .AddBytes(call_data)                    // callData
            .AddFixedBytes(GetFunctionHashBytes4(
                "resolveCallback(bytes,bytes)"))  // callbackFunction
            .AddBytes(extra_data)                 // extraData
            .EncodeWithSelector(kOffchainLookupSelector);

    return MakeJsonRpcErrorResponseWithData(3, "execution reverted",
                                            ToHex(offchain_lookup));
  }

 private:
  EthAddress resolver_address_;
  std::string host_name_;
  GURL gateway_url_;
};

class Ensip10ResolveCallbackHandler : public EthCallHandler {
 public:
  explicit Ensip10ResolveCallbackHandler(const EthAddress& resolver_address)
      : EthCallHandler(resolver_address,
                       GetFunctionHashBytes4("resolveCallback(bytes,bytes)")),
        resolver_address_(resolver_address) {}
  ~Ensip10ResolveCallbackHandler() override = default;

  absl::optional<std::string> HandleEthCall(eth_abi::Span call_data) override {
    auto [_, args] = eth_abi::ExtractFunctionSelectorAndArgsFromCall(call_data);

    auto extra_data_bytes = eth_abi::ExtractBytesFromTuple(args, 1);
    EXPECT_EQ("extra data",
              eth_abi::ExtractStringFromTuple(*extra_data_bytes, 0));

    auto bytes_result = eth_abi::ExtractBytesFromTuple(args, 0);
    if (!bytes_result)
      return absl::nullopt;

    return MakeJsonRpcTupleResponse(
        eth_abi::TupleEncoder().AddBytes(*bytes_result));
  }

 private:
  EthAddress resolver_address_;
};

class JsonRpcEnpointHandler {
 public:
  explicit JsonRpcEnpointHandler(const GURL& endpoint) : endpoint_(endpoint) {}

  absl::optional<std::string> HandleRequest(
      const network::ResourceRequest& request) {
    if (request.url != endpoint_)
      return absl::nullopt;

    auto value = ToValue(request);
    if (value && value->is_dict()) {
      auto response = HandleCall(value->GetDict());
      if (response) {
        return response;
      }
    }

    return absl::nullopt;
  }

  void AddEthCallHandler(EthCallHandler* handler) {
    eth_call_handlers_.push_back(handler);
  }

 protected:
  absl::optional<std::string> HandleCall(const base::Value::Dict& dict) {
    auto* method = dict.FindString("method");
    if (!method || *method != "eth_call")
      return absl::nullopt;

    auto* params_list = dict.FindList("params");
    if (!params_list || params_list->size() == 0 ||
        !params_list->begin()->is_dict()) {
      return absl::nullopt;
    }

    auto& transaction_params = params_list->begin()->GetDict();
    auto* data_param = transaction_params.FindString("data");
    auto* to_param = transaction_params.FindString("to");
    if (!data_param || !to_param || !EthAddress::FromHex(*to_param).IsValid())
      return absl::nullopt;

    auto call_data = PrefixedHexStringToBytes(*data_param);
    if (!call_data)
      return absl::nullopt;

    for (auto* handler : eth_call_handlers_) {
      if (!handler->CallSupported(EthAddress::FromHex(*to_param), *call_data))
        continue;

      auto response = handler->HandleEthCall(*call_data);
      if (response)
        return response;
    }
    return absl::nullopt;
  }

 private:
  GURL endpoint_;
  std::vector<EthCallHandler*> eth_call_handlers_;
};

class OffchainGatewayHandler {
 public:
  OffchainGatewayHandler(const GURL& gateway_url,
                         const EthAddress& resolver_address,
                         const EthAddress& offchain_eth_address,
                         const std::vector<uint8_t>& offchain_contenthash)
      : gateway_url_(gateway_url),
        resolver_address_(resolver_address),
        offchain_eth_address_(offchain_eth_address),
        offchain_contenthash_(offchain_contenthash) {}

  absl::optional<std::string> HandleRequest(
      const network::ResourceRequest& request) {
    if (request.url.host() != gateway_url_.host())
      return absl::nullopt;

    if (respond_with_500_)
      return "";

    auto payload = ToValue(request);
    if (!payload || !payload->is_dict())
      return absl::nullopt;
    auto* sender = payload->GetDict().FindString("sender");
    EXPECT_EQ(EthAddress::FromHex(*sender), resolver_address_);

    auto* data = payload->GetDict().FindString("data");
    auto bytes = PrefixedHexStringToBytes(*data);
    if (!bytes) {
      NOTREACHED();
      return absl::nullopt;
    }

    auto [selector, args] =
        eth_abi::ExtractFunctionSelectorAndArgsFromCall(*bytes);

    if (ToHex(selector) != GetFunctionHash("resolve(bytes,bytes)")) {
      NOTREACHED();
      return absl::nullopt;
    }

    auto dns_encoded_name = eth_abi::ExtractBytesFromTuple(args, 0);
    if (!dns_encoded_name) {
      NOTREACHED();
      return absl::nullopt;
    }
    if (dns_encoded_name != ens::DnsEncode("offchainexample.eth")) {
      return absl::nullopt;
    }

    auto encoded_call = eth_abi::ExtractBytesFromTuple(args, 1);
    auto [encoded_call_selector, _] =
        eth_abi::ExtractFunctionSelectorAndArgsFromCall(*encoded_call);

    std::string data_value;
    if (base::ranges::equal(encoded_call_selector, kAddrBytes32Selector)) {
      auto address = respond_with_no_record_ ? EthAddress::ZeroAddress()
                                             : offchain_eth_address_;
      data_value = ToHex(eth_abi::TupleEncoder().AddAddress(address).Encode());
    } else if (base::ranges::equal(encoded_call_selector,
                                   kContentHashBytes32Selector)) {
      auto contenthash = respond_with_no_record_ ? std::vector<uint8_t>()
                                                 : offchain_contenthash_;
      data_value =
          ToHex(eth_abi::TupleEncoder().AddBytes(contenthash).Encode());
    } else {
      NOTREACHED();
      return absl::nullopt;
    }

    base::Value::Dict result;
    result.Set("data", data_value);
    std::string response;
    base::JSONWriter::Write(result, &response);
    return response;
  }

  void SetRespondWith500() { respond_with_500_ = true; }
  void SetRespondWithNoRecord() { respond_with_no_record_ = true; }

 private:
  GURL gateway_url_;
  EthAddress resolver_address_;
  EthAddress offchain_eth_address_;
  std::vector<uint8_t> offchain_contenthash_;
  bool respond_with_500_ = false;
  bool respond_with_no_record_ = false;
};

class ENSL2JsonRpcServiceUnitTest : public JsonRpcServiceUnitTest {
 public:
  ENSL2JsonRpcServiceUnitTest() = default;

  void SetUp() override {
    JsonRpcServiceUnitTest::SetUp();

    json_rpc_endpoint_handler_ = std::make_unique<JsonRpcEnpointHandler>(
        GetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH));

    ens_resolver_handler_ =
        std::make_unique<EnsGetResolverHandler>(ens_host(), resolver_address());
    ens_get_record_handler_ = std::make_unique<EnsGetRecordHandler>(
        resolver_address(), ens_host(), onchain_eth_addr(),
        onchain_contenthash());
    ensip10_support_handler_ =
        std::make_unique<Ensip10SupportHandler>(resolver_address());
    ensip10_resolve_handler_ = std::make_unique<Ensip10ResolveHandler>(
        resolver_address(), ens_host(), gateway_url());
    ensip10_resolve_callback_handler_ =
        std::make_unique<Ensip10ResolveCallbackHandler>(resolver_address());

    json_rpc_endpoint_handler_->AddEthCallHandler(ens_resolver_handler_.get());
    json_rpc_endpoint_handler_->AddEthCallHandler(
        ens_get_record_handler_.get());
    json_rpc_endpoint_handler_->AddEthCallHandler(
        ensip10_support_handler_.get());
    json_rpc_endpoint_handler_->AddEthCallHandler(
        ensip10_resolve_handler_.get());
    json_rpc_endpoint_handler_->AddEthCallHandler(
        ensip10_resolve_callback_handler_.get());

    offchain_gateway_handler_ = std::make_unique<OffchainGatewayHandler>(
        gateway_url(), resolver_address(), offchain_eth_addr(),
        offchain_contenthash());

    url_loader_factory_.SetInterceptor(base::BindRepeating(
        &ENSL2JsonRpcServiceUnitTest::HandleRequest, base::Unretained(this)));
  }

  std::string ens_host() { return "offchainexample.eth"; }
  GURL gateway_url() { return GURL("https://gateway.brave.com/"); }
  EthAddress resolver_address() {
    return EthAddress::FromHex("0xc1735677a60884abbcf72295e88d47764beda282");
  }
  EthAddress offchain_eth_addr() {
    return EthAddress::FromHex("0xaabbccddeeaabbccddeeaabbccddeeaabbccddee");
  }

  EthAddress onchain_eth_addr() {
    return EthAddress::FromHex("0x1234567890123456789012345678901234567890");
  }

  std::vector<uint8_t> offchain_contenthash() {
    std::string contenthash =
        "e30101701220f073be187e8e06039796c432a"
        "5bdd6da3f403c2f93fa5d9dbdc5547c7fe0e3bc";
    std::vector<uint8_t> bytes;
    base::HexStringToBytes(contenthash, &bytes);
    return bytes;
  }

  std::vector<uint8_t> onchain_contenthash() {
    std::string contenthash =
        "e50101701220f073be187e8e06039796c432a"
        "5bdd6da3f403c2f93fa5d9dbdc5547c7fe0e3bc";
    std::vector<uint8_t> bytes;
    base::HexStringToBytes(contenthash, &bytes);
    return bytes;
  }

  mojom::EnsOffchainLookupOptionsPtr AllowOffchain() {
    return mojom::EnsOffchainLookupOptions::New(true, false);
  }

 protected:
  void HandleRequest(const network::ResourceRequest& request) {
    url_loader_factory_.ClearResponses();
    if (auto response = json_rpc_endpoint_handler_->HandleRequest(request)) {
      url_loader_factory_.AddResponse(request.url.spec(), *response);
    } else if (auto response =
                   offchain_gateway_handler_->HandleRequest(request)) {
      if (response->empty()) {
        url_loader_factory_.AddResponse(request.url.spec(), "",
                                        net::HTTP_INTERNAL_SERVER_ERROR);
      } else {
        url_loader_factory_.AddResponse(request.url.spec(), *response);
      }
    }
  }

  std::unique_ptr<EnsGetResolverHandler> ens_resolver_handler_;
  std::unique_ptr<EnsGetRecordHandler> ens_get_record_handler_;
  std::unique_ptr<Ensip10SupportHandler> ensip10_support_handler_;
  std::unique_ptr<Ensip10ResolveHandler> ensip10_resolve_handler_;
  std::unique_ptr<Ensip10ResolveCallbackHandler>
      ensip10_resolve_callback_handler_;
  std::unique_ptr<JsonRpcEnpointHandler> json_rpc_endpoint_handler_;
  std::unique_ptr<OffchainGatewayHandler> offchain_gateway_handler_;

 private:
  base::test::ScopedFeatureList feature_list_{
      features::kBraveWalletENSL2Feature};
};

TEST_F(ENSL2JsonRpcServiceUnitTest, GetEthAddr) {
  base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
  EXPECT_CALL(callback, Run(offchain_eth_addr().ToHex(), false,
                            mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->EnsGetEthAddr(ens_host(), AllowOffchain(), callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(ENSL2JsonRpcServiceUnitTest, GetEthAddr_NoResolver) {
  base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
  EXPECT_CALL(callback,
              Run("", false, mojom::ProviderError::kInternalError,
                  l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
  json_rpc_service_->EnsGetEthAddr("unknown-host.eth", AllowOffchain(),
                                   callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(ENSL2JsonRpcServiceUnitTest, GetEthAddr_NoEnsip10Support) {
  // TODO(apaymyshev): ENS offchain lookup should work without ensip10
  // support.

  // Turning off Ensip-10 support for resolver so addr(bytes32) is called.
  ensip10_support_handler_->DisableSupport();

  base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
  EXPECT_CALL(callback, Run(onchain_eth_addr().ToHex(), false,
                            mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->EnsGetEthAddr(ens_host(), AllowOffchain(), callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(ENSL2JsonRpcServiceUnitTest, GetEthAddr_Gateway500Error) {
  // Gateway request fails.
  offchain_gateway_handler_->SetRespondWith500();

  base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
  EXPECT_CALL(callback,
              Run("", false, mojom::ProviderError::kInternalError,
                  l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
  json_rpc_service_->EnsGetEthAddr(ens_host(), AllowOffchain(), callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(ENSL2JsonRpcServiceUnitTest, GetEthAddr_GatewayNoRecord) {
  // No data record in gateway.
  offchain_gateway_handler_->SetRespondWithNoRecord();

  base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
  EXPECT_CALL(callback,
              Run("", false, mojom::ProviderError::kInvalidParams,
                  l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS)));
  json_rpc_service_->EnsGetEthAddr(ens_host(), AllowOffchain(), callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(ENSL2JsonRpcServiceUnitTest, GetEthAddr_Consent) {
  EXPECT_EQ(
      decentralized_dns::EnsOffchainResolveMethod::kAsk,
      decentralized_dns::GetEnsOffchainResolveMethod(local_state_prefs()));

  // Call with defaults.
  {
    base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
    // Called with `require_offchain_consent` == true.
    EXPECT_CALL(callback, Run("", true, mojom::ProviderError::kSuccess, ""));
    json_rpc_service_->EnsGetEthAddr(ens_host(), nullptr, callback.Get());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(
        decentralized_dns::EnsOffchainResolveMethod::kAsk,
        decentralized_dns::GetEnsOffchainResolveMethod(local_state_prefs()));
  }

  // Allow once.
  {
    base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
    EXPECT_CALL(callback, Run(offchain_eth_addr().ToHex(), false,
                              mojom::ProviderError::kSuccess, ""));
    json_rpc_service_->EnsGetEthAddr(
        ens_host(), mojom::EnsOffchainLookupOptions::New(true, false),
        callback.Get());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(
        decentralized_dns::EnsOffchainResolveMethod::kAsk,
        decentralized_dns::GetEnsOffchainResolveMethod(local_state_prefs()));
  }

  // Allow and remember.
  {
    base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
    EXPECT_CALL(callback, Run(offchain_eth_addr().ToHex(), false,
                              mojom::ProviderError::kSuccess, ""));
    json_rpc_service_->EnsGetEthAddr(
        ens_host(), mojom::EnsOffchainLookupOptions::New(true, true),
        callback.Get());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(
        decentralized_dns::EnsOffchainResolveMethod::kEnabled,
        decentralized_dns::GetEnsOffchainResolveMethod(local_state_prefs()));
  }

  // Allowed without explicit consent.
  {
    base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
    EXPECT_CALL(callback, Run(offchain_eth_addr().ToHex(), false,
                              mojom::ProviderError::kSuccess, ""));
    json_rpc_service_->EnsGetEthAddr(ens_host(), nullptr, callback.Get());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(
        decentralized_dns::EnsOffchainResolveMethod::kEnabled,
        decentralized_dns::GetEnsOffchainResolveMethod(local_state_prefs()));
  }

  // Reset in prefs.
  decentralized_dns::SetEnsOffchainResolveMethod(
      local_state_prefs(), decentralized_dns::EnsOffchainResolveMethod::kAsk);

  // Fails after no once.
  {
    base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
    EXPECT_CALL(callback,
                Run("", false, mojom::ProviderError::kInternalError,
                    l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
    json_rpc_service_->EnsGetEthAddr(
        ens_host(), mojom::EnsOffchainLookupOptions::New(false, false),
        callback.Get());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(
        decentralized_dns::EnsOffchainResolveMethod::kAsk,
        decentralized_dns::GetEnsOffchainResolveMethod(local_state_prefs()));
  }

  // Fails after no and remember.
  {
    base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
    EXPECT_CALL(callback,
                Run("", false, mojom::ProviderError::kInternalError,
                    l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
    json_rpc_service_->EnsGetEthAddr(
        ens_host(), mojom::EnsOffchainLookupOptions::New(false, true),
        callback.Get());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(
        decentralized_dns::EnsOffchainResolveMethod::kDisabled,
        decentralized_dns::GetEnsOffchainResolveMethod(local_state_prefs()));
  }

  // Fails after no explicit consent.
  {
    base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
    EXPECT_CALL(callback,
                Run("", false, mojom::ProviderError::kInternalError,
                    l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
    json_rpc_service_->EnsGetEthAddr(ens_host(), nullptr, callback.Get());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(
        decentralized_dns::EnsOffchainResolveMethod::kDisabled,
        decentralized_dns::GetEnsOffchainResolveMethod(local_state_prefs()));
  }

  // Still ok with explicit consent. Should not happen with our ui, but still
  // fixing that behavior.
  {
    base::MockCallback<JsonRpcService::EnsGetEthAddrCallback> callback;
    EXPECT_CALL(callback, Run(offchain_eth_addr().ToHex(), false,
                              mojom::ProviderError::kSuccess, ""));
    json_rpc_service_->EnsGetEthAddr(
        ens_host(), mojom::EnsOffchainLookupOptions::New(true, false),
        callback.Get());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(
        decentralized_dns::EnsOffchainResolveMethod::kDisabled,
        decentralized_dns::GetEnsOffchainResolveMethod(local_state_prefs()));
  }
}

TEST_F(ENSL2JsonRpcServiceUnitTest, GetContentHash) {
  decentralized_dns::SetEnsOffchainResolveMethod(
      local_state_prefs(),
      decentralized_dns::EnsOffchainResolveMethod::kEnabled);

  base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
  EXPECT_CALL(callback, Run(offchain_contenthash(), false,
                            mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->EnsGetContentHash(ens_host(), callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(ENSL2JsonRpcServiceUnitTest, GetContentHash_NoResolver) {
  decentralized_dns::SetEnsOffchainResolveMethod(
      local_state_prefs(),
      decentralized_dns::EnsOffchainResolveMethod::kEnabled);

  base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
  EXPECT_CALL(
      callback,
      Run(std::vector<uint8_t>(), false, mojom::ProviderError::kInternalError,
          l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
  json_rpc_service_->EnsGetContentHash("unknown-host.eth", callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(ENSL2JsonRpcServiceUnitTest, GetContentHash_NoEnsip10Support) {
  decentralized_dns::SetEnsOffchainResolveMethod(
      local_state_prefs(),
      decentralized_dns::EnsOffchainResolveMethod::kEnabled);

  // TODO(apaymyshev): ENS offchain lookup should work without ensip10
  // support.

  // Turning off Ensip-10 support for resolver so contenthash(bytes32) is
  // called.
  ensip10_support_handler_->DisableSupport();

  base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
  EXPECT_CALL(callback, Run(onchain_contenthash(), false,
                            mojom::ProviderError::kSuccess, ""));
  json_rpc_service_->EnsGetContentHash(ens_host(), callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(ENSL2JsonRpcServiceUnitTest, GetContentHash_Gateway500Error) {
  decentralized_dns::SetEnsOffchainResolveMethod(
      local_state_prefs(),
      decentralized_dns::EnsOffchainResolveMethod::kEnabled);

  // Gateway request fails.
  offchain_gateway_handler_->SetRespondWith500();

  base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
  EXPECT_CALL(
      callback,
      Run(std::vector<uint8_t>(), false, mojom::ProviderError::kInternalError,
          l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
  json_rpc_service_->EnsGetContentHash(ens_host(), callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(ENSL2JsonRpcServiceUnitTest, GetContentHash_GatewayNoRecord) {
  decentralized_dns::SetEnsOffchainResolveMethod(
      local_state_prefs(),
      decentralized_dns::EnsOffchainResolveMethod::kEnabled);

  // No data record in gateway.
  offchain_gateway_handler_->SetRespondWithNoRecord();

  base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
  EXPECT_CALL(
      callback,
      Run(std::vector<uint8_t>(), false, mojom::ProviderError::kInvalidParams,
          l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS)));
  json_rpc_service_->EnsGetContentHash(ens_host(), callback.Get());
  base::RunLoop().RunUntilIdle();
}

TEST_F(ENSL2JsonRpcServiceUnitTest, GetContentHash_Consent) {
  EXPECT_EQ(
      decentralized_dns::EnsOffchainResolveMethod::kAsk,
      decentralized_dns::GetEnsOffchainResolveMethod(local_state_prefs()));

  // Ask by default.
  {
    base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
    EXPECT_CALL(callback, Run(std::vector<uint8_t>(), true,
                              mojom::ProviderError::kSuccess, ""));
    json_rpc_service_->EnsGetContentHash(ens_host(), callback.Get());
    base::RunLoop().RunUntilIdle();
  }

  decentralized_dns::SetEnsOffchainResolveMethod(
      local_state_prefs(),
      decentralized_dns::EnsOffchainResolveMethod::kEnabled);
  // Ok when enabled by prefs.
  {
    base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
    EXPECT_CALL(callback, Run(offchain_contenthash(), false,
                              mojom::ProviderError::kSuccess, ""));
    json_rpc_service_->EnsGetContentHash(ens_host(), callback.Get());
    base::RunLoop().RunUntilIdle();
  }

  // Disable in prefs.
  decentralized_dns::SetEnsOffchainResolveMethod(
      local_state_prefs(),
      decentralized_dns::EnsOffchainResolveMethod::kDisabled);

  // Fails when disabled in prefs.
  {
    base::MockCallback<JsonRpcService::EnsGetContentHashCallback> callback;
    EXPECT_CALL(
        callback,
        Run(std::vector<uint8_t>(), false, mojom::ProviderError::kInternalError,
            l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR)));
    json_rpc_service_->EnsGetContentHash(ens_host(), callback.Get());
    base::RunLoop().RunUntilIdle();
  }
}

}  // namespace brave_wallet
