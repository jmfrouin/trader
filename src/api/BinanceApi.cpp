//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#include "BinanceApi.h"

#include <openssl/hmac.h>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <iostream>
#include <thread>

namespace API {

CBinanceAPI::CBinanceAPI() : mCurl(nullptr) {}

CBinanceAPI::CBinanceAPI(const std::string& apiKey, const std::string& apiSecret)
    : mApiKey(apiKey), mApiSecret(apiSecret), mCurl(nullptr) {}

CBinanceAPI::~CBinanceAPI() {
    // Clean up WebSocket connections
    std::lock_guard<std::mutex> guard(mWsMutex);
    mWsConnections.clear();

    // Clean up CURL
    if (mCurl) {
        curl_easy_cleanup(mCurl);
    }
}

CBinanceAPI::SWebSocketConnection::~SWebSocketConnection() {
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

bool CBinanceAPI::Initialize() {
    mCurl = curl_easy_init();
    if (!mCurl) {
        std::cerr << "Failed to initialize CURL" << std::endl;
        return false;
    }

    // Set common CURL options
    curl_easy_setopt(mCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(mCurl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(mCurl, CURLOPT_SSL_VERIFYHOST, 2L);

    // Test connection by getting exchange info
    try {
        std::string response = SendRequest("/api/v3/exchangeInfo");
        nlohmann::json exchangeInfo = nlohmann::json::parse(response);

        if (exchangeInfo.contains("symbols")) {
            mInitialized = true;
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error initializing Binance API: " << e.what() << std::endl;
    }

    return false;
}

bool CBinanceAPI::IsInitialized() const {
    return mInitialized;
}

void CBinanceAPI::SetCredentials(const std::string& apiKey, const std::string& apiSecret) {
    mApiKey = apiKey;
    mApiSecret = apiSecret;
}

std::string CBinanceAPI::SendRequest(const std::string& endpoint, const std::string& params, bool isPrivate, const std::string& method, const std::string& data) {
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

    std::string url = std::string(API_BASE) + endpoint;
    std::string queryString = params;

    // For private API calls, add timestamp and signature
    if (isPrivate) {
        if (!queryString.empty()) {
            queryString += "&";
        }
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        queryString += "timestamp=" + std::to_string(timestamp);
        queryString += "&signature=" + GenerateSignature(queryString);
    }

    if (!queryString.empty() && method != "POST") {
        url += "?" + queryString;
    }

    // Setup CURL
    curl_easy_setopt(mCurl, CURLOPT_URL, url.c_str());

    // Setup headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (isPrivate) {
        std::string authHeader = "X-MBX-APIKEY: " + mApiKey;
        headers = curl_slist_append(headers, authHeader.c_str());
    }

    curl_easy_setopt(mCurl, CURLOPT_HTTPHEADER, headers);

    // Setup request method
    if (method == "POST") {
        curl_easy_setopt(mCurl, CURLOPT_POST, 1L);
        if (!data.empty()) {
            curl_easy_setopt(mCurl, CURLOPT_POSTFIELDS, data.c_str());
        } else if (!queryString.empty()) {
            curl_easy_setopt(mCurl, CURLOPT_POSTFIELDS, queryString.c_str());
        }
    } else if (method == "DELETE") {
        curl_easy_setopt(mCurl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else {
        curl_easy_setopt(mCurl, CURLOPT_HTTPGET, 1L);
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

size_t CBinanceAPI::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string CBinanceAPI::GenerateSignature(const std::string& queryString) {
    unsigned char* digest = HMAC(EVP_sha256(), mApiSecret.c_str(), mApiSecret.length(),
                                 (unsigned char*)queryString.c_str(), queryString.length(),
                                 nullptr, nullptr);

    std::stringstream ss;
    for (int i = 0; i < 32; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    }

    return ss.str();
}

STicker CBinanceAPI::GetTicker(const std::string& symbol) {
    std::string response = SendRequest("/api/v3/ticker/24hr", "symbol=" + symbol);
    nlohmann::json j = nlohmann::json::parse(response);

    STicker ticker;
    ticker.mSymbol = j["symbol"];
    ticker.mLastPrice = std::stod(j["lastPrice"].get<std::string>());
    ticker.mBidPrice = std::stod(j["bidPrice"].get<std::string>());
    ticker.mAskPrice = std::stod(j["askPrice"].get<std::string>());
    ticker.mVolume24h = std::stod(j["volume"].get<std::string>());
    ticker.mPriceChange24h = std::stod(j["priceChange"].get<std::string>());
    ticker.mPriceChangePercent24h = std::stod(j["priceChangePercent"].get<std::string>());
    ticker.mTimestamp = std::stoll(j["closeTime"].get<std::string>());

    return ticker;
}

SOrderBook CBinanceAPI::GetOrderBook(const std::string& symbol, int limit) {
    std::string params = "symbol=" + symbol + "&limit=" + std::to_string(limit);
    std::string response = SendRequest("/api/v3/depth", params);
    nlohmann::json j = nlohmann::json::parse(response);

    SOrderBook orderBook;
    orderBook.mTimestamp = j["lastUpdateId"].get<long long>();

    // Parse bids
    for (const auto& bid : j["bids"]) {
        SOrderBookEntry entry;
        entry.mPrice = std::stod(bid[0].get<std::string>());
        entry.mQuantity = std::stod(bid[1].get<std::string>());
        orderBook.mBids.push_back(entry);
    }

    // Parse asks
    for (const auto& ask : j["asks"]) {
        SOrderBookEntry entry;
        entry.mPrice = std::stod(ask[0].get<std::string>());
        entry.mQuantity = std::stod(ask[1].get<std::string>());
        orderBook.mAsks.push_back(entry);
    }

    return orderBook;
}

std::vector<STradeInfo> CBinanceAPI::GetRecentTrades(const std::string& symbol, int limit) {
    std::string params = "symbol=" + symbol + "&limit=" + std::to_string(limit);
    std::string response = SendRequest("/api/v3/trades", params);
    nlohmann::json j = nlohmann::json::parse(response);

    std::vector<STradeInfo> trades;
    for (const auto& trade : j) {
        STradeInfo info;
        info.mSymbol = symbol;
        info.mId = trade["id"].get<std::string>();
        info.mPrice = std::stod(trade["price"].get<std::string>());
        info.mQuantity = std::stod(trade["qty"].get<std::string>());
        info.mIsBuyerMaker = trade["isBuyerMaker"].get<bool>();
        info.mTimestamp = trade["time"].get<long long>();
        trades.push_back(info);
    }

    return trades;
}

std::vector<SKline> CBinanceAPI::GetKlines(const std::string& symbol, const std::string& interval, int limit, long long startTime, long long endTime) {
    std::string params = "symbol=" + symbol + "&interval=" + interval + "&limit=" + std::to_string(limit);

    if (startTime > 0) {
        params += "&startTime=" + std::to_string(startTime);
    }

    if (endTime > 0) {
        params += "&endTime=" + std::to_string(endTime);
    }

    std::string response = SendRequest("/api/v3/klines", params);
    nlohmann::json j = nlohmann::json::parse(response);

    std::vector<SKline> klines;
    for (const auto& kline : j) {
        SKline k;
        k.mOpenTime = kline[0].get<long long>();
        k.mOpen = std::stod(kline[1].get<std::string>());
        k.mHigh = std::stod(kline[2].get<std::string>());
        k.mLow = std::stod(kline[3].get<std::string>());
        k.mClose = std::stod(kline[4].get<std::string>());
        k.mVolume = std::stod(kline[5].get<std::string>());
        k.mCloseTime = kline[6].get<long long>();
        klines.push_back(k);
    }

    return klines;
}

SOrderResponse CBinanceAPI::PlaceOrder(const SOrderRequest& request) {
    if (!IsInitialized() || mApiKey.empty() || mApiSecret.empty()) {
        throw std::runtime_error("API not initialized or credentials not set");
    }

    std::string params = "symbol=" + request.mSymbol;
    params += "&side=" + std::string(request.mSide == EOrderSide::BUY ? "BUY" : "SELL");
    params += "&type=" + std::string(request.mType == EOrderType::MARKET ? "MARKET" : "LIMIT");

    // Add type-specific parameters
    if (request.mType == EOrderType::MARKET) {
        params += "&quantity=" + std::to_string(request.mQuantity);
    } else { // LIMIT order
        params += "&quantity=" + std::to_string(request.mQuantity);
        params += "&price=" + std::to_string(request.mPrice);
        params += "&timeInForce=GTC"; // Good Till Cancelled
    }

    std::string response = SendRequest("/api/v3/order", params, true, "POST");
    nlohmann::json j = nlohmann::json::parse(response);

    SOrderResponse orderResponse;
    orderResponse.mOrderId = j["orderId"].get<std::string>();
    orderResponse.mSymbol = j["symbol"];
    orderResponse.mSide = j["side"] == "BUY" ? EOrderSide::BUY : EOrderSide::SELL;
    orderResponse.mType = j["type"] == "MARKET" ? EOrderType::MARKET : EOrderType::LIMIT;
    orderResponse.mPrice = j.contains("price") ? std::stod(j["price"].get<std::string>()) : 0;
    orderResponse.mOrigQty = std::stod(j["origQty"].get<std::string>());
    orderResponse.mExecutedQty = std::stod(j["executedQty"].get<std::string>());
    orderResponse.mStatus = j["status"];
    orderResponse.mTransactTime = j["transactTime"].get<long long>();

    return orderResponse;
}

bool CBinanceAPI::CancelOrder(const std::string& symbol, const std::string& orderId) {
    if (!IsInitialized() || mApiKey.empty() || mApiSecret.empty()) {
        throw std::runtime_error("API not initialized or credentials not set");
    }

    std::string params = "symbol=" + symbol + "&orderId=" + orderId;

    try {
        std::string response = SendRequest("/api/v3/order", params, true, "DELETE");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error cancelling order: " << e.what() << std::endl;
        return false;
    }
}

SOrderResponse CBinanceAPI::GetOrderStatus(const std::string& symbol, const std::string& orderId) {
    if (!IsInitialized() || mApiKey.empty() || mApiSecret.empty()) {
        throw std::runtime_error("API not initialized or credentials not set");
    }

    std::string params = "symbol=" + symbol + "&orderId=" + orderId;
    std::string response = SendRequest("/api/v3/order", params, true);
    nlohmann::json j = nlohmann::json::parse(response);

    SOrderResponse orderResponse;
    orderResponse.mOrderId = j["orderId"].get<std::string>();
    orderResponse.mSymbol = j["symbol"];
    orderResponse.mSide = j["side"] == "BUY" ? EOrderSide::BUY : EOrderSide::SELL;
    orderResponse.mType = j["type"] == "MARKET" ? EOrderType::MARKET : EOrderType::LIMIT;
    orderResponse.mPrice = std::stod(j["price"].get<std::string>());
    orderResponse.mOrigQty = std::stod(j["origQty"].get<std::string>());
    orderResponse.mExecutedQty = std::stod(j["executedQty"].get<std::string>());
    orderResponse.mStatus = j["status"];
    orderResponse.mTransactTime = j["time"].get<long long>();

    return orderResponse;
}

std::vector<SOrderResponse> CBinanceAPI::GetOpenOrders(const std::string& symbol) {
    if (!IsInitialized() || mApiKey.empty() || mApiSecret.empty()) {
        throw std::runtime_error("API not initialized or credentials not set");
    }

    std::string params = symbol.empty() ? "" : "symbol=" + symbol;
    std::string response = SendRequest("/api/v3/openOrders", params, true);
    nlohmann::json j = nlohmann::json::parse(response);

    std::vector<SOrderResponse> orders;
    for (const auto& order : j) {
        SOrderResponse orderResponse;
        orderResponse.mOrderId = order["orderId"].get<std::string>();
        orderResponse.mSymbol = order["symbol"];
        orderResponse.mSide = order["side"] == "BUY" ? EOrderSide::BUY : EOrderSide::SELL;
        orderResponse.mType = order["type"] == "MARKET" ? EOrderType::MARKET : EOrderType::LIMIT;
        orderResponse.mPrice = std::stod(order["price"].get<std::string>());
        orderResponse.mOrigQty = std::stod(order["origQty"].get<std::string>());
        orderResponse.mExecutedQty = std::stod(order["executedQty"].get<std::string>());
        orderResponse.mStatus = order["status"];
        orderResponse.mTransactTime = order["time"].get<long long>();
        orders.push_back(orderResponse);
    }

    return orders;
}

double CBinanceAPI::GetAccountBalance(const std::string& asset) {
    if (!IsInitialized() || mApiKey.empty() || mApiSecret.empty()) {
        throw std::runtime_error("API not initialized or credentials not set");
    }

    std::string response = SendRequest("/api/v3/account", "", true);
    nlohmann::json j = nlohmann::json::parse(response);

    for (const auto& balance : j["balances"]) {
        if (balance["asset"] == asset) {
            return std::stod(balance["free"].get<std::string>());
        }
    }

    return 0.0;
}

bool CBinanceAPI::ConnectWebSocket(const std::string& streamName, const std::function<void(const std::string&)>& messageHandler) {
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
            conn->mWs = std::make_unique<boost::beast::websocket::stream<boost::beast::tcp_stream>>(ioc);

            // Resolve the domain
            auto const results = conn->mResolver->resolve("stream.binance.com", "9443");

            // Connect to the IP address
            boost::beast::get_lowest_layer(*conn->mWs).connect(results);

            // Set the SNI hostname
            if(!SSL_set_tlsext_host_name(conn->mWs->next_layer().native_handle(), "stream.binance.com")) {
                throw boost::beast::system_error{
                    static_cast<int>(::ERR_get_error()),
                    boost::asio::error::get_ssl_category()
                };
            }

            // Handshake with the server
            conn->mWs->handshake("stream.binance.com", "/ws/" + streamName);

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

void CBinanceAPI::DisconnectWebSocket(const std::string& streamName) {
    std::lock_guard<std::mutex> guard(mWsMutex);

    auto it = mWsConnections.find(streamName);
    if (it != mWsConnections.end()) {
        it->second->mRunning = false;
        mWsConnections.erase(it);
    }
}

bool CBinanceAPI::SubscribeOrderBook(const std::string& symbol, FOrderBookCallback callback) {
    std::string streamName = symbol.c_str() + std::string("@depth");

    return ConnectWebSocket(streamName, [callback](const std::string& message) {
        try {
            nlohmann::json j = nlohmann::json::parse(message);

            SOrderBook orderBook;
            orderBook.mTimestamp = j["lastUpdateId"].get<long long>();

            // Parse bids
            for (const auto& bid : j["bids"]) {
                SOrderBookEntry entry;
                entry.mPrice = std::stod(bid[0].get<std::string>());
                entry.mQuantity = std::stod(bid[1].get<std::string>());
                orderBook.mBids.push_back(entry);
            }

            // Parse asks
            for (const auto& ask : j["asks"]) {
                SOrderBookEntry entry;
                entry.mPrice = std::stod(ask[0].get<std::string>());
                entry.mQuantity = std::stod(ask[1].get<std::string>());
                orderBook.mAsks.push_back(entry);
            }

            callback(orderBook);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing order book message: " << e.what() << std::endl;
        }
    });
}

bool CBinanceAPI::SubscribeTicker(const std::string& symbol, FTickerCallback callback) {
    std::string streamName = symbol.c_str() + std::string("@ticker");

    return ConnectWebSocket(streamName, [callback, symbol](const std::string& message) {
        try {
            nlohmann::json j = nlohmann::json::parse(message);

            STicker ticker;
            ticker.mSymbol = symbol;
            ticker.mLastPrice = std::stod(j["c"].get<std::string>());
            ticker.mBidPrice = std::stod(j["b"].get<std::string>());
            ticker.mAskPrice = std::stod(j["a"].get<std::string>());
            ticker.mVolume24h = std::stod(j["v"].get<std::string>());
            ticker.mPriceChange24h = std::stod(j["p"].get<std::string>());
            ticker.mPriceChangePercent24h = std::stod(j["P"].get<std::string>());
            ticker.mTimestamp = j["E"].get<long long>();

            callback(ticker);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing ticker message: " << e.what() << std::endl;
        }
    });
}

bool CBinanceAPI::SubscribeTrades(const std::string& symbol, FTradeCallback callback) {
    std::string streamName = symbol.c_str() + std::string("@trade");

    return ConnectWebSocket(streamName, [callback, symbol](const std::string& message) {
        try {
            nlohmann::json j = nlohmann::json::parse(message);

            STradeInfo trade;
            trade.mSymbol = symbol;
            trade.mId = j["t"].get<std::string>();
            trade.mPrice = std::stod(j["p"].get<std::string>());
            trade.mQuantity = std::stod(j["q"].get<std::string>());
            trade.mIsBuyerMaker = j["m"].get<bool>();
            trade.mTimestamp = j["T"].get<long long>();

            callback(trade);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing trade message: " << e.what() << std::endl;
        }
    });
}

bool CBinanceAPI::SubscribeKlines(const std::string& symbol, const std::string& interval, FKlineCallback callback) {
    std::string streamName = symbol.c_str() + std::string("@kline_") + interval;

    return ConnectWebSocket(streamName, [callback](const std::string& message) {
        try {
            nlohmann::json j = nlohmann::json::parse(message);
            auto k = j["k"];

            SKline kline;
            kline.mOpenTime = k["t"].get<long long>();
            kline.mOpen = std::stod(k["o"].get<std::string>());
            kline.mHigh = std::stod(k["h"].get<std::string>());
            kline.mLow = std::stod(k["l"].get<std::string>());
            kline.mClose = std::stod(k["c"].get<std::string>());
            kline.mVolume = std::stod(k["v"].get<std::string>());
            kline.mCloseTime = k["T"].get<long long>();

            callback(kline);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing kline message: " << e.what() << std::endl;
        }
    });
}

bool CBinanceAPI::Unsubscribe(const std::string& symbol, const std::string& streamType) {
    std::string streamName;

    if (streamType == "orderbook") {
        streamName = symbol.c_str() + std::string("@depth");
    } else if (streamType == "ticker") {
        streamName = symbol.c_str() + std::string("@ticker");
    } else if (streamType == "trades") {
        streamName = symbol.c_str() + std::string("@trade");
    } else if (streamType.find("kline_") == 0) {
        streamName = symbol.c_str() + std::string("@") + streamType;
    } else {
        return false;
    }

    DisconnectWebSocket(streamName);
    return true;
}

std::vector<std::string> CBinanceAPI::GetAvailablePairs() {
    std::string response = SendRequest("/api/v3/exchangeInfo");
    nlohmann::json j = nlohmann::json::parse(response);

    std::vector<std::string> pairs;
    for (const auto& symbol : j["symbols"]) {
        if (symbol["status"] == "TRADING") {
            pairs.push_back(symbol["symbol"]);
        }
    }

    return pairs;
}

bool CBinanceAPI::IsValidPair(const std::string& symbol) {
    std::string response = SendRequest("/api/v3/exchangeInfo");
    nlohmann::json j = nlohmann::json::parse(response);

    for (const auto& s : j["symbols"]) {
        if (s["symbol"] == symbol && s["status"] == "TRADING") {
            return true;
        }
    }

    return false;
}

} // namespace API