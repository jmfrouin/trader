//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#ifndef IEXCHANGEAPI_H
#define IEXCHANGEAPI_H

#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

namespace API {

// Définition des types pour les données de marché
struct SOrderBookEntry {
    double mPrice;
    double mQuantity;
};

struct SOrderBook {
    std::vector<SOrderBookEntry> mBids;
    std::vector<SOrderBookEntry> mAsks;
    long long mTimestamp;
};

struct STicker {
    std::string mSymbol;
    double mLastPrice;
    double mBidPrice;
    double mAskPrice;
    double mVolume24h;
    double mPriceChange24h;
    double mPriceChangePercent24h;
    long long mTimestamp;
};

struct STradeInfo {
    std::string mSymbol;
    std::string mId;
    double mPrice;
    double mQuantity;
    bool mIsBuyerMaker;
    long long mTimestamp;
};

struct SKline {
    long long mOpenTime;
    double mOpen;
    double mHigh;
    double mLow;
    double mClose;
    double mVolume;
    long long mCloseTime;
};

enum class EOrderType {
    MARKET,
    LIMIT
};

enum class EOrderSide {
    BUY,
    SELL
};

struct SOrderRequest {
    std::string mSymbol;
    EOrderSide mSide;
    EOrderType mType;
    double mQuantity;
    double mPrice;  // Utilisé seulement pour les ordres LIMIT
};

struct SOrderResponse {
    std::string mOrderId;
    std::string mSymbol;
    EOrderSide mSide;
    EOrderType mType;
    double mPrice;
    double mExecutedQty;
    double mOrigQty;
    std::string mStatus;
    long long mTransactTime;
};

using OrderBookCallback = std::function<void(const SOrderBook&)>;
using TickerCallback = std::function<void(const STicker&)>;
using TradeCallback = std::function<void(const STradeInfo&)>;
using KlineCallback = std::function<void(const SKline&)>;

    // Interface for Exchange APIs
    class IExchangeAPI {
    public:
        virtual ~IExchangeAPI() = default;

        // Connection management
        virtual bool Initialize() = 0;
        virtual bool IsInitialized() const = 0;

        // Market data - REST
        virtual STicker GetTicker(const std::string& symbol) = 0;
        virtual SOrderBook GetOrderBook(const std::string& symbol, int limit = 100) = 0;
        virtual std::vector<STradeInfo> GetRecentTrades(const std::string& symbol, int limit = 100) = 0;
        virtual std::vector<SKline> GetKlines(const std::string& symbol, const std::string& interval, int limit = 500, long long startTime = 0, long long endTime = 0) = 0;

        // Trading - REST
        virtual SOrderResponse PlaceOrder(const SOrderRequest& request) = 0;
        virtual bool CancelOrder(const std::string& symbol, const std::string& orderId) = 0;
        virtual SOrderResponse GetOrderStatus(const std::string& symbol, const std::string& orderId) = 0;
        virtual std::vector<SOrderResponse> GetOpenOrders(const std::string& symbol = "") = 0;
        virtual double GetAccountBalance(const std::string& asset) = 0;

        // WebSocket streams
        virtual bool SubscribeOrderBook(const std::string& symbol, OrderBookCallback callback) = 0;
        virtual bool SubscribeTicker(const std::string& symbol, TickerCallback callback) = 0;
        virtual bool SubscribeTrades(const std::string& symbol, TradeCallback callback) = 0;
        virtual bool SubscribeKlines(const std::string& symbol, const std::string& interval, KlineCallback callback) = 0;
        virtual bool Unsubscribe(const std::string& symbol, const std::string& streamType) = 0;

        // Utility
        virtual std::string GetExchangeName() const = 0;
        virtual std::vector<std::string> GetAvailablePairs() = 0;
        virtual bool IsValidPair(const std::string& symbol) = 0;
    };

} // namespace API

#endif //IEXCHANGEAPI_H