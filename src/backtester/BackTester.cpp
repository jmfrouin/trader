//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#include "BackTester.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace BackTester {

CBackTester::CBackTester()
    : mInitialBalance(10000.0)
    , mTimeframe("1h")
    , mPair("BTCUSDT")
    , mStartTimestamp(0)
    , mEndTimestamp(std::time(nullptr))
    , mFeeRate(0.001) // 0.1% fee
    , mSlippagePercent(0.05) // 0.05% slippage
    , mResultAvailable(false)
{
    mCurrentBalance = mInitialBalance;
    mCurrentPosition = 0.0;
    mPositionValue = 0.0;
    mTotalTrades = 0;
    mWinningTrades = 0;
    mLosingTrades = 0;
    mMaxDrawdown = 0.0;
    mSharpeRatio = 0.0;
}

CBackTester::~CBackTester() = default;

void CBackTester::SetInitialBalance(double balance) {
    mInitialBalance = balance;
    mCurrentBalance = balance;
}

void CBackTester::SetTimeframe(const std::string& timeframe) {
    mTimeframe = timeframe;
}

void CBackTester::SetPair(const std::string& pair) {
    mPair = pair;
}

void CBackTester::SetStartDate(const std::string& date) {
    mStartTimestamp = ParseDate(date);
}

void CBackTester::SetEndDate(const std::string& date) {
    mEndTimestamp = ParseDate(date);
}

void CBackTester::SetStrategy(std::shared_ptr<Strategy::IStrategy> strategy) {
    mStrategy = strategy;
}

void CBackTester::SetExchangeAPI(std::shared_ptr<API::IExchangeAPI> api) {
    mExchangeAPI = api;
}

void CBackTester::SetFeeRate(double feeRate) {
    mFeeRate = feeRate;
}

void CBackTester::SetSlippageModel(double slippagePercent) {
    mSlippagePercent = slippagePercent;
}

void CBackTester::LoadHistoricalData(const std::string& csvFile) {
    std::ifstream file(csvFile);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open CSV file: " + csvFile);
    }

    mHistoricalData.clear();
    std::string line;

    // Skip header line
    if (std::getline(file, line)) {
        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string item;

            API::SKline kline;
            int field = 0;

            while (std::getline(ss, item, ',')) {
                switch (field) {
                    case 0: kline.mOpenTime = std::stoll(item); break;
                    case 1: kline.mOpen = std::stod(item); break;
                    case 2: kline.mHigh = std::stod(item); break;
                    case 3: kline.mLow = std::stod(item); break;
                    case 4: kline.mClose = std::stod(item); break;
                    case 5: kline.mVolume = std::stod(item); break;
                    case 6: kline.mCloseTime = std::stoll(item); break;
                    default: break;
                }
                field++;
            }

            if (field >= 7) {
                mHistoricalData.push_back(kline);
            }
        }
    }

    file.close();

    std::cout << "Loaded " << mHistoricalData.size() << " historical data points" << std::endl;
}

void CBackTester::LoadHistoricalDataFromAPI() {
    if (!mExchangeAPI || !mExchangeAPI->IsInitialized()) {
        throw std::runtime_error("Exchange API not initialized");
    }

    // Calculate the time range in milliseconds
    long long startTime = mStartTimestamp * 1000;
    long long endTime = mEndTimestamp * 1000;

    mHistoricalData.clear();

    // Fetch data in chunks (most APIs limit the number of klines per request)
    const int maxKlines = 1000;
    long long currentTime = startTime;

    while (currentTime < endTime) {
        try {
            auto klines = mExchangeAPI->GetKlines(mPair, mTimeframe, maxKlines, currentTime, 0);

            if (klines.empty()) {
                break;
            }

            // Filter klines within our time range
            for (const auto& kline : klines) {
                if (kline.mOpenTime >= startTime && kline.mOpenTime <= endTime) {
                    mHistoricalData.push_back(kline);
                }
            }

            // Update current time to the last kline's close time
            currentTime = klines.back().mCloseTime + 1;

            // Small delay to respect rate limits
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        } catch (const std::exception& e) {
            std::cerr << "Error fetching historical data: " << e.what() << std::endl;
            break;
        }
    }

    // Sort by timestamp
    std::sort(mHistoricalData.begin(), mHistoricalData.end(),
              [](const API::SKline& a, const API::SKline& b) {
                  return a.mOpenTime < b.mOpenTime;
              });

    std::cout << "Loaded " << mHistoricalData.size() << " historical data points from API" << std::endl;
}

SBacktestResult CBackTester::Run() {
    if (!mStrategy) {
        throw std::runtime_error("Strategy not set");
    }

    if (mHistoricalData.empty()) {
        throw std::runtime_error("No historical data available");
    }

    Reset();

    mEquityCurve.clear();
    mTrades.clear();
    mDrawdownCurve.clear();
    mReturns.clear();

    double peakEquity = mInitialBalance;

    for (size_t i = 0; i < mHistoricalData.size(); ++i) {
        const auto& currentKline = mHistoricalData[i];

        // Update strategy with current market data
        mStrategy->Update(currentKline);

        // Get signal from strategy
        auto signal = mStrategy->GetSignal();

        // Execute trades based on signal
        if (signal.mType != Strategy::ESignalType::HOLD) {
            ExecuteTrade(signal, currentKline);
        }

        // Calculate current equity
        double currentPrice = currentKline.mClose;
        double equity = mCurrentBalance + (mCurrentPosition * currentPrice);

        // Update equity curve
        mEquityCurve[currentKline.mOpenTime] = equity;

        // Calculate drawdown
        if (equity > peakEquity) {
            peakEquity = equity;
        }

        double drawdown = (peakEquity - equity) / peakEquity * 100.0;
        mDrawdownCurve[currentKline.mOpenTime] = drawdown;

        if (drawdown > mMaxDrawdown) {
            mMaxDrawdown = drawdown;
        }

        // Calculate returns
        if (i > 0) {
            double previousEquity = mEquityCurve[mHistoricalData[i-1].mOpenTime];
            double returnRate = (equity - previousEquity) / previousEquity;
            mReturns.push_back(returnRate);
        }
    }

    // Calculate final metrics
    double finalEquity = mEquityCurve.rbegin()->second;
    double totalReturn = (finalEquity - mInitialBalance) / mInitialBalance * 100.0;

    mSharpeRatio = CalculateSharpeRatio(mReturns, 0.02); // Assuming 2% risk-free rate

    double winRate = mTotalTrades > 0 ? (double)mWinningTrades / mTotalTrades * 100.0 : 0.0;

    // Create result
    SBacktestResult result;
    result.mInitialBalance = mInitialBalance;
    result.mFinalBalance = finalEquity;
    result.mTotalReturn = totalReturn;
    result.mMaxDrawdown = mMaxDrawdown;
    result.mSharpeRatio = mSharpeRatio;
    result.mTotalTrades = mTotalTrades;
    result.mWinningTrades = mWinningTrades;
    result.mLosingTrades = mLosingTrades;
    result.mWinRate = winRate;
    result.mStartTimestamp = mStartTimestamp;
    result.mEndTimestamp = mEndTimestamp;
    result.mPair = mPair;
    result.mTimeframe = mTimeframe;
    result.mEquityCurve = mEquityCurve;
    result.mTrades = mTrades;
    result.mDrawdownCurve = mDrawdownCurve;

    mResult = result;
    mResultAvailable = true;

    return result;
}

void CBackTester::Reset() {
    mCurrentBalance = mInitialBalance;
    mCurrentPosition = 0.0;
    mPositionValue = 0.0;
    mTotalTrades = 0;
    mWinningTrades = 0;
    mLosingTrades = 0;
    mMaxDrawdown = 0.0;
    mSharpeRatio = 0.0;
    mResultAvailable = false;

    if (mStrategy) {
        mStrategy->Reset();
    }
}

void CBackTester::SaveResultsToJson(const std::string& filename) const {
    if (!mResultAvailable) {
        throw std::runtime_error("No backtest results available. Run backtest first.");
    }

    nlohmann::json jsonResult = GetResultsAsJson();

    std::ofstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + filename);
    }

    file << std::setw(4) << jsonResult << std::endl;
    file.close();

    std::cout << "Results saved to: " << filename << std::endl;
}

nlohmann::json CBackTester::GetResultsAsJson() const {
    if (!mResultAvailable) {
        throw std::runtime_error("No backtest results available. Run backtest first.");
    }

    nlohmann::json json;

    // Basic metrics
    json["summary"]["initialBalance"] = mResult.mInitialBalance;
    json["summary"]["finalBalance"] = mResult.mFinalBalance;
    json["summary"]["totalReturn"] = mResult.mTotalReturn;
    json["summary"]["maxDrawdown"] = mResult.mMaxDrawdown;
    json["summary"]["sharpeRatio"] = mResult.mSharpeRatio;
    json["summary"]["totalTrades"] = mResult.mTotalTrades;
    json["summary"]["winningTrades"] = mResult.mWinningTrades;
    json["summary"]["losingTrades"] = mResult.mLosingTrades;
    json["summary"]["winRate"] = mResult.mWinRate;
    json["summary"]["pair"] = mResult.mPair;
    json["summary"]["timeframe"] = mResult.mTimeframe;
    json["summary"]["startTimestamp"] = mResult.mStartTimestamp;
    json["summary"]["endTimestamp"] = mResult.mEndTimestamp;

    // Equity curve
    for (const auto& point : mResult.mEquityCurve) {
        json["equityCurve"].push_back({
            {"timestamp", point.first},
            {"equity", point.second}
        });
    }

    // Trades
    for (const auto& trade : mResult.mTrades) {
        json["trades"].push_back({
            {"timestamp", trade.mTimestamp},
            {"type", trade.mType == Strategy::ESignalType::BUY ? "BUY" : "SELL"},
            {"price", trade.mPrice},
            {"quantity", trade.mQuantity},
            {"pnl", trade.mPnl},
            {"balance", trade.mBalance}
        });
    }

    // Drawdown curve
    for (const auto& point : mResult.mDrawdownCurve) {
        json["drawdownCurve"].push_back({
            {"timestamp", point.first},
            {"drawdown", point.second}
        });
    }

    return json;
}

void CBackTester::ExecuteTrade(const Strategy::SSignal& signal, const API::SKline& currentKline) {
    double currentPrice = currentKline.mClose;

    // Apply slippage
    double slippage = 1.0 + (mSlippagePercent / 100.0);
    if (signal.mType == Strategy::ESignalType::BUY) {
        currentPrice *= slippage;
    } else {
        currentPrice /= slippage;
    }

    STradeRecord trade;
    trade.mTimestamp = currentKline.mOpenTime;
    trade.mType = signal.mType;
    trade.mPrice = currentPrice;

    if (signal.mType == Strategy::ESignalType::BUY && mCurrentPosition == 0.0) {
        // Open long position
        double cost = mCurrentBalance * signal.mSize;
        double fee = cost * mFeeRate;
        double netCost = cost + fee;

        if (netCost <= mCurrentBalance) {
            mCurrentPosition = (cost) / currentPrice;
            mCurrentBalance -= netCost;
            mPositionValue = cost;

            trade.mQuantity = mCurrentPosition;
            trade.mPnl = -fee;
            trade.mBalance = mCurrentBalance;

            mTrades.push_back(trade);
            mTotalTrades++;
        }
    }
    else if (signal.mType == Strategy::ESignalType::SELL && mCurrentPosition > 0.0) {
        // Close long position
        double proceeds = mCurrentPosition * currentPrice;
        double fee = proceeds * mFeeRate;
        double netProceeds = proceeds - fee;

        double pnl = netProceeds - mPositionValue;

        mCurrentBalance += netProceeds;

        trade.mQuantity = mCurrentPosition;
        trade.mPnl = pnl;
        trade.mBalance = mCurrentBalance;

        mTrades.push_back(trade);
        mTotalTrades++;

        if (pnl > 0) {
            mWinningTrades++;
        } else {
            mLosingTrades++;
        }

        mCurrentPosition = 0.0;
        mPositionValue = 0.0;
    }
}

double CBackTester::CalculateSharpeRatio(const std::vector<double>& returns, double riskFreeRate) const {
    if (returns.empty()) {
        return 0.0;
    }

    // Convert annual risk-free rate to period rate
    double periodRiskFreeRate = riskFreeRate / 365.0; // Daily rate

    // Calculate excess returns
    std::vector<double> excessReturns;
    for (double ret : returns) {
        excessReturns.push_back(ret - periodRiskFreeRate);
    }

    // Calculate mean excess return
    double meanExcessReturn = 0.0;
    for (double ret : excessReturns) {
        meanExcessReturn += ret;
    }
    meanExcessReturn /= excessReturns.size();

    // Calculate standard deviation of excess returns
    double variance = 0.0;
    for (double ret : excessReturns) {
        variance += std::pow(ret - meanExcessReturn, 2);
    }
    variance /= excessReturns.size();
    double stdDev = std::sqrt(variance);

    if (stdDev == 0.0) {
        return 0.0;
    }

    // Annualize the Sharpe ratio
    return (meanExcessReturn / stdDev) * std::sqrt(365.0);
}

double CBackTester::CalculateMaxDrawdown(const std::map<long long, double>& equity) const {
    double maxDrawdown = 0.0;
    double peak = 0.0;

    for (const auto& point : equity) {
        double currentEquity = point.second;

        if (currentEquity > peak) {
            peak = currentEquity;
        }

        double drawdown = (peak - currentEquity) / peak * 100.0;
        if (drawdown > maxDrawdown) {
            maxDrawdown = drawdown;
        }
    }

    return maxDrawdown;
}

std::time_t CBackTester::ParseDate(const std::string& date) const {
    std::tm tm = {};
    std::istringstream ss(date);

    // Try different date formats
    if (ss >> std::get_time(&tm, "%Y-%m-%d")) {
        return std::mktime(&tm);
    }

    ss.clear();
    ss.str(date);
    if (ss >> std::get_time(&tm, "%d/%m/%Y")) {
        return std::mktime(&tm);
    }
    ss.clear();
    ss.str(date);
    if (ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S")) {
        return std::mktime(&tm);
    }

    throw std::runtime_error("Invalid date format: " + date);
}

} // namespace BackTester
