
//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#ifndef SMASTRATEGY_H
#define SMASTRATEGY_H

#include "IStrategy.h"
#include <deque>
#include <chrono>
#include <atomic>

namespace Strategy {

    // Structure pour les paramètres SMA
    struct SSMAParams {
        int mFastPeriod = 10;
        int mSlowPeriod = 20;
        int mLongPeriod = 50;        // Pour les stratégies à 3 moyennes
        double mPositionSize = 0.1;
        double mStopLossPercent = 2.0;
        double mTakeProfitPercent = 4.0;
        int mMinPeriods = 25;
        bool mUseTripleMA = false;    // Utiliser 3 moyennes mobiles
        bool mUseSlopeFilter = true;  // Filtrer par la pente
        double mMinSlope = 0.001;     // Pente minimale pour valider le signal
        bool mUseVolumeFilter = false; // Filtrer par le volume
        double mVolumeThreshold = 1.5; // Multiplicateur de volume moyen
    };

    // Structure pour les valeurs SMA calculées
    struct SSMAValues {
        double mFastSMA = 0.0;
        double mSlowSMA = 0.0;
        double mLongSMA = 0.0;
        double mFastSlope = 0.0;      // Pente de la moyenne rapide
        double mSlowSlope = 0.0;      // Pente de la moyenne lente
        double mLongSlope = 0.0;      // Pente de la moyenne longue
        double mSpread = 0.0;         // Écart entre moyennes rapide et lente
        double mSpreadPercent = 0.0;  // Écart en pourcentage
        std::chrono::system_clock::time_point mTimestamp;
        bool mIsValid = false;
        int mPeriodCount = 0;
    };

    // Énumération pour les configurations de moyennes mobiles
    enum class ESMAConfiguration {
        DUAL_MA,        // 2 moyennes mobiles (rapide/lente)
        TRIPLE_MA,      // 3 moyennes mobiles (rapide/lente/longue)
        SINGLE_MA_PRICE // 1 moyenne mobile vs prix
    };

    // Énumération pour les types de signaux SMA
    enum class ESMASignalType {
        NONE,
        GOLDEN_CROSS,           // Croisement haussier (rapide > lente)
        DEATH_CROSS,            // Croisement baissier (rapide < lente)
        PRICE_ABOVE_MA,         // Prix au-dessus de la moyenne
        PRICE_BELOW_MA,         // Prix en-dessous de la moyenne
        TREND_ACCELERATION,     // Accélération de tendance
        TREND_DECELERATION,     // Décélération de tendance
        PULLBACK_BUY,          // Pullback haussier
        PULLBACK_SELL,         // Pullback baissier
        TRIPLE_ALIGNMENT_BULL,  // Alignement haussier des 3 MA
        TRIPLE_ALIGNMENT_BEAR,  // Alignement baissier des 3 MA
        CONVERGENCE,           // Convergence des moyennes
        DIVERGENCE             // Divergence des moyennes
    };

    // Énumération pour la tendance
    enum class ESMATrend {
        STRONG_UPTREND,     // Tendance haussière forte
        WEAK_UPTREND,       // Tendance haussière faible
        SIDEWAYS,           // Marché latéral
        WEAK_DOWNTREND,     // Tendance baissière faible
        STRONG_DOWNTREND    // Tendance baissière forte
    };

    // Structure pour l'historique des signaux SMA
    struct SSMASignalHistory {
        ESMASignalType mType;
        SSMAValues mValues;
        ESMATrend mTrend;
        double mPrice;
        double mVolume;
        std::chrono::system_clock::time_point mTimestamp;
        std::string mDescription;
        double mStrength; // Force du signal (0-1)
    };

    // Structure pour l'analyse de tendance
    struct SSMATrendAnalysis {
        ESMATrend mCurrentTrend = ESMATrend::SIDEWAYS;
        ESMATrend mPreviousTrend = ESMATrend::SIDEWAYS;
        double mTrendStrength = 0.0;        // Force de la tendance (0-1)
        double mTrendDuration = 0.0;        // Durée en periods
        bool mIsTrendChanging = false;      // Changement de tendance
        double mSupportLevel = 0.0;         // Niveau de support
        double mResistanceLevel = 0.0;      // Niveau de résistance
        std::chrono::system_clock::time_point mTrendStartTime;
    };

    // Classe principale de la stratégie SMA
    class CSMAStrategy : public CBaseStrategy {
    public:
        CSMAStrategy();
        explicit CSMAStrategy(const SSMAParams& params);
        ~CSMAStrategy() override = default;

        // Configuration de la stratégie
        void Configure(const nlohmann::json& config) override;
        nlohmann::json GetDefaultConfig() const override;
        nlohmann::json GetCurrentConfig() const override;
        void SetConfig(const SStrategyConfig& config) override;
        SStrategyConfig GetConfig() const override;

        // Informations de base
        std::string GetDescription() const override { return "Simple Moving Average Strategy"; }
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

        // Méthodes spécifiques à SMA
        void SetSMAParams(const SSMAParams& params);
        SSMAParams GetSMAParams() const;
        SSMAValues GetCurrentSMAValues() const;
        std::vector<SSMAValues> GetSMAHistory(size_t count = 100) const;
        std::vector<SSMASignalHistory> GetSignalHistory(size_t count = 50) const;

        // Configuration avancée
        void SetConfiguration(ESMAConfiguration config);
        void SetSlopeFilter(bool enable, double minSlope = 0.001);
        void SetVolumeFilter(bool enable, double threshold = 1.5);
        void SetTripleMAMode(bool enable);

        // Analyse technique SMA
        ESMATrend GetCurrentTrend() const;
        SSMATrendAnalysis GetTrendAnalysis() const;
        ESMASignalType AnalyzeSMASignal(const SSMAValues& current, const SSMAValues& previous) const;
        double GetTrendStrength() const;
        double GetSignalStrength(ESMASignalType signalType, const SSMAValues& values) const;

        // Détection de configurations
        bool IsGoldenCross(const SSMAValues& current, const SSMAValues& previous) const;
        bool IsDeathCross(const SSMAValues& current, const SSMAValues& previous) const;
        bool IsTripleAlignment(bool bullish = true) const;
        bool IsPullbackOpportunity(bool bullish = true) const;

        // Niveaux de support/résistance
        double GetDynamicSupport() const;
        double GetDynamicResistance() const;
        std::vector<double> GetSMALevels() const;

    private:
        // Configuration
        SSMAParams mParams;
        SStrategyConfig mConfig;
        ESMAConfiguration mConfiguration = ESMAConfiguration::DUAL_MA;
        std::atomic<bool> mIsInitialized{false};

        // État de la stratégie
        std::deque<double> mClosePrices;
        std::deque<double> mVolumes;
        std::deque<SSMAValues> mSMAHistory;
        std::deque<SSMASignalHistory> mSignalHistory;
        bool mInPosition = false;
        API::EOrderSide mCurrentPositionSide = API::EOrderSide::BUY;
        std::string mCurrentPositionId;

        // Valeurs SMA courantes
        SSMAValues mCurrentSMAValues;
        SSMAValues mPreviousSMAValues;
        SSMATrendAnalysis mTrendAnalysis;

        // Configuration avancée
        bool mSlopeFilterEnabled = true;
        double mMinSlope = 0.001;
        bool mVolumeFilterEnabled = false;
        double mVolumeThreshold = 1.5;
        bool mTripleModeEnabled = false;

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

        // Statistiques des signaux SMA
        std::map<ESMASignalType, int> mSignalCounts;
        std::map<ESMASignalType, double> mSignalSuccessRates;
        std::map<ESMATrend, int> mTrendTimeSpent;
        int mGoldenCrosses = 0;
        int mDeathCrosses = 0;
        int mTrendChanges = 0;

        // Synchronisation
        mutable std::mutex mDataMutex;
        mutable std::mutex mPositionMutex;

        // Méthodes de calcul privées
        double CalculateSMA(const std::deque<double>& prices, int period) const;
        void CalculateSMAValues(const std::deque<double>& prices, SSMAValues& outValues);
        double CalculateSlope(const std::deque<double>& values, int period = 3) const;
        double CalculateSpread(double fastSMA, double slowSMA) const;
        double CalculateAverageVolume(size_t periods = 20) const;

        // Analyse de tendance
        ESMATrend DetermineTrend(const SSMAValues& values) const;
        double CalculateTrendStrength(const SSMAValues& values) const;
        void UpdateTrendAnalysis();
        bool IsTrendChanging(ESMATrend newTrend) const;

        // Détection de signaux
        ESMASignalType DetectCrossover(const SSMAValues& current, const SSMAValues& previous) const;
        ESMASignalType DetectPriceMARelation(double price, const SSMAValues& values) const;
        ESMASignalType DetectTrendSignals(const SSMAValues& current, const SSMAValues& previous) const;
        ESMASignalType DetectTripleMASignals(const SSMAValues& current, const SSMAValues& previous) const;
        bool IsVolumeConfirmed(double currentVolume) const;

        // Gestion des données
        void UpdateClosePrices(const std::vector<API::SKline>& klines);
        void UpdateVolumes(const std::vector<API::SKline>& klines);
        void UpdateSMAHistory();
        void AddSignalToHistory(ESMASignalType signalType, const SSMAValues& values, double price, double volume, const std::string& description = "");
        void CleanupOldData();

        // Validation et contrôles
        bool HasSufficientData() const;
        bool IsValidSMAValues(const SSMAValues& values) const;
        bool ShouldGenerateSignal(ESMASignalType signalType) const;
        bool PassesSlopeFilter(const SSMAValues& values) const;
        bool PassesVolumeFilter(double volume) const;

        // Support et résistance
        void UpdateSupportResistanceLevels();
        double FindNearestSMALevel(double price) const;
        bool IsPriceNearMA(double price, double ma, double tolerance = 0.5) const;

        // Gestion des positions
        void UpdatePositionState(const SPosition& position);
        double CalculateStopLoss(double entryPrice, API::EOrderSide side) const;
        double CalculateTakeProfit(double entryPrice, API::EOrderSide side) const;
        bool ShouldClosePosition(const SSMAValues& values) const;

        // Utilitaires
        std::string SignalTypeToString(ESMASignalType type) const;
        std::string TrendToString(ESMATrend trend) const;
        std::string ConfigurationToString(ESMAConfiguration config) const;
        ESMASignalType StringToSignalType(const std::string& typeStr) const;
        ESMATrend StringToTrend(const std::string& trendStr) const;
        void LogSignal(ESMASignalType signalType, const SSMAValues& values, double price) const;

        // Métriques
        void UpdateSignalStatistics(ESMASignalType signalType, bool successful);
        void UpdateTrendStatistics(ESMATrend trend);
        void CalculateAdvancedMetrics();
        void ResetMetrics();
    };

    // Factory pour créer des stratégies SMA préconfigurées
    class CSMAStrategyFactory {
    public:
        // Stratégies préconfigurées
        static std::shared_ptr<CSMAStrategy> CreateDefault();
        static std::shared_ptr<CSMAStrategy> CreateScalping();
        static std::shared_ptr<CSMAStrategy> CreateSwing();
        static std::shared_ptr<CSMAStrategy> CreateTrend();
        static std::shared_ptr<CSMAStrategy> CreateBreakout();
        static std::shared_ptr<CSMAStrategy> CreatePullback();
        static std::shared_ptr<CSMAStrategy> CreateTripleMA();

        // Configuration personnalisée
        static std::shared_ptr<CSMAStrategy> CreateCustom(const SSMAParams& params);
        static std::shared_ptr<CSMAStrategy> CreateFromConfig(const nlohmann::json& config);

        // Méthodes d'aide pour les paramètres
        static SSMAParams GetDefaultParams();
        static SSMAParams GetScalpingParams();
        static SSMAParams GetSwingParams();
        static SSMAParams GetTrendParams();
        static SSMAParams GetBreakoutParams();
        static SSMAParams GetPullbackParams();
        static SSMAParams GetTripleMAParams();

        // Paramètres spécialisés par marché
        static SSMAParams GetCryptoParams();
        static SSMAParams GetForexParams();
        static SSMAParams GetStockParams();
        static SSMAParams GetCommodityParams();

        // Configurations par timeframe
        static SSMAParams GetIntraday();
        static SSMAParams GetDaily();
        static SSMAParams GetWeekly();
    };

} // namespace Strategy

#endif //SMASTRATEGY_H