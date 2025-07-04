
//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#include "RiskManager.h"

#include <iostream>
#include <algorithm>

namespace Risk {

CRiskManager::CRiskManager()
    : mMaxCapitalPerTrade(5.0) // % of capital
    , mMaxTotalExposure(50.0)  // % of capital
    , mMaxSymbolExposure(20.0) // % of capital
    , mMaxOpenPositions(5)
    , mMaxDailyLoss(10.0)      // % of capital
    , mDefaultStopLoss(2.0)    // % from entry
    , mDefaultTakeProfit(5.0)  // % from entry
    , mMinTimeBetweenTrades(60) // seconds
    , mEnableVolatilityCheck(true)
    , mMaxVolatility(5.0)      // % price change
    , mTotalExposure(0.0)
    , mTodayPnL(0.0) {
    mStartOfDay = std::chrono::system_clock::now();

    // Reset daily PnL at start of day
    auto now = std::chrono::system_clock::now();
    auto today = std::chrono::floor<std::chrono::days>(now);
    if (today > std::chrono::floor<std::chrono::days>(mStartOfDay)) {
        mStartOfDay = today;
        mTodayPnL = 0.0;
    }
}

CRiskManager::~CRiskManager() = default;

void CRiskManager::Configure(const nlohmann::json& config) {
    if (config.contains("risk") && config["risk"].is_object()) {
        const auto& risk = config["risk"];

        if (risk.contains("capital_pct")) {
            mMaxCapitalPerTrade = risk["capital_pct"];
        }

        if (risk.contains("max_exposure")) {
            mMaxTotalExposure = risk["max_exposure"];
        }

        if (risk.contains("max_symbol_exposure")) {
            mMaxSymbolExposure = risk["max_symbol_exposure"];
        }

        if (risk.contains("max_positions")) {
            mMaxOpenPositions = risk["max_positions"];
        }

        if (risk.contains("max_daily_loss")) {
            mMaxDailyLoss = risk["max_daily_loss"];
        }

        if (risk.contains("stop_loss_pct")) {
            mDefaultStopLoss = risk["stop_loss_pct"];
        }

        if (risk.contains("take_profit_pct")) {
            mDefaultTakeProfit = risk["take_profit_pct"];
        }

        if (risk.contains("min_time_between_trades")) {
            mMinTimeBetweenTrades = std::chrono::seconds(risk["min_time_between_trades"].get<int>());
        }

        if (risk.contains("check_volatility")) {
            mEnableVolatilityCheck = risk["check_volatility"];
        }

        if (risk.contains("max_volatility")) {
            mMaxVolatility = risk["max_volatility"];
        }
    }
}

nlohmann::json CRiskManager::GetConfig() const {
    nlohmann::json config;

    nlohmann::json risk;
    risk["capital_pct"] = mMaxCapitalPerTrade;
    risk["max_exposure"] = mMaxTotalExposure;
    risk["max_symbol_exposure"] = mMaxSymbolExposure;
    risk["max_positions"] = mMaxOpenPositions;
    risk["max_daily_loss"] = mMaxDailyLoss;
    risk["stop_loss_pct"] = mDefaultStopLoss;
    risk["take_profit_pct"] = mDefaultTakeProfit;
    risk["min_time_between_trades"] = mMinTimeBetweenTrades.count();
    risk["check_volatility"] = mEnableVolatilityCheck;
    risk["max_volatility"] = mMaxVolatility;

    config["risk"] = risk;

    return config;
}

double CRiskManager::CalculatePositionSize(const std::string& symbol, double price, double availableBalance) const {
    // Calculate position size based on risk parameters
    double maxAmount = availableBalance * (mMaxCapitalPerTrade / 100.0);

    // Check against maximum total exposure
    double remainingExposure = availableBalance * (mMaxTotalExposure / 100.0) - mTotalExposure;
    maxAmount = std::min(maxAmount, remainingExposure);

    // Check against maximum symbol exposure
    double symbolExposureLimit = availableBalance * (mMaxSymbolExposure / 100.0);
    double currentSymbolExposure = 0.0;

    {
        std::lock_guard<std::mutex> guard(mPositionsMutex);
        auto it = mSymbolExposure.find(symbol);
        if (it != mSymbolExposure.end()) {
            currentSymbolExposure = it->second;
        }
    }

    double remainingSymbolExposure = symbolExposureLimit - currentSymbolExposure;
    maxAmount = std::min(maxAmount, remainingSymbolExposure);

    // Convert to quantity
    double quantity = price > 0 ? maxAmount / price : 0.0;

    return quantity;
}

bool CRiskManager::CheckPositionAllowed(const std::string& symbol, Strategy::EOrderSide side, double quantity, double price) const {
    // Check basic parameters
    if (symbol.empty() || quantity <= 0 || price <= 0) {
        return false;
    }

    // Check maximum positions
    if (!CheckMaxOpenPositions()) {
        return false;
    }

    // Check daily loss limit
    if (!CheckMaxDailyLoss()) {
        return false;
    }

    // Check symbol exposure
    double exposure = quantity * price;
    if (!CheckSymbolExposure(symbol, exposure)) {
        return false;
    }

    // Check trade frequency
    if (!CheckTradeFrequency(symbol)) {
        return false;
    }

    // Check market volatility if enabled
    if (mEnableVolatilityCheck && !CheckMarketVolatility(symbol, price)) {
        return false;
    }

    return true;
}

bool CRiskManager::CheckMaxOpenPositions() const {
    std::lock_guard<std::mutex> guard(mPositionsMutex);
    return mOpenPositions.size() < static_cast<size_t>(mMaxOpenPositions);
}

bool CRiskManager::CheckMaxDailyLoss() const {
    // Reset daily PnL at start of day
    auto now = std::chrono::system_clock::now();
    auto today = std::chrono::floor<std::chrono::days>(now);
    if (today > std::chrono::floor<std::chrono::days>(mStartOfDay)) {
        const_cast<CRiskManager*>(this)->mStartOfDay = today;
        const_cast<CRiskManager*>(this)->mTodayPnL = 0.0;
    }

    // Calculate max loss threshold
    // This would typically be based on account balance from the API, but we'll use a fixed value here
    double accountBalance = 10000.0; // Example value
    double maxLossThreshold = accountBalance * (mMaxDailyLoss / 100.0);

    return -mTodayPnL < maxLossThreshold;
}

bool CRiskManager::CheckMarketVolatility(const std::string& symbol, double price) const {
    // In a real implementation, this would check recent price history to determine volatility
    // For simplicity, we'll return true
    return true;
}

bool CRiskManager::CheckSymbolExposure(const std::string& symbol, double addedExposure) const {
    std::lock_guard<std::mutex> guard(mPositionsMutex);

    // Current exposure for this symbol
    double currentExposure = 0.0;
    auto it = mSymbolExposure.find(symbol);
    if (it != mSymbolExposure.end()) {
        currentExposure = it->second;
    }

    // Account balance (in a real implementation, would come from API)
    double accountBalance = 10000.0; // Example value

    // Calculate maximum allowed exposure for this symbol
    double maxAllowedExposure = accountBalance * (mMaxSymbolExposure / 100.0);

    // Check if new total exposure would exceed the limit
    return (currentExposure + addedExposure) <= maxAllowedExposure;
}

bool CRiskManager::CheckTradeFrequency(const std::string& symbol) const {
    std::lock_guard<std::mutex> guard(mPositionsMutex);

    // Check if we've traded this symbol recently
    auto it = mLastTradeTime.find(symbol);
    if (it != mLastTradeTime.end()) {
        auto now = std::chrono::system_clock::now();
        auto elapsed = now - it->second;

        if (elapsed < mMinTimeBetweenTrades) {
            return false;
        }
    }

    return true;
}

void CRiskManager::RegisterPosition(const Strategy::SPosition& position) {
    std::lock_guard<std::mutex> guard(mPositionsMutex);

    if (position.mId.empty()) {
        throw std::invalid_argument("Position ID cannot be empty");
    }

    // Calculate position exposure
    double exposure = position.mQuantity * position.mEntryPrice;

    // Update state
    mOpenPositions[position.mId] = position;
    mSymbolExposure[position.mSymbol] += exposure;
    mTotalExposure += exposure;
    mLastTradeTime[position.mSymbol] = std::chrono::system_clock::now();
}

void CRiskManager::ClosePosition(const std::string& positionId, double exitPrice, double pnl) {
    std::lock_guard<std::mutex> guard(mPositionsMutex);

    auto it = mOpenPositions.find(positionId);
    if (it == mOpenPositions.end()) {
        throw std::runtime_error("Position with ID '" + positionId + "' not found");
    }

    const auto& position = it->second;

    // Calculate position exposure
    double exposure = position.mQuantity * position.mEntryPrice;

    // Update state
    mSymbolExposure[position.mSymbol] -= exposure;
    mTotalExposure -= exposure;
    mTodayPnL += pnl;

    // Remove position
    mOpenPositions.erase(it);
}

std::vector<Strategy::SPosition> CRiskManager::GetOpenPositions() const {
    std::lock_guard<std::mutex> guard(mPositionsMutex);

    std::vector<Strategy::SPosition> result;
    for (const auto& pair : mOpenPositions) {
        result.push_back(pair.second);
    }

    return result;
}

double CRiskManager::GetTotalExposure() const {
    std::lock_guard<std::mutex> guard(mPositionsMutex);
    return mTotalExposure;
}

double CRiskManager::GetSymbolExposure(const std::string& symbol) const {
    std::lock_guard<std::mutex> guard(mPositionsMutex);

    auto it = mSymbolExposure.find(symbol);
    if (it != mSymbolExposure.end()) {
        return it->second;
    }

    return 0.0;
}

std::pair<double, double> CRiskManager::CalculateExitLevels(const std::string& symbol, Strategy::EOrderSide side, double entryPrice) const {
    double stopLoss = 0.0;
    double takeProfit = 0.0;

    if (side == Strategy::EOrderSide::BUY) {
        stopLoss = entryPrice * (1.0 - mDefaultStopLoss / 100.0);
        takeProfit = entryPrice * (1.0 + mDefaultTakeProfit / 100.0);
    } else { // SELL
        stopLoss = entryPrice * (1.0 + mDefaultStopLoss / 100.0);
        takeProfit = entryPrice * (1.0 - mDefaultTakeProfit / 100.0);
    }

    return {stopLoss, takeProfit};
}

void CRiskManager::SetMaxCapitalPerTrade(double percentage) {
    mMaxCapitalPerTrade = percentage;
}

void CRiskManager::SetMaxTotalExposure(double percentage) {
    mMaxTotalExposure = percentage;
}

void CRiskManager::SetMaxSymbolExposure(double percentage) {
    mMaxSymbolExposure = percentage;
}

void CRiskManager::SetMaxOpenPositions(int maxPositions) {
    mMaxOpenPositions = maxPositions;
}

void CRiskManager::SetMaxDailyLoss(double percentage) {
    mMaxDailyLoss = percentage;
}

void CRiskManager::SetDefaultStopLoss(double percentage) {
    mDefaultStopLoss = percentage;
}

void CRiskManager::SetDefaultTakeProfit(double percentage) {
    mDefaultTakeProfit = percentage;
}

void CRiskManager::SetMinTimeBetweenTrades(std::chrono::seconds seconds) {
    mMinTimeBetweenTrades = seconds;
}

void CRiskManager::SetEnableVolatilityCheck(bool enable) {
    mEnableVolatilityCheck = enable;
}

void CRiskManager::SetMaxVolatility(double percentage) {
    mMaxVolatility = percentage;
}

double CRiskManager::GetTodayPnL() const {
    return mTodayPnL;
}

void CRiskManager::ResetDailyStats() {
    mTodayPnL = 0.0;
    mStartOfDay = std::chrono::system_clock::now();
}

} // namespace Risk