
//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#ifndef ISTRATEGY_H
#define ISTRATEGY_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <nlohmann/json.hpp>
#include "api/IExchangeAPI.h"

namespace Strategy {

    // Forward declarations
    namespace API {
        enum class EOrderSide;
        struct SKline;
        struct STicker;
        class IExchangeAPI;
    }

    // Énumération pour les types de signaux
    enum class ESignalType {
        BUY,
        SELL,
        HOLD,
        CLOSE_LONG,
        CLOSE_SHORT,
        CANCEL
    };

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
        INITIALIZING
    };

    // Structure pour les positions ouvertes
    struct SPosition {
        std::string mSymbol;
        API::EOrderSide mSide;
        double mEntryPrice = 0.0;
        double mQuantity = 0.0;
        std::chrono::system_clock::time_point mEntryTime;
        double mStopLoss = 0.0;
        double mTakeProfit = 0.0;
        std::string mId;
        std::string mStrategyName;
        double mCurrentPrice = 0.0;
        double mUnrealizedPnL = 0.0;
        double mCommission = 0.0;
        std::map<std::string, std::string> mMetadata;
    };

    // Structure pour le résultat de la stratégie
    struct SSignal {
        ESignalType mType = ESignalType::HOLD;
        std::string mSymbol;
        double mPrice = 0.0;
        double mQuantity = 0.0;
        double mStopLoss = 0.0;
        double mTakeProfit = 0.0;
        double mConfidence = 0.0; // 0.0 à 1.0
        std::map<std::string, double> mParameters;
        std::string mMessage;
        std::chrono::system_clock::time_point mTimestamp;
        std::string mStrategyName;
    };

    // Structure pour les métriques de performance
    struct SStrategyMetrics {
        // Métriques de base
        int mTotalTrades = 0;
        int mWinningTrades = 0;
        int mLosingTrades = 0;
        double mWinRate = 0.0;

        // Métriques de performance
        double mTotalPnL = 0.0;
        double mTotalReturn = 0.0;
        double mSharpeRatio = 0.0;
        double mSortinoRatio = 0.0;
        double mMaxDrawdown = 0.0;
        double mCurrentDrawdown = 0.0;

        // Métriques de risque
        double mAverageTrade = 0.0;
        double mBestTrade = 0.0;
        double mWorstTrade = 0.0;
        double mVolatility = 0.0;
        double mBeta = 0.0;

        // Métriques de timing
        std::chrono::duration<double> mAverageTradeTime;
        std::chrono::system_clock::time_point mLastTradeTime;
        std::chrono::system_clock::time_point mStartTime;

        // Métriques avancées
        double mProfitFactor = 0.0;
        double mRecoveryFactor = 0.0;
        double mCalmarRatio = 0.0;
        int mConsecutiveWins = 0;
        int mConsecutiveLosses = 0;
        int mMaxConsecutiveWins = 0;
        int mMaxConsecutiveLosses = 0;
    };

    // Structure pour les paramètres de configuration
    struct SStrategyConfig {
        std::string mName;
        EStrategyType mType = EStrategyType::SWING;
        std::vector<std::string> mSymbols;
        std::string mTimeframe = "1h";
        double mRiskPercentage = 2.0;
        double mMaxDrawdown = 10.0;
        int mMaxOpenPositions = 3;
        bool mEnabled = true;
        nlohmann::json mCustomParams;
    };

    // Interface principale pour les stratégies
    class IStrategy {
    public:
        virtual ~IStrategy() = default;

        // Configuration de la stratégie
        virtual void Configure(const nlohmann::json& config) = 0;
        virtual nlohmann::json GetDefaultConfig() const = 0;
        virtual nlohmann::json GetCurrentConfig() const = 0;
        virtual void SetConfig(const SStrategyConfig& config) = 0;
        virtual SStrategyConfig GetConfig() const = 0;

        // Informations de base
        virtual std::string GetName() const = 0;
        virtual std::string GetDescription() const = 0;
        virtual std::string GetVersion() const = 0;
        virtual EStrategyType GetType() const = 0;
        virtual EStrategyState GetState() const = 0;

        // Initialisation et nettoyage
        virtual void Initialize() = 0;
        virtual void Shutdown() = 0;
        virtual void Reset() = 0;

        // Gestion de l'état
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual void Pause() = 0;
        virtual void Resume() = 0;

        // API de l'échange
        virtual void SetExchangeAPI(std::shared_ptr<API::IExchangeAPI> api) = 0;
        virtual std::shared_ptr<API::IExchangeAPI> GetExchangeAPI() const = 0;

        // Mise à jour des données et génération du signal
        virtual SSignal Update(const std::vector<API::SKline>& klines, const API::STicker& ticker) = 0;
        virtual std::vector<SSignal> ProcessMarketData(const std::vector<API::SKline>& klines, const API::STicker& ticker) = 0;

        // Gestion des positions
        virtual void OnPositionOpened(const SPosition& position) = 0;
        virtual void OnPositionClosed(const SPosition& position, double exitPrice, double pnl) = 0;
        virtual void OnPositionUpdated(const SPosition& position) = 0;

        // Gestion des ordres
        virtual void OnOrderFilled(const std::string& orderId, const SPosition& position) = 0;
        virtual void OnOrderCanceled(const std::string& orderId, const std::string& reason) = 0;
        virtual void OnOrderRejected(const std::string& orderId, const std::string& reason) = 0;

        // Métriques et statistiques
        virtual SStrategyMetrics GetMetrics() const = 0;
        virtual std::map<std::string, double> GetCustomMetrics() const = 0;
        virtual void UpdateMetrics(const SPosition& position, double pnl) = 0;

        // Validation et tests
        virtual bool ValidateSignal(const SSignal& signal) const = 0;
        virtual bool CanTrade(const std::string& symbol) const = 0;
        virtual double CalculatePositionSize(const std::string& symbol, double price, double availableBalance) const = 0;

        // Gestion des erreurs
        virtual void OnError(const std::string& error) = 0;
        virtual std::vector<std::string> GetErrors() const = 0;
        virtual void ClearErrors() = 0;

        // Sérialisation
        virtual nlohmann::json Serialize() const = 0;
        virtual void Deserialize(const nlohmann::json& data) = 0;

        // Callbacks et événements
        using SignalCallback = std::function<void(const SSignal&)>;
        using PositionCallback = std::function<void(const SPosition&)>;
        using ErrorCallback = std::function<void(const std::string&)>;

        virtual void SetSignalCallback(SignalCallback callback) = 0;
        virtual void SetPositionCallback(PositionCallback callback) = 0;
        virtual void SetErrorCallback(ErrorCallback callback) = 0;

        // Utilitaires
        virtual bool IsSymbolSupported(const std::string& symbol) const = 0;
        virtual std::vector<std::string> GetSupportedSymbols() const = 0;
        virtual std::vector<std::string> GetRequiredIndicators() const = 0;
        virtual std::chrono::duration<double> GetLastExecutionTime() const = 0;
    };

    // Classe de base pour faciliter l'implémentation
    class CBaseStrategy : public IStrategy {
    public:
        CBaseStrategy(const std::string& name, EStrategyType type);
        virtual ~CBaseStrategy() = default;

        // Implémentations par défaut
        std::string GetName() const override { return mName; }
        std::string GetDescription() const override { return mDescription; }
        std::string GetVersion() const override { return mVersion; }
        EStrategyType GetType() const override { return mType; }
        EStrategyState GetState() const override { return mState; }

        void SetExchangeAPI(std::shared_ptr<API::IExchangeAPI> api) override { mExchangeAPI = api; }
        std::shared_ptr<API::IExchangeAPI> GetExchangeAPI() const override { return mExchangeAPI; }

        void OnError(const std::string& error) override;
        std::vector<std::string> GetErrors() const override { return mErrors; }
        void ClearErrors() override { mErrors.clear(); }

        void SetSignalCallback(SignalCallback callback) override { mSignalCallback = callback; }
        void SetPositionCallback(PositionCallback callback) override { mPositionCallback = callback; }
        void SetErrorCallback(ErrorCallback callback) override { mErrorCallback = callback; }

        std::chrono::duration<double> GetLastExecutionTime() const override { return mLastExecutionTime; }

    protected:
        std::string mName;
        std::string mDescription;
        std::string mVersion = "1.0.0";
        EStrategyType mType;
        EStrategyState mState = EStrategyState::INACTIVE;

        std::shared_ptr<API::IExchangeAPI> mExchangeAPI;
        std::vector<std::string> mErrors;
        std::chrono::duration<double> mLastExecutionTime;

        SignalCallback mSignalCallback;
        PositionCallback mPositionCallback;
        ErrorCallback mErrorCallback;

        // Méthodes d'aide
        void NotifySignal(const SSignal& signal);
        void NotifyPosition(const SPosition& position);
        void NotifyError(const std::string& error);
    };

} // namespace Strategy

#endif //ISTRATEGY_H