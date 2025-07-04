//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#ifndef RISKMANAGER_H
#define RISKMANAGER_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <utility>
#include <nlohmann/json.hpp>

// Forward declarations
namespace Strategy {
    struct SPosition;
    enum class EOrderSide;
}

namespace Risk {

// Structure pour définir les paramètres de risque
struct SRiskParameters {
    double mMaxCapitalPerTrade = 5.0;     // % of capital per trade
    double mMaxTotalExposure = 50.0;      // % of total capital
    double mMaxSymbolExposure = 20.0;     // % of capital per symbol
    int mMaxOpenPositions = 5;            // Maximum number of open positions
    double mMaxDailyLoss = 10.0;          // % of capital daily loss limit
    double mDefaultStopLoss = 2.0;        // % from entry price
    double mDefaultTakeProfit = 5.0;      // % from entry price
    std::chrono::seconds mMinTimeBetweenTrades{60}; // Minimum time between trades
    bool mEnableVolatilityCheck = true;   // Enable volatility checks
    double mMaxVolatility = 5.0;          // % maximum price volatility
};

// Structure pour les statistiques de risque
struct SRiskStatistics {
    double mTotalExposure = 0.0;
    double mTodayPnL = 0.0;
    int mOpenPositionsCount = 0;
    double mMaxDrawdown = 0.0;
    double mCurrentDrawdown = 0.0;
    std::chrono::system_clock::time_point mLastResetTime;
};

// Classe principale de gestion des risques
class CRiskManager {
public:
    CRiskManager();
    ~CRiskManager();

    // Configuration
    void Configure(const nlohmann::json& config);
    nlohmann::json GetConfig() const;

    // Paramètres de risque - Setters
    void SetMaxCapitalPerTrade(double percentage);
    void SetMaxTotalExposure(double percentage);
    void SetMaxSymbolExposure(double percentage);
    void SetMaxOpenPositions(int maxPositions);
    void SetMaxDailyLoss(double percentage);
    void SetDefaultStopLoss(double percentage);
    void SetDefaultTakeProfit(double percentage);
    void SetMinTimeBetweenTrades(std::chrono::seconds seconds);
    void SetEnableVolatilityCheck(bool enable);
    void SetMaxVolatility(double percentage);

    // Paramètres de risque - Getters
    double GetMaxCapitalPerTrade() const { return mMaxCapitalPerTrade; }
    double GetMaxTotalExposure() const { return mMaxTotalExposure; }
    double GetMaxSymbolExposure() const { return mMaxSymbolExposure; }
    int GetMaxOpenPositions() const { return mMaxOpenPositions; }
    double GetMaxDailyLoss() const { return mMaxDailyLoss; }
    double GetDefaultStopLoss() const { return mDefaultStopLoss; }
    double GetDefaultTakeProfit() const { return mDefaultTakeProfit; }
    std::chrono::seconds GetMinTimeBetweenTrades() const { return mMinTimeBetweenTrades; }
    bool IsVolatilityCheckEnabled() const { return mEnableVolatilityCheck; }
    double GetMaxVolatility() const { return mMaxVolatility; }

    // Calculs de taille de position
    double CalculatePositionSize(const std::string& symbol, double price, double availableBalance) const;

    // Vérifications de risque
    bool CheckPositionAllowed(const std::string& symbol, Strategy::EOrderSide side, double quantity, double price) const;
    bool CheckMaxOpenPositions() const;
    bool CheckMaxDailyLoss() const;
    bool CheckSymbolExposure(const std::string& symbol, double addedExposure) const;
    bool CheckTradeFrequency(const std::string& symbol) const;
    bool CheckMarketVolatility(const std::string& symbol, double price) const;

    // Gestion des positions
    void RegisterPosition(const Strategy::SPosition& position);
    void ClosePosition(const std::string& positionId, double exitPrice, double pnl);
    std::vector<Strategy::SPosition> GetOpenPositions() const;

    // Exposition et statistiques
    double GetTotalExposure() const;
    double GetSymbolExposure(const std::string& symbol) const;
    double GetTodayPnL() const;
    SRiskStatistics GetRiskStatistics() const;

    // Calculs de niveaux de sortie
    std::pair<double, double> CalculateExitLevels(const std::string& symbol, Strategy::EOrderSide side, double entryPrice) const;

    // Utilitaires
    void ResetDailyStats();
    bool IsWithinRiskLimits() const;

    // Alertes et notifications
    enum class ERiskAlertType {
        DAILY_LOSS_LIMIT,
        TOTAL_EXPOSURE_LIMIT,
        SYMBOL_EXPOSURE_LIMIT,
        MAX_POSITIONS_LIMIT,
        VOLATILITY_ALERT
    };

    struct SRiskAlert {
        ERiskAlertType mType;
        std::string mSymbol;
        std::string mMessage;
        std::chrono::system_clock::time_point mTimestamp;
        double mCurrentValue;
        double mLimitValue;
    };

    std::vector<SRiskAlert> GetActiveAlerts() const;

private:
    // Paramètres de risque
    double mMaxCapitalPerTrade;
    double mMaxTotalExposure;
    double mMaxSymbolExposure;
    int mMaxOpenPositions;
    double mMaxDailyLoss;
    double mDefaultStopLoss;
    double mDefaultTakeProfit;
    std::chrono::seconds mMinTimeBetweenTrades;
    bool mEnableVolatilityCheck;
    double mMaxVolatility;

    // État actuel
    mutable std::mutex mPositionsMutex;
    std::map<std::string, Strategy::SPosition> mOpenPositions;
    std::map<std::string, double> mSymbolExposure;
    std::map<std::string, std::chrono::system_clock::time_point> mLastTradeTime;
    double mTotalExposure;
    double mTodayPnL;
    std::chrono::system_clock::time_point mStartOfDay;

    // Alertes actives
    mutable std::mutex mAlertsMutex;
    std::vector<SRiskAlert> mActiveAlerts;

    // Méthodes privées d'aide
    void UpdateRiskStatistics();
    void CheckAndGenerateAlerts() const;
    void AddAlert(ERiskAlertType type, const std::string& symbol, const std::string& message, double currentValue, double limitValue) const;
    void ClearExpiredAlerts();
};

} // namespace Risk

#endif //RISKMANAGER_H