//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#ifndef BINANCEAPI_H
#define BINANCEAPI_H

#include "IExchangeAPI.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>

namespace API {

class CBinanceAPI : public IExchangeAPI {
public:
    CBinanceAPI();
    CBinanceAPI(const std::string& apiKey, const std::string& apiSecret);
    ~CBinanceAPI() override;

    // IExchangeAPI implementation
    bool Initialize() override;
    bool IsInitialized() const override;

    // Market data - REST
    STicker GetTicker(const std::string& symbol) override;
    SOrderBook GetOrderBook(const std::string& symbol, int limit = 100) override;
    std::vector<STradeInfo> GetRecentTrades(const std::string& symbol, int limit = 100) override;
    std::vector<SKline> GetKlines(const std::string& symbol, const std::string& interval, int limit = 500, long long startTime = 0, long long endTime = 0) override;

    // Trading - REST
    SOrderResponse PlaceOrder(const SOrderRequest& request) override;
    bool CancelOrder(const std::string& symbol, const std::string& orderId) override;
    SOrderResponse GetOrderStatus(const std::string& symbol, const std::string& orderId) override;
    std::vector<SOrderResponse> GetOpenOrders(const std::string& symbol = "") override;
    double GetAccountBalance(const std::string& asset) override;

    // WebSocket streams
    bool SubscribeOrderBook(const std::string& symbol, OrderBookCallback callback) override;
    bool SubscribeTicker(const std::string& symbol, TickerCallback callback) override;
    bool SubscribeTrades(const std::string& symbol, TradeCallback callback) override;
    bool SubscribeKlines(const std::string& symbol, const std::string& interval, KlineCallback callback) override;
    bool Unsubscribe(const std::string& symbol, const std::string& streamType) override;

    // Utility
    std::string GetExchangeName() const override { return "Binance"; }
    std::vector<std::string> GetAvailablePairs() override;
    bool IsValidPair(const std::string& symbol) override;

    // Set credentials
    void SetCredentials(const std::string& apiKey, const std::string& apiSecret);

private:
    // REST API helpers
    std::string SendRequest(const std::string& endpoint, const std::string& params = "", bool isPrivate = false, const std::string& method = "GET", const std::string& data = "");
    std::string GenerateSignature(const std::string& queryString);
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);

    // WebSocket helpers
    struct SWebSocketConnection {
        std::unique_ptr<boost::asio::ip::tcp::resolver> mResolver;
        std::unique_ptr<boost::beast::websocket::stream<boost::beast::tcp_stream>> mWebSocket;
        std::thread mWorker;
        std::atomic<bool> mRunning{false};

        SWebSocketConnection() = default;
        ~SWebSocketConnection();
        SWebSocketConnection(const SWebSocketConnection&) = delete;
        SWebSocketConnection& operator=(const SWebSocketConnection&) = delete;
    };

    std::map<std::string, std::unique_ptr<SWebSocketConnection>> mWebSocketConnections;
    std::mutex mWebSocketMutex;
    std::mutex mRequestMutex;

    bool ConnectWebSocket(const std::string& streamName, const std::function<void(const std::string&)>& messageHandler);
    void DisconnectWebSocket(const std::string& streamName);
    void HandleWebSocketMessages(SWebSocketConnection* conn, const std::function<void(const std::string&)>& messageHandler);

    // API endpoints
    static constexpr const char* API_BASE = "https://api.binance.com";
    static constexpr const char* API_VERSION = "/api/v3";
    static constexpr const char* WS_BASE = "wss://stream.binance.com:9443/ws/";

    // Credentials
    std::string mApiKey;
    std::string mApiSecret;
    std::atomic<bool> mInitialized{false};
    CURL* mCurl;

    // Rate limiting
    std::mutex mRateLimitMutex;
    std::chrono::steady_clock::time_point mLastRequestTime;
    int mRequestsPerMinute = 0;
    const int MAX_REQUESTS_PER_MINUTE = 1200; // Default limit
};

} // namespace API

#endif //BINANCEAPI_H