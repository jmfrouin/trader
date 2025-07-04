
//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#ifndef RSISTRATEGY_H
#define RSISTRATEGY_H

#include "IStrategy.h"
#include <deque>
#include <chrono>
#include <atomic>

namespace Strategy {

    // Structure pour les paramètres RSI
    struct SRSIParams {
        int mRSIPeriod = 14;
        double mOversoldThreshold = 30.0;
        double mOverboughtThreshold = 70.0;
        double mExtremeOversold = 20.0;
        double mExtremeOverbought = 80.0;
        double mPositionSize = 0.1;
        double mStopLossPercent = 2.0;
        double mTakeProfitPercent = 4.0;
        int mMinPeriods = 20;
        bool mUseDivergence = true;
        bool mUseMultiTimeframe = false;
        double mRSIChangeThreshold = 5.0;
    };

    // Structure pour les valeurs RSI calculées
    struct SRSIValues {
        double mRSI = 50.0;
        double mPreviousRSI = 50.0;
        double mRSIChange = 0.0;
        double mAverageGain = 0.0;
        double mAverageLoss = 0.0;
        std::chrono::system_clock::time_point mTimestamp;
        bool mIsValid = false;
        int mPeriodCount = 0;
    };

    // Énumération pour les zones RSI
    enum class ERSIZone {
        EXTREME_OVERSOLD,   // RSI < 20
        OVERSOLD,           // RSI 20-30
        NEUTRAL_LOW,        // RSI 30-50
        NEUTRAL_HIGH,       // RSI 50-70
        OVERBOUGHT,         // RSI 70-80
        EXTREME_OVERBOUGHT  // RSI > 80
    };

    // Énumération pour les types de signaux RSI
    enum class ERSISignalType {
        NONE,
        BUY_OVERSOLD,           // Achat en zone de survente
        SELL_OVERBOUGHT,        // Vente en zone de surachat
        BUY_OVERSOLD_EXIT,      // Sortie de zone de survente (haussier)
        SELL_OVERBOUGHT_EXIT,   // Sortie de zone de surachat (baissier)
        DIVERGENCE_BULLISH,     // Divergence haussière
        DIVERGENCE_BEARISH,     // Divergence baissière
        MOMENTUM_BULLISH,       // Momentum haussier
        MOMENTUM_BEARISH,       // Momentum baissier
        EXTREME_REVERSAL_BUY,   // Retournement depuis zone extrême (achat)
        EXTREME_REVERSAL_SELL   // Retournement depuis zone extrême (vente)
    };

    // Structure pour l'historique des signaux RSI
    struct SRSISignalHistory {
        ERSISignalType mType;
        SRSIValues mValues;
        ERSIZone mZone;
        double mPrice;
        std::chrono::system_clock::time_point mTimestamp;
        std::string mDescription;
        double mStrength; // Force du signal (0-1)
    };

    // Structure pour la détection de divergence
    struct SRSIDivergence {
        bool mIsBullish = false;
        bool mIsBearish = false;
        double mPriceHigh = 0.0;
        double mPriceLow = 0.0;
        double mRSIHigh = 0.0;
        double mRSILow = 0.0;
        int mPeriodsSpan = 0;
        double mStrength = 0.0;
        std::chrono::system_clock::time_point mDetectedAt;
    };

    // Classe principale de la stratégie RSI
    class CRSIStrategy : public CBaseStrategy {
    public:
        CRSIStrategy();
        explicit CRSIStrategy(const SRSIParams& params);
        ~CRSIStrategy() override = default;

        // Configuration de la stratégie
        void Configure(const nlohmann::json& config) override;
        nlohmann::json GetDefaultConfig() const override;
        nlohmann::json GetCurrentConfig() const override;
        void SetConfig(const SStrategyConfig& config) override;
        SStrategyConfig GetConfig() const override;

        // Informations de base
        std::string GetDescription() const override { return "Relative Strength Index Strategy"; }
        EStrategyType GetType() const override { return EStrategyType::MEAN_REVERSION; }

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

        // Méthodes spécifiques à RSI
        void SetRSIParams(const SRSIParams& params);
        SRSIParams GetRSIParams() const;
        SRSIValues GetCurrentRSIValues() const;
        std::vector<SRSIValues> GetRSIHistory(size_t count = 100) const;
        std::vector<SRSISignalHistory> GetSignalHistory(size_t count = 50) const;

        // Configuration avancée
        void SetOversoldThreshold(double threshold);
        void SetOverboughtThreshold(double threshold);
        void SetDivergenceDetection(bool enable);
        void SetMultiTimeframeAnalysis(bool enable);

        // Analyse technique RSI
        ERSIZone GetCurrentRSIZone() const;
        ERSISignalType AnalyzeRSISignal(const SRSIValues& current, const SRSIValues& previous) const;
        SRSIDivergence DetectDivergence(size_t lookbackPeriods = 20) const;
        double GetRSIMomentum(size_t periods = 3) const;
        double GetSignalStrength(ERSISignalType signalType, const SRSIValues& values) const;

        // Zones et niveaux
        bool IsInOversoldZone(double rsi = -1.0) const;
        bool IsInOverboughtZone(double rsi = -1.0) const;
        bool IsInExtremeZone(double rsi = -1.0) const;
        bool IsExitingOversold() const;
        bool IsExitingOverbought() const;

    private:
        // Configuration
        SRSIParams mParams;
        SStrategyConfig mConfig;
        std::atomic<bool> mIsInitialized{false};

        // État de la stratégie
        std::deque<double> mClosePrices;
        std::deque<double> mGains;
        std::deque<double> mLosses;
        std::deque<SRSIValues> mRSIHistory;
        std::deque<SRSISignalHistory> mSignalHistory;
        bool mInPosition = false;
        API::EOrderSide mCurrentPositionSide = API::EOrderSide::BUY;
        std::string mCurrentPositionId;
        
        // Valeurs RSI courantes
        SRSIValues mCurrentRSIValues;
        SRSIValues mPreviousRSIValues;
        ERSIZone mCurrentZone = ERSIZone::NEUTRAL_LOW;
        ERSIZone mPreviousZone = ERSIZone::NEUTRAL_LOW;
        
        // Configuration avancée
        bool mDivergenceDetectionEnabled = true;
        bool mMultiTimeframeEnabled = false;
        double mMinRSIChange = 5.0;
        int mDivergenceLookback = 20;

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
        
        // Statistiques des signaux RSI
        std::map<ERSISignalType, int> mSignalCounts;
        std::map<ERSISignalType, double> mSignalSuccessRates;
        std::map<ERSIZone, int> mZoneTimeSpent;
        int mOversoldEntries = 0;
        int mOverboughtEntries = 0;
        int mDivergenceSignals = 0;

        // Synchronisation
        mutable std::mutex mDataMutex;
        mutable std::mutex mPositionMutex;

        // Méthodes de calcul privées
        double CalculateRSI(const std::deque<double>& prices, int period);
        void CalculateRSIValues(const std::deque<double>& prices, SRSIValues& outValues);
        double CalculateWildersSmoothing(const std::deque<double>& values, int period);
        void UpdateGainsAndLosses(double currentPrice, double previousPrice);
        
        // Analyse des zones et signaux
        ERSIZone DetermineRSIZone(double rsi) const;
        ERSISignalType DetectZoneTransition(ERSIZone currentZone, ERSIZone previousZone, const SRSIValues& values) const;
        ERSISignalType DetectMomentumSignal(const SRSIValues& current, const SRSIValues& previous) const;
        bool IsRSIReversing(size_t periods = 3) const;
        
        // Détection de divergence
        SRSIDivergence AnalyzeDivergence(const std::deque<double>& prices, const std::deque<SRSIValues>& rsiHistory, size_t lookback) const;
        bool FindPriceHighsAndLows(const std::deque<double>& prices, size_t lookback, std::vector<size_t>& highs, std::vector<size_t>& lows) const;
        bool FindRSIHighsAndLows(const std::deque<SRSIValues>& rsiHistory, size_t lookback, std::vector<size_t>& highs, std::vector<size_t>& lows) const;
        double CalculateDivergenceStrength(const SRSIDivergence& divergence) const;
        
        // Gestion des données
        void UpdateClosePrices(const std::vector<API::SKline>& klines);
        void UpdateRSIHistory();
        void AddSignalToHistory(ERSISignalType signalType, const SRSIValues& values, double price, const std::string& description = "");
        void CleanupOldData();
        
        // Validation et contrôles
        bool HasSufficientData() const;
        bool IsValidRSIValues(const SRSIValues& values) const;
        bool ShouldGenerateSignal(ERSISignalType signalType) const;
        bool IsSignalFilterPassed(ERSISignalType signalType, const SRSIValues& values) const;
        
        // Gestion des positions
        void UpdatePositionState(const SPosition& position);
        double CalculateStopLoss(double entryPrice, API::EOrderSide side) const;
        double CalculateTakeProfit(double entryPrice, API::EOrderSide side) const;
        bool ShouldClosePosition(const SRSIValues& values) const;
        
        // Utilitaires
        std::string SignalTypeToString(ERSISignalType type) const;
        std::string ZoneToString(ERSIZone zone) const;
        ERSISignalType StringToSignalType(const std::string& typeStr) const;
        ERSIZone StringToZone(const std::string& zoneStr) const;
        void LogSignal(ERSISignalType signalType, const SRSIValues& values, double price) const;
        
        // Métriques
        void UpdateSignalStatistics(ERSISignalType signalType, bool successful);
        void UpdateZoneStatistics(ERSIZone zone);
        void CalculateAdvancedMetrics();
        void ResetMetrics();
    };

    // Factory pour créer des stratégies RSI préconfigurées
    class CRSIStrategyFactory {
    public:
        // Stratégies préconfigurées
        static std::shared_ptr<CRSIStrategy> CreateDefault();
        static std::shared_ptr<CRSIStrategy> CreateScalping();
        static std::shared_ptr<CRSIStrategy> CreateSwing();
        static std::shared_ptr<CRSIStrategy> CreateConservative();
        static std::shared_ptr<CRSIStrategy> CreateAggressive();
        static std::shared_ptr<CRSIStrategy> CreateMeanReversion();
        static std::shared_ptr<CRSIStrategy> CreateDivergenceHunter();
        
        // Configuration personnalisée
        static std::shared_ptr<CRSIStrategy> CreateCustom(const SRSIParams& params);
        static std::shared_ptr<CRSIStrategy> CreateFromConfig(const nlohmann::json& config);
        
        // Méthodes d'aide pour les paramètres
        static SRSIParams GetDefaultParams();
        static SRSIParams GetScalpingParams();
        static SRSIParams GetSwingParams();
        static SRSIParams GetConservativeParams();
        static SRSIParams GetAggressiveParams();
        static SRSIParams GetMeanReversionParams();
        static SRSIParams GetDivergenceParams();
        
        // Paramètres spécialisés
        static SRSIParams GetCryptoParams();
        static SRSIParams GetForexParams();
        static SRSIParams GetStockParams();
    };

} // namespace Strategy

#endif //RSISTRATEGY_H