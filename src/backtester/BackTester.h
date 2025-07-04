//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#ifndef BACKTESTER_H
#define BACKTESTER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <ctime>
#include <nlohmann/json.hpp>

// Forward declarations
namespace API {
    class IExchangeAPI;
    struct SKline;
}

namespace Strategy {
    class IStrategy;
    struct SSignal;
    enum class ESignalType;
}

namespace BackTester {

// Structure pour stocker les résultats du backtest
struct SBacktestResult {
    double mInitialBalance = 0.0;
    double mFinalBalance = 0.0;
    double mTotalReturn = 0.0;
    double mMaxDrawdown = 0.0;
    double mSharpeRatio = 0.0;
    int mTotalTrades = 0;
    int mWinningTrades = 0;
    int mLosingTrades = 0;
    double mWinRate = 0.0;
    std::string mPair;
    std::string mTimeframe;
    std::time_t mStartTimestamp = 0;
    std::time_t mEndTimestamp = 0;

    // Courbes de performance
    std::map<long long, double> mEquityCurve;
    std::vector<struct STradeRecord> mTrades;
    std::map<long long, double> mDrawdownCurve;
};

// Structure pour enregistrer les trades
struct STradeRecord {
    long long mTimestamp = 0;
    Strategy::ESignalType mType;
    double mPrice = 0.0;
    double mQuantity = 0.0;
    double mPnl = 0.0;
    double mBalance = 0.0;
};

// Classe principale du backtester
class CBackTester {
public:
    CBackTester();
    ~CBackTester();

    // Configuration du backtest
    void SetInitialBalance(double balance);
    void SetTimeframe(const std::string& timeframe);
    void SetPair(const std::string& pair);
    void SetStartDate(const std::string& date);
    void SetEndDate(const std::string& date);
    void SetStrategy(std::shared_ptr<Strategy::IStrategy> strategy);
    void SetExchangeAPI(std::shared_ptr<API::IExchangeAPI> api);
    void SetFeeRate(double feeRate);
    void SetSlippageModel(double slippagePercent);

    // Chargement des données historiques
    void LoadHistoricalData(const std::string& csvFile);
    void LoadHistoricalDataFromAPI();

    // Exécution du backtest
    SBacktestResult Run();
    void Reset();

    // Sauvegarde et récupération des résultats
    void SaveResultsToJson(const std::string& filename) const;
    nlohmann::json GetResultsAsJson() const;

    // Getters pour les résultats
    const SBacktestResult& GetResult() const { return mResult; }
    bool IsResultAvailable() const { return mResultAvailable; }

private:
    // Configuration
    double mInitialBalance;
    std::string mTimeframe;
    std::string mPair;
    std::time_t mStartTimestamp;
    std::time_t mEndTimestamp;
    double mFeeRate;
    double mSlippagePercent;

    // Composants
    std::shared_ptr<Strategy::IStrategy> mStrategy;
    std::shared_ptr<API::IExchangeAPI> mExchangeAPI;

    // Données
    std::vector<API::SKline> mHistoricalData;

    // État du backtest
    double mCurrentBalance;
    double mCurrentPosition;
    double mPositionValue;
    int mTotalTrades;
    int mWinningTrades;
    int mLosingTrades;
    double mMaxDrawdown;
    double mSharpeRatio;

    // Résultats
    SBacktestResult mResult;
    bool mResultAvailable;

    // Courbes de performance
    std::map<long long, double> mEquityCurve;
    std::vector<STradeRecord> mTrades;
    std::map<long long, double> mDrawdownCurve;
    std::vector<double> mReturns;

    // Méthodes privées
    void ExecuteTrade(const Strategy::SSignal& signal, const API::SKline& currentKline);
    double CalculateSharpeRatio(const std::vector<double>& returns, double riskFreeRate = 0.02) const;
    double CalculateMaxDrawdown(const std::map<long long, double>& equity) const;
    std::time_t ParseDate(const std::string& date) const;
};

} // namespace BackTester

#endif //BACKTESTER_H