//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#ifndef STRATEGYENGINE_H
#define STRATEGYENGINE_H


#include "IStrategy.h"
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>

// Forward declarations
namespace API {
    class IExchangeAPI;
    struct SKline;
    struct STicker;
}

namespace Strategy {

// Forward declarations
class IStrategy;
struct SSignal;
struct SPosition;

// Énumération pour les types de stratégies
enum class EStrategyType {
    SCALPING,
    SWING,
    POSITION,
    ARBITRAGE,
    GRID,
    DCA,
    MOMENTUM,
    MEAN_REVERSION
};

// Énumération pour les états de stratégie
enum class EStrategyState {
    INACTIVE,
    ACTIVE,
    PAUSED,
    ERROR,
    STOPPED
};

// Structure pour les paramètres de stratégie
struct SStrategyParams {
    std::string mName;
    EStrategyType mType = EStrategyType::SWING;
    EStrategyState mState = EStrategyState::INACTIVE;
    double mRiskPercentage = 2.0;
    double mMaxDrawdown = 10.0;
    int mMaxOpenPositions = 3;
    std::string mTimeframe = "1h";
    std::vector<std::string> mSymbols;
    nlohmann::json mCustomParams;
};

// Structure pour les statistiques de stratégie
struct SStrategyStatistics {
    std::string mStrategyName;
    int mTotalTrades = 0;
    int mWinningTrades = 0;
    int mLosingTrades = 0;
    double mWinRate = 0.0;
    double mTotalPnL = 0.0;
    double mSharpeRatio = 0.0;
    double mMaxDrawdown = 0.0;
    double mCurrentDrawdown = 0.0;
    std::chrono::system_clock::time_point mLastTradeTime;
    std::chrono::system_clock::time_point mStartTime;
};

// Classe principale du moteur de stratégies
class CStrategyEngine {
public:
    CStrategyEngine();
    ~CStrategyEngine();

    // Gestion des stratégies
    void RegisterStrategy(std::shared_ptr<IStrategy> strategy);
    void RemoveStrategy(const std::string& strategyName);
    std::shared_ptr<IStrategy> GetStrategy(const std::string& strategyName);
    std::vector<std::string> GetAvailableStrategies() const;
    std::vector<std::string> GetActiveStrategies() const;

    // État des stratégies
    void StartStrategy(const std::string& strategyName);
    void StopStrategy(const std::string& strategyName);
    void PauseStrategy(const std::string& strategyName);
    void ResumeStrategy(const std::string& strategyName);
    EStrategyState GetStrategyState(const std::string& strategyName) const;

    // Exécution des stratégies
    SSignal ExecuteStrategy(const std::string& strategyName,
                           const std::vector<API::SKline>& klines,
                           const API::STicker& ticker);
    void ExecuteAllStrategies(const std::vector<API::SKline>& klines,
                             const API::STicker& ticker);

    // Configuration
    void ConfigureStrategy(const std::string& strategyName, const nlohmann::json& config);
    nlohmann::json GetStrategyConfig(const std::string& strategyName) const;
    void SetStrategyParams(const std::string& strategyName, const SStrategyParams& params);
    SStrategyParams GetStrategyParams(const std::string& strategyName) const;

    // Gestion des positions
    void RegisterPosition(const SPosition& position);
    void ClosePosition(const std::string& positionId, double exitPrice, double pnl);
    void UpdatePosition(const std::string& positionId, double currentPrice);
    std::vector<SPosition> GetOpenPositions() const;
    std::vector<SPosition> GetOpenPositionsByStrategy(const std::string& strategyName) const;
    std::vector<SPosition> GetPositionsBySymbol(const std::string& symbol) const;

    // Statistiques et performance
    SStrategyStatistics GetStrategyStatistics(const std::string& strategyName) const;
    std::map<std::string, SStrategyStatistics> GetAllStrategyStatistics() const;
    double GetTotalPnL() const;
    double GetTotalPnLByStrategy(const std::string& strategyName) const;

    // API d'échange
    void SetExchangeAPI(std::shared_ptr<API::IExchangeAPI> api);
    std::shared_ptr<API::IExchangeAPI> GetExchangeAPI() const;

    // Gestion des risques
    void SetRiskManager(std::shared_ptr<class CRiskManager> riskManager);
    std::shared_ptr<class CRiskManager> GetRiskManager() const;

    // Événements et callbacks
    using StrategyCallback = std::function<void(const std::string&, const SSignal&)>;
    using PositionCallback = std::function<void(const std::string&, const SPosition&)>;
    using ErrorCallback = std::function<void(const std::string&, const std::string&)>;

    void SetStrategyCallback(StrategyCallback callback);
    void SetPositionCallback(PositionCallback callback);
    void SetErrorCallback(ErrorCallback callback);

    // Utilitaires
    void ResetStrategy(const std::string& strategyName);
    void ResetAllStrategies();
    bool IsStrategyActive(const std::string& strategyName) const;
    size_t GetActiveStrategyCount() const;

    // Sauvegarde et chargement
    void SaveStrategyState(const std::string& strategyName, const std::string& filename) const;
    void LoadStrategyState(const std::string& strategyName, const std::string& filename);
    nlohmann::json ExportAllStrategies() const;
    void ImportStrategies(const nlohmann::json& strategiesData);

private:
    // Stratégies
    std::map<std::string, std::shared_ptr<IStrategy>> mStrategies;
    std::map<std::string, SStrategyParams> mStrategyParams;
    std::map<std::string, EStrategyState> mStrategyStates;
    std::map<std::string, SStrategyStatistics> mStrategyStats;
    mutable std::mutex mStrategiesMutex;

    // Positions
    std::map<std::string, SPosition> mOpenPositions;
    std::map<std::string, std::string> mPositionToStrategy;
    std::map<std::string, std::vector<std::string>> mStrategyPositions;
    mutable std::mutex mPositionsMutex;

    // Composants externes
    std::shared_ptr<API::IExchangeAPI> mExchangeAPI;
    std::shared_ptr<class CRiskManager> mRiskManager;

    // Callbacks
    StrategyCallback mStrategyCallback;
    PositionCallback mPositionCallback;
    ErrorCallback mErrorCallback;

    // Synchronisation
    mutable std::mutex mCallbacksMutex;

    // Méthodes privées
    void UpdateStrategyStatistics(const std::string& strategyName, const SPosition& position, double pnl);
    void NotifyStrategySignal(const std::string& strategyName, const SSignal& signal);
    void NotifyPositionUpdate(const std::string& strategyName, const SPosition& position);
    void NotifyError(const std::string& strategyName, const std::string& error);
    bool ValidateStrategy(const std::string& strategyName) const;
    void CleanupClosedPositions();
    std::string GeneratePositionId() const;
};

} // namespace Strategy

#endif //STRATEGYENGINE_H
