
//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#include "KrakenApi.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <iostream>
#include <thread>
#include <random>

namespace API {

CKrakenAPI::CKrakenAPI() : mCurl(nullptr) {}

CKrakenAPI::CKrakenAPI(const std::string& apiKey, const std::string& apiSecret)
    : mApiKey(apiKey), mApiSecret(apiSecret), mCurl(nullptr) {}

CKrakenAPI::~CKrakenAPI() {
    // Clean up WebSocket connections
    std::lock_guard<std::mutex> guard(mWsMutex);
    mWsConnections.clear();

    // Clean up CURL
    if (mCurl) {
        curl_easy_cleanup(mCurl);
    }
}

CKrakenAPI::SWebSocketConnection::~SWebSocketConnection() {
    if (mRunning) {
        mRunning = false;
        if (mWs && mWs->is_open()) {
            boost::beast::error_code ec;
            mWs->close(boost::beast::websocket::close_code::normal, ec);
        }
        if (mWorker.joinable()) {
            mWorker.join();
        }
    }
}

bool CKrakenAPI::Initialize() {
    mCurl = curl_easy_init();
    if (!mCurl) {
        std::cerr << "Failed to initialize CURL" << std::endl;
        return false;
    }

    // Set common CURL options
    curl_easy_setopt(mCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(mCurl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(mCurl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(mCurl, CURLOPT_USERAGENT, "Trader/1.0");

    // Load asset pairs
    LoadAssetPairs();

    // Test connection by getting server time
    try {
        std::string response = SendRequest("/public/Time");
        nlohmann::json j = nlohmann::json::parse(response);

        if (j.contains("result") && j["result"].contains("unixtime")) {
            mInitialized = true;
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error initializing Kraken API: " << e.what() << std::endl;
    }

    return false;
}

bool CKrakenAPI::IsInitialized() const {
    return mInitialized;
}

void CKrakenAPI::SetCredentials(const std::string& apiKey, const std::string& apiSecret) {
    mApiKey = apiKey;
    mApiSecret = apiSecret;
}

std::string CKrakenAPI::SendRequest(const std::string& endpoint, const std::string& params, bool isPrivate, const std::string& method, const std::string& data) {
    if (!mCurl) {
        throw std::runtime_error("CURL not initialized");
    }

    // Rate limiting
    {
        std::lock_guard<std::mutex> guard(mRateLimitMutex);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - mLastRequestTime);

        if (elapsed.count() < 1) {
            if (mRequestsPerMinute >= MAX_REQUESTS_PER_MINUTE) {
                // Wait until next minute
                auto sleepTime = std::chrono::minutes(1) - (now - mLastRequestTime);
                std::this_thread::sleep_for(sleepTime);
                mRequestsPerMinute = 0;
                mLastRequestTime = std::chrono::steady_clock::now();
            }
        } else {
            mRequestsPerMinute = 0;
            mLastRequestTime = now;
        }

        mRequestsPerMinute++;
    }

    std::lock_guard<std::mutex> guard(mRequestMutex);

    std::string url = std::string(API_BASE) + API_VERSION + endpoint;
    std::string postData = params;

    // Setup CURL
    curl_easy_setopt(mCurl, CURLOPT_URL, url.c_str());

    // Setup headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    if (isPrivate) {
        std::string nonce = GenerateNonce();
        if (!postData.empty()) {
            postData += "&";
        }
        postData += "nonce=" + nonce;

        std::string signature = GenerateSignature(API_VERSION + endpoint, postData, nonce);
        std::string authHeader = "API-Key: " + mApiKey;
        std::string signHeader = "API-Sign: " + signature;

        headers = curl_slist_append(headers, authHeader.c_str());
        headers = curl_slist_append(headers, signHeader.c_str());
    }

    curl_easy_setopt(mCurl, CURLOPT_HTTPHEADER, headers);

    // Setup request method
    if (method == "POST" || isPrivate) {
        curl_easy_setopt(mCurl, CURLOPT_POST, 1L);
        curl_easy_setopt(mCurl, CURLOPT_POSTFIELDS, postData.c_str());
    } else {
        curl_easy_setopt(mCurl, CURLOPT_HTTPGET, 1L);
        if (!postData.empty()) {
            url += "?" + postData;
            curl_easy_setopt(mCurl, CURLOPT_URL, url.c_str());
        }
    }

    // Response handling
    std::string response_string;
    curl_easy_setopt(mCurl, CURLOPT_WRITEDATA, &response_string);

    // Perform request
    CURLcode res = curl_easy_perform(mCurl);

    // Clean up
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        throw std::runtime_error("CURL request failed: " + std::string(curl_easy_strerror(res)));
    }

    long http_code = 0;
    curl_easy_getinfo(mCurl, CURLINFO_RESPONSE_CODE, &http_code);

    if (http_code >= 400) {
        throw std::runtime_error("HTTP error " + std::to_string(http_code) + ": " + response_string);
    }

    return response_string;
}

size_t CKrakenAPI::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string CKrakenAPI::GenerateNonce() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(timestamp);
}

std::string CKrakenAPI::GenerateSignature(const std::string& uri, const std::string& postData, const std::string& nonce) {
    std::string message = nonce + postData;
    std::string hash = Sha256(message);
    std::string data = uri + hash;

    std::string decodedSecret = Base64Decode(mApiSecret);
    std::string signature = HmacSha512(decodedSecret, data);

    return Base64Encode(signature);
}

std::string CKrakenAPI::Base64Encode(const std::string& data) {
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);

    BIO_write(bio, data.c_str(), data.length());
    BIO_flush(bio);

    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string result(bufferPtr->data, bufferPtr->length);

    BIO_free_all(bio);
    return result;
}

std::string CKrakenAPI::Base64Decode(const std::string& data) {
    BIO* bio = BIO_new_mem_buf(data.c_str(), data.length());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);

    std::string result(data.length(), '\0');
    int length = BIO_read(bio, &result[0], data.length());
    result.resize(length);

    BIO_free_all(bio);
    return result;
}

std::string CKrakenAPI::HmacSha512(const std::string& key, const std::string& data) {
    unsigned char* digest = HMAC(EVP_sha512(), key.c_str(), key.length(),
                                 (unsigned char*)data.c_str(), data.length(),
                                 nullptr, nullptr);
    return std::string((char*)digest, 64);
}

std::string CKrakenAPI::Sha256(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.c_str(), data.length());
    SHA256_Final(hash, &sha256);

    return std::string((char*)hash, SHA256_DIGEST_LENGTH);
}

void CKrakenAPI::LoadAssetPairs() {
    try {
        std::string response = SendRequest("/public/AssetPairs");
        nlohmann::json j = nlohmann::json::parse(response);

        if (j.contains("result")) {
            std::lock_guard<std::mutex> guard(mAssetPairsMutex);
            for (const auto& pair : j["result"].items()) {
                mAssetPairs[pair.key()] = pair.value()["wsname"];
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading asset pairs: " << e.what() << std::endl;
    }
}

std::string CKrakenAPI::ConvertToKrakenSymbol(const std::string& symbol) {
    std::lock_guard<std::mutex> guard(mAssetPairsMutex);

    // Direct lookup first
    if (mAssetPairs.find(symbol) != mAssetPairs.end()) {
        return symbol;
    }

    // Common conversions
    std::string krakenSymbol = symbol;

    // BTC conversions
    if (symbol == "BTCUSDT") return "XBTUSD";
    if (symbol == "BTCEUR") return "XBTEUR";
    if (symbol == "BTCGBP") return "XBTGBP";

    // ETH conversions
    if (symbol == "ETHUSDT") return "ETHUSD";
    if (symbol == "ETHEUR") return "ETHEUR";
    if (symbol == "ETHGBP") return "ETHGBP";

    // Add more conversions as needed
    return krakenSymbol;
}

std::string CKrakenAPI::ConvertFromKrakenSymbol(const std::string& krakenSymbol) {
    // Reverse conversions
    if (krakenSymbol == "XBTUSD") return "BTCUSDT";
    if (krakenSymbol == "XBTEUR") return "BTCEUR";
    if (krakenSymbol == "XBTGBP") return "BTCGBP";
    if (krakenSymbol == "ETHUSD") return "ETHUSDT";
    if (krakenSymbol == "ETHEUR") return "ETHEUR";
    if (krakenSymbol == "ETHGBP") return "ETHGBP";

    return krakenSymbol;
}

std::string CKrakenAPI::ConvertToKrakenInterval(const std::string& interval) {
    // Convert standard intervals to Kraken format
    if (interval == "1m") return "1";
    if (interval == "5m") return "5";
    if (interval == "15m") return "15";
    if (interval == "30m") return "30";
    if (interval == "1h") return "60";
    if (interval == "4h") return "240";
    if (interval == "1d") return "1440";
    if (interval == "1w") return "10080";

    return interval;
}

STicker CKrakenAPI::GetTicker(const std::string& symbol) {
    std::string krakenSymbol = ConvertToKrakenSymbol(symbol);
    std::string response = SendRequest("/public/Ticker", "pair=" + krakenSymbol);
    nlohmann::json j = nlohmann::json::parse(response);

    if (!j.contains("result") || j["result"].empty()) {
        throw std::runtime_error("Invalid ticker response");
    }

    auto tickerData = j["result"].begin().value();

    STicker ticker;
    ticker.mSymbol = symbol;
    ticker.mLastPrice = std::stod(tickerData["c"][0].get<std::string>());
    ticker.mBidPrice = std::stod(tickerData["b"][0].get<std::string>());
    ticker.mAskPrice = std::stod(tickerData["a"][0].get<std::string>());
    ticker.mVolume24h = std::stod(tickerData["v"][1].get<std::string>());
    ticker.mPriceChange24h = std::stod(tickerData["p"][1].get<std::string>());
    ticker.mPriceChangePercent24h = ticker.mPriceChange24h / ticker.mLastPrice * 100.0;
    ticker.mTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    return ticker;
}

SOrderBook CKrakenAPI::GetOrderBook(const std::string& symbol, int limit) {
    std::string krakenSymbol = ConvertToKrakenSymbol(symbol);
    std::string params = "pair=" + krakenSymbol + "&count=" + std::to_string(limit);
    std::string response = SendRequest("/public/Depth", params);
    nlohmann::json j = nlohmann::json::parse(response);

    if (!j.contains("result") || j["result"].empty()) {
        throw std::runtime_error("Invalid order book response");
    }

    auto orderBookData = j["result"].begin().value();

    SOrderBook orderBook;
    orderBook.mTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Parse bids
    for (const auto& bid : orderBookData["bids"]) {
        SOrderBookEntry entry;
        entry.mPrice = std::stod(bid[0].get<std::string>());
        entry.mQuantity = std::stod(bid[1].get<std::string>());
        orderBook.mBids.push_back(entry);
    }

    // Parse asks
    for (const auto& ask : orderBookData["asks"]) {
        SOrderBookEntry entry;
        entry.mPrice = std::stod(ask[0].get<std::string>());
        entry.mQuantity = std::stod(ask[1].get<std::string>());
        orderBook.mAsks.push_back(entry);
    }

    return orderBook;
}

std::vector<STradeInfo> CKrakenAPI::GetRecentTrades(const std::string& symbol, int limit) {
    std::string krakenSymbol = ConvertToKrakenSymbol(symbol);
    std::string params = "pair=" + krakenSymbol + "&count=" + std::to_string(limit);
    std::string response = SendRequest("/public/Trades", params);
    nlohmann::json j = nlohmann::json::parse(response);

    if (!j.contains("result") || j["result"].empty()) {
        throw std::runtime_error("Invalid trades response");
    }

    auto tradesData = j["result"].begin().value();

    std::vector<STradeInfo> trades;
    for (const auto& trade : tradesData) {
        STradeInfo info;
        info.mSymbol = symbol;
        info.mId = std::to_string(trades.size()); // Kraken doesn't provide trade IDs
        info.mPrice = std::stod(trade[0].get<std::string>());
        info.mQuantity = std::stod(trade[1].get<std::string>());
        info.mTimestamp = static_cast<long long>(std::stod(trade[2].get<std::string>()) * 1000);
        info.mIsBuyerMaker = trade[3].get<std::string>() == "s"; // 's' for sell (maker), 'b' for buy (maker)
        trades.push_back(info);
    }

    return trades;
}

std::vector<SKline> CKrakenAPI::GetKlines(const std::string& symbol, const std::string& interval, int limit, long long startTime, long long endTime) {
    std::string krakenSymbol = ConvertToKrakenSymbol(symbol);
    std::string krakenInterval = ConvertToKrakenInterval(interval);

    std::string params = "pair=" + krakenSymbol + "&interval=" + krakenInterval;

    if (startTime > 0) {
        params += "&since=" + std::to_string(startTime / 1000);
    }

    std::string response = SendRequest("/public/OHLC", params);
    nlohmann::json j = nlohmann::json::parse(response);

    if (!j.contains("result") || j["result"].empty()) {
        throw std::runtime_error("Invalid klines response");
    }

    auto klineData = j["result"].begin().value();

    std::vector<SKline> klines;
    int count = 0;
    for (const auto& kline : klineData) {
        if (count >= limit) break;

        SKline k;
        k.mOpenTime = static_cast<long long>(std::stod(kline[0].get<std::string>()) * 1000);
        k.mOpen = std::stod(kline[1].get<std::string>());
        k.mHigh = std::stod(kline[2].get<std::string>());
        k.mLow = std::stod(kline[3].get<std::string>());
        k.mClose = std::stod(kline[4].get<std::string>());
        k.mVolume = std::stod(kline[6].get<std::string>());
        k.mCloseTime = k.mOpenTime + (std::stoi(krakenInterval) * 60 * 1000);

        klines.push_back(k);
        count++;
    }

    return klines;
}

SOrderResponse CKrakenAPI::PlaceOrder(const SOrderRequest& request) {
    if (!IsInitialized() || mApiKey.empty() || mApiSecret.empty()) {
        throw std::runtime_error("API not initialized or credentials not set");
    }

    std::string krakenSymbol = ConvertToKrakenSymbol(request.mSymbol);
    std::string params = "pair=" + krakenSymbol;
    params += "&type=" + std::string(request.mSide == EOrderSide::BUY ? "buy" : "sell");
    params += "&ordertype=" + std::string(request.mType == EOrderType::MARKET ? "market" : "limit");
    params += "&volume=" + std::to_string(request.mQuantity);

    if (request.mType == EOrderType::LIMIT) {
        params += "&price=" + std::to_string(request.mPrice);
    }

    std::string response = SendRequest("/private/AddOrder", params, true, "POST");
    nlohmann::json j = nlohmann::json::parse(response);

    if (!j.contains("result") || j["result"].empty()) {
        throw std::runtime_error("Invalid order response");
    }

    SOrderResponse orderResponse;
    orderResponse.mOrderId = j["result"]["txid"][0].get<std::string>();
    orderResponse.mSymbol = request.mSymbol;
    orderResponse.mSide = request.mSide;
    orderResponse.mType = request.mType;
    orderResponse.mPrice = request.mPrice;
    orderResponse.mOrigQty = request.mQuantity;
    orderResponse.mExecutedQty = 0; // Will be updated when order is filled
    orderResponse.mStatus = "NEW";
    orderResponse.mTransactTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    return orderResponse;
}

bool CKrakenAPI::CancelOrder(const std::string& symbol, const std::string& orderId) {
    if (!IsInitialized() || mApiKey.empty() || mApiSecret.empty()) {
        throw std::runtime_error("API not initialized or credentials not set");
    }

    std::string params = "txid=" + orderId;

    try {
        std::string response = SendRequest("/private/CancelOrder", params, true, "POST");
        nlohmann::json j = nlohmann::json::parse(response);
        return j.contains("result") && j["result"].contains("count") && j["result"]["count"] > 0;
    } catch (const std::exception& e) {
        std::cerr << "Error cancelling order: " << e.what() << std::endl;
        return false;
    }
}

SOrderResponse CKrakenAPI::GetOrderStatus(const std::string& symbol, const std::string& orderId) {
    if (!IsInitialized() || mApiKey.empty() || mApiSecret.empty()) {
        throw std::runtime_error("API not initialized or credentials not set");
    }

    std::string params = "txid=" + orderId;
    std::string response = SendRequest("/private/QueryOrders", params, true, "POST");
    nlohmann::json j = nlohmann::json::parse(response);

    if (!j.contains("result") || j["result"].empty()) {
        throw std::runtime_error("Invalid order status response");
    }

    auto orderData = j["result"].begin().value();

    SOrderResponse orderResponse;
    orderResponse.mOrderId = orderId;
    orderResponse.mSymbol = ConvertFromKrakenSymbol(orderData["descr"]["pair"]);
    orderResponse.mSide = orderData["descr"]["type"] == "buy" ? EOrderSide::BUY : EOrderSide::SELL;
    orderResponse.mType = orderData["descr"]["ordertype"] == "market" ? EOrderType::MARKET : EOrderType::LIMIT;
    orderResponse.mPrice = std::stod(orderData["descr"]["price"].get<std::string>());
    orderResponse.mOrigQty = std::stod(orderData["vol"].get<std::string>());
    orderResponse.mExecutedQty = std::stod(orderData["vol_exec"].get<std::string>());
    orderResponse.mStatus = orderData["status"];
    orderResponse.mTransactTime = static_cast<long long>(std::stod(orderData["opentm"].get<std::string>()) * 1000);

    return orderResponse;
}

std::vector<SOrderResponse> CKrakenAPI::GetOpenOrders(const std::string& symbol) {
    if (!IsInitialized() || mApiKey.empty() || mApiSecret.empty()) {
        throw std::runtime_error("API not initialized or credentials not set");
    }

    std::string response = SendRequest("/private/OpenOrders", "", true, "POST");
    nlohmann::json j = nlohmann::json::parse(response);

    if (!j.contains("result") || !j["result"].contains("open")) {
        throw std::runtime_error("Invalid open orders response");
    }

    std::vector<SOrderResponse> orders;
    for (const auto& order : j["result"]["open"].items()) {
        SOrderResponse orderResponse;
        orderResponse.mOrderId = order.key();
        orderResponse.mSymbol = ConvertFromKrakenSymbol(order.value()["descr"]["pair"]);

        // Filter by symbol if specified
        if (!symbol.empty() && orderResponse.mSymbol != symbol) {
            continue;
        }

        orderResponse.mSide = order.value()["descr"]["type"] == "buy" ? EOrderSide::BUY : EOrderSide::SELL;
        orderResponse.mType = order.value()["descr"]["ordertype"] == "market" ? EOrderType::MARKET : EOrderType::LIMIT;
        orderResponse.mPrice = std::stod(order.value()["descr"]["price"].get<std::string>());
        orderResponse.mOrigQty = std::stod(order.value()["vol"].get<std::string>());
        orderResponse.mExecutedQty = std::stod(order.value()["vol_exec"].get<std::string>());
        orderResponse.mStatus = order.value()["status"];
        orderResponse.mTransactTime = static_cast<long long>(std::stod(order.value()["opentm"].get<std::string>()) * 1000);

        orders.push_back(orderResponse);
    }

    return orders;
}

double CKrakenAPI::GetAccountBalance(const std::string& asset) {
    if (!IsInitialized() || mApiKey.empty() || mApiSecret.empty()) {
        throw std::runtime_error("API not initialized or credentials not set");
    }

    std::string response = SendRequest("/private/Balance", "", true, "POST");
    nlohmann::json j = nlohmann::json::parse(response);

    if (!j.contains("result")) {
        throw std::runtime_error("Invalid balance response");
    }

    // Convert asset name to Kraken format
    std::string krakenAsset = asset;
    if (asset == "BTC") krakenAsset = "XXBT";
    if (asset == "ETH") krakenAsset = "XETH";
    if (asset == "USD") krakenAsset = "ZUSD";
    if (asset == "EUR") krakenAsset = "ZEUR";

    if (j["result"].contains(krakenAsset)) {
        return std::stod(j["result"][krakenAsset].get<std::string>());
    }

    return 0.0;
}

bool CKrakenAPI::ConnectWebSocket(const std::string& streamName, const std::function<void(const std::string&)>& messageHandler) {
    std::lock_guard<std::mutex> guard(mWsMutex);

    if (mWsConnections.find(streamName) != mWsConnections.end()) {
        return true; // Already connected
    }

    auto conn = std::make_unique<SWebSocketConnection>();
    conn->mRunning = true;

    conn->mWorker = std::thread([this, streamName, messageHandler, conn = conn.get()]() {
        try {
            boost::asio::io_context ioc;
            boost::asio::ssl::context ctx{boost::asio::ssl::context::tlsv12_client};

            conn->mResolver = std::make_unique<boost::asio::ip::tcp::resolver>(ioc);
            conn->mWs = std::make_unique<boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>>(ioc, ctx);

            // Resolve the domain
            auto const results = conn->mResolver->resolve("ws.kraken.com", "443");

            // Connect to the IP address
            boost::beast::get_lowest_layer(*conn->mWs).connect(results);

            // SSL handshake
            conn->mWs->next_layer().handshake(boost::asio::ssl::stream_base::client);

            // WebSocket handshake
            conn->mWs->handshake("ws.kraken.com", "/");

            // Subscribe to the stream
            nlohmann::json subscribeMsg = {
                {"event", "subscribe"},
                {"pair", {streamName}},
                {"subscription", {{"name", "ticker"}}}
            };
            conn->mWs->write(boost::asio::buffer(subscribeMsg.dump()));

            // Process messages
            while (conn->mRunning) {
                try {
                    boost::beast::flat_buffer buffer;
                    conn->mWs->read(buffer);
                    std::string msg(boost::beast::buffers_to_string(buffer.data()));
                    messageHandler(msg);
                } catch (const std::exception& e) {
                    if (conn->mRunning) {
                        std::cerr << "WebSocket read error: " << e.what() << std::endl;
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }

            // Gracefully close the WebSocket connection
            conn->mWs->close(boost::beast::websocket::close_code::normal);
        } catch (const std::exception& e) {
            std::cerr << "WebSocket connection error: " << e.what() << std::endl;
        }
    });

    mWsConnections[streamName] = std::move(conn);
    return true;
}

void CKrakenAPI::DisconnectWebSocket(const std::string& streamName) {
    std::lock_guard<std::mutex> guard(mWsMutex);

    auto it = mWsConnections.find(streamName);
    if (it != mWsConnections.end()) {
        it->second->mRunning = false;
        mWsConnections.erase(it);
    }
}

bool CKrakenAPI::SubscribeOrderBook(const std::string& symbol, OrderBookCallback callback) {
    std::string krakenSymbol = ConvertToKrakenSymbol(symbol);

    return ConnectWebSocket(krakenSymbol, [callback](const std::string& message) {
        try {
            nlohmann::json j = nlohmann::json::parse(message);

            // Handle Kraken WebSocket order book format
            if (j.is_array() && j.size() >= 2) {
                SOrderBook orderBook;
                orderBook.mTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                // Parse the order book data (simplified)
                callback(orderBook);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing order book message: " << e.what() << std::endl;
        }
    });
}

bool CKrakenAPI::SubscribeTicker(const std::string& symbol, TickerCallback callback) {
    std::string krakenSymbol = ConvertToKrakenSymbol(symbol);

    return ConnectWebSocket(krakenSymbol, [callback, symbol](const std::string& message) {
        try {
            nlohmann::json j = nlohmann::json::parse(message);

            // Handle Kraken WebSocket ticker format
            if (j.is_array() && j.size() >= 2) {
                STicker ticker;
                ticker.mSymbol = symbol;
                ticker.mTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                // Parse the ticker data (simplified)
                callback(ticker);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing ticker message: " << e.what() << std::endl;
        }
    });
}

bool CKrakenAPI::SubscribeTrades(const std::string& symbol, TradeCallback callback) {
    std::string krakenSymbol = ConvertToKrakenSymbol(symbol);

    return ConnectWebSocket(krakenSymbol, [callback, symbol](const std::string& message) {
        try {
            nlohmann::json j = nlohmann::json::parse(message);

            // Handle Kraken WebSocket trade format
            if (j.is_array() && j.size() >= 2) {
                STradeInfo trade;
                trade.mSymbol = symbol;
                trade.mTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                // Parse the trade data (simplified)
                callback(trade);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing trade message: " << e.what() << std::endl;
        }
    });
}

bool CKrakenAPI::SubscribeKlines(const std::string& symbol, const std::string& interval, KlineCallback callback) {
    std::string krakenSymbol = ConvertToKrakenSymbol(symbol);

    return ConnectWebSocket(krakenSymbol, [callback](const std::string& message) {
        try {
            nlohmann::json j = nlohmann::json::parse(message);

            // Handle Kraken WebSocket kline format
            if (j.is_array() && j.size() >= 2) {
                SKline kline;
                kline.mTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                // Parse the kline data (simplified)
                callback(kline);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing kline message: " << e.what() << std::endl;
        }
    });
}

bool CKrakenAPI::Unsubscribe(const std::string& symbol, const std::string& streamType) {
    std::string krakenSymbol = ConvertToKrakenSymbol(symbol);
    DisconnectWebSocket(krakenSymbol);
    return true;
}

std::vector<std::string> CKrakenAPI::GetAvailablePairs() {
    std::string response = SendRequest("/public/AssetPairs");
    nlohmann::json j = nlohmann::json::parse(response);

    std::vector<std::string> pairs;
    if (j.contains("result")) {
        for (const auto& pair : j["result"].items()) {
            pairs.push_back(ConvertFromKrakenSymbol(pair.key()));
        }
    }

    return pairs;
}

bool CKrakenAPI::IsValidPair(const std::string& symbol) {
    std::string krakenSymbol = ConvertToKrakenSymbol(symbol);
    std::string response = SendRequest("/public/AssetPairs", "pair=" + krakenSymbol);
    nlohmann::json j = nlohmann::json::parse(response);

    return j.contains("result") && !j["result"].empty();
}

} // namespace API