
//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#ifndef MACDSTRATEGY_H
#define MACDSTRATEGY_H

#include "IStrategy.h"
#include <deque>
#include <chrono>
#include <atomic>

namespace Strategy {

    // Structure pour les paramètres MACD
    struct SMACDParams {
        int mFastEMA = 12;
        int mSlowEMA = 26;
        int mSignalPeriod = 9;
        double mPositionSize = 0.1;
        double mStopLossPercent = 2.0;
        double mTakeProfitPercent = 4.0;
        int mMinPeriods = 50;
        double mMinHistogramChange = 0.0001;
        bool mUseHistogramCrossover = true;
        bool mUseZeroCrossover = true;
    };

    // Structure pour les valeurs MACD calculées
    struct SMACDValues {
        double mMACD = 0.0;
        double mSignal = 0.0;
        double mHistogram = 0.0;
        std::chrono::system_clock::time_point mTimestamp;
        bool mIsValid = false;
    };

    // Énumération pour les types de signaux MACD
    enum class EMACDSignalType {
        NONE,
        BULLISH_CROSSOVER,      // MACD croise au-dessus de la ligne de signal
        BEARISH_CROSSOVER,      // MACD croise en-dessous de la ligne de signal
        BULLISH_ZERO_CROSS,     // MACD croise au-dessus de zéro
        BEARISH_ZERO_CROSS,     // MACD croise en-dessous de zéro
        DIVERGENCE_BULLISH,     // Divergence haussière
        DIVERGENCE_BEARISH      // Divergence baissière
    };

    // Structure pour l'historique des signaux
    struct SMACDSignalHistory {
        EMACDSignalType mType;
        SMACDValues mValues;
        double mPrice;
        std::chrono::system_clock::time_point mTimestamp;
        std::string mDescription;
    };

    // Classe principale de la stratégie MACD
    class CMACDStrategy : public CBaseStrategy {
    public:
        CMACDStrategy();
        explicit CMACDStrategy(const SMACDParams& params);
        ~CMACDStrategy() override = default;

        // Configuration de la stratégie
        void Configure(const nlohmann::json& config) override;
        nlohmann::json GetDefaultConfig() const override;
        nlohmann::json GetCurrentConfig() const override;
        void SetConfig(const SStrategyConfig& config) override;
        SStrategyConfig GetConfig() const override;

        // Informations de base
        std::string GetDescription() const override { return "Moving Average Convergence Divergence Strategy"; }
        EStrategyType GetType() const override { return EStrategyType::MOMENTUM; }

        // Initialisation et nettoyage
        void Initialize() override;
        void Shutdown() override;
        void Reset() override;

        // Gestion de l'état
        void Start() override;
        void Stop() override;
        void Pause() override;
        void Resume() override;

        // Mise à jour des données et génération du signal
        SSignal Update(const std::vector<API::SKline>& klines, const API::STicker& ticker) override;
        std::vector<SSignal> ProcessMarketData(const std::vector<API::SKline>& klines, const API::STicker& ticker) override;

        // Gestion des positions
        void OnPositionOpened(const SPosition& position) override;
        void OnPositionClosed(const SPosition& position, double exitPrice, double pnl) override;
        void OnPositionUpdated(const SPosition& position) override;

        // Gestion des ordres
        void OnOrderFilled(const std::string& orderId, const SPosition& position) override;
        void OnOrderCanceled(const std::string& orderId, const std::string& reason) override;
        void OnOrderRejected(const std::string& orderId, const std::string& reason) override;

        // Métriques et statistiques
        SStrategyMetrics GetMetrics() const override;
        std::map<std::string, double> GetCustomMetrics() const override;
        void UpdateMetrics(const SPosition& position, double pnl) override;

        // Validation et tests
        bool ValidateSignal(const SSignal& signal) const override;
        bool CanTrade(const std::string& symbol) const override;
        double CalculatePositionSize(const std::string& symbol, double price, double availableBalance) const override;

        // Sérialisation
        nlohmann::json Serialize() const override;
        void Deserialize(const nlohmann::json& data) override;

        // Utilitaires
        bool IsSymbolSupported(const std::string& symbol) const override;
        std::vector<std::string> GetSupportedSymbols() const override;
        std::vector<std::string> GetRequiredIndicators() const override;

        // Méthodes spécifiques à MACD
        void SetMACDParams(const SMACDParams& params);
        SMACDParams GetMACDParams() const;
        SMACDValues GetCurrentMACDValues() const;
        std::vector<SMACDValues> GetMACDHistory(size_t count = 100) const;
        std::vector<SMACDSignalHistory> GetSignalHistory(size_t count = 50) const;

        // Configuration avancée
        void SetMinHistogramChange(double minChange);
        void SetUseHistogramCrossover(bool use);
        void SetUseZeroCrossover(bool use);
        void SetDivergenceDetection(bool enable);

        // Analyse technique
        EMACDSignalType AnalyzeMACDSignal(const SMACDValues& current, const SMACDValues& previous) const;
        bool DetectDivergence(const std::deque<double>& prices, const std::deque<SMACDValues>& macdHistory) const;
        double GetMACDStrength() const;
        double GetTrendStrength() const;

    private:
        // Configuration
        SMACDParams mParams;
        SStrategyConfig mConfig;
        std::atomic<bool> mIsInitialized{false};

        // État de la stratégie
        std::deque<double> mClosePrices;
        std::deque<SMACDValues> mMACDHistory;
        std::deque<SMACDSignalHistory> mSignalHistory;
        bool mInPosition = false;
        API::EOrderSide mCurrentPositionSide = API::EOrderSide::BUY;
        std::string mCurrentPositionId;

        // Valeurs MACD courantes
        SMACDValues mCurrentMACDValues;
        SMACDValues mPreviousMACDValues;

        // Configuration avancée
        bool mDivergenceDetectionEnabled = false;
        double mMinHistogramChange = 0.0001;
        bool mUseHistogramCrossover = true;
        bool mUseZeroCrossover = true;

        // Métriques personnalisées
        mutable std::mutex mMetricsMutex;
        int mTotalTrades = 0;
        int mWinningTrades = 0;
        double mTotalPnL = 0.0;
        double mMaxDrawdown = 0.0;
        double mPeakBalance = 0.0;
        double mCurrentBalance = 0.0;
        int mConsecutiveWins = 0;
        int mConsecutiveLosses = 0;
        int mMaxConsecutiveWins = 0;
        int mMaxConsecutiveLosses = 0;

        // Statistiques des signaux
        std::map<EMACDSignalType, int> mSignalCounts;
        std::map<EMACDSignalType, double> mSignalSuccessRates;

        // Synchronisation
        mutable std::mutex mDataMutex;
        mutable std::mutex mPositionMutex;

        // Méthodes de calcul privées
        double CalculateEMA(const std::deque<double>& prices, int period) const;
        void CalculateMACD(const std::deque<double>& prices, SMACDValues& outValues) const;
        double CalculateEMAFromPrevious(double currentPrice, double previousEMA, int period) const;

        // Analyse des signaux
        EMACDSignalType DetectCrossover(const SMACDValues& current, const SMACDValues& previous) const;
        bool IsHistogramIncreasing(size_t periods = 3) const;
        bool IsHistogramDecreasing(size_t periods = 3) const;
        double CalculateHistogramMomentum() const;

        // Gestion des données
        void UpdateClosePrices(const std::vector<API::SKline>& klines);
        void UpdateMACDHistory();
        void AddSignalToHistory(EMACDSignalType signalType, const SMACDValues& values, double price, const std::string& description = "");
        void CleanupOldData();

        // Validation et contrôles
        bool HasSufficientData() const;
        bool IsValidMACDValues(const SMACDValues& values) const;
        bool ShouldGenerateSignal(EMACDSignalType signalType) const;

        // Gestion des positions
        void UpdatePositionState(const SPosition& position);
        double CalculateStopLoss(double entryPrice, API::EOrderSide side) const;
        double CalculateTakeProfit(double entryPrice, API::EOrderSide side) const;

        // Utilitaires
        std::string SignalTypeToString(EMACDSignalType type) const;
        EMACDSignalType StringToSignalType(const std::string& typeStr) const;
        void LogSignal(EMACDSignalType signalType, const SMACDValues& values, double price) const;

        // Métriques
        void UpdateSignalStatistics(EMACDSignalType signalType, bool successful);
        void CalculateAdvancedMetrics();
        void ResetMetrics();
    };

    // Factory pour créer des stratégies MACD préconfigurées
    class CMACDStrategyFactory {
    public:
        // Stratégies préconfigurées
        static std::shared_ptr<CMACDStrategy> CreateDefault();
        static std::shared_ptr<CMACDStrategy> CreateScalping();
        static std::shared_ptr<CMACDStrategy> CreateSwing();
        static std::shared_ptr<CMACDStrategy> CreateConservative();
        static std::shared_ptr<CMACDStrategy> CreateAggressive();

        // Configuration personnalisée
        static std::shared_ptr<CMACDStrategy> CreateCustom(const SMACDParams& params);
        static std::shared_ptr<CMACDStrategy> CreateFromConfig(const nlohmann::json& config);

        // Méthodes d'aide
        static SMACDParams GetDefaultParams();
        static SMACDParams GetScalpingParams();
        static SMACDParams GetSwingParams();
        static SMACDParams GetConservativeParams();
        static SMACDParams GetAggressiveParams();
    };

} // namespace Strategy

#endif //MACDSTRATEGY_H