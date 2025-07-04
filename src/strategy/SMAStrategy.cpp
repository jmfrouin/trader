//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#include "SMAStrategy.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <iomanip>

namespace Strategy {

// ============================================================================
// CSMAStrategy - Constructeurs et destructeur
// ============================================================================

CSMAStrategy::CSMAStrategy()
    : CBaseStrategy()
    , mParams()
    , mConfiguration(ESMAConfiguration::DUAL_MA)
    , mIsInitialized(false) {

    SetName("SMA Strategy");
    mParams = SSMAParams(); // Utilise les valeurs par défaut
}

CSMAStrategy::CSMAStrategy(const SSMAParams& params)
    : CBaseStrategy()
    , mParams(params)
    , mConfiguration(params.mUseTripleMA ? ESMAConfiguration::TRIPLE_MA : ESMAConfiguration::DUAL_MA)
    , mIsInitialized(false) {

    SetName("SMA Strategy");
}

// ============================================================================
// Configuration de la stratégie
// ============================================================================

void CSMAStrategy::Configure(const nlohmann::json& config) {
    std::lock_guard<std::mutex> guard(mDataMutex);

    try {
        if (config.contains("fastPeriod")) {
            mParams.mFastPeriod = config["fastPeriod"];
        }
        if (config.contains("slowPeriod")) {
            mParams.mSlowPeriod = config["slowPeriod"];
        }
        if (config.contains("longPeriod")) {
            mParams.mLongPeriod = config["longPeriod"];
        }
        if (config.contains("positionSize")) {
            mParams.mPositionSize = config["positionSize"];
        }
        if (config.contains("stopLossPercent")) {
            mParams.mStopLossPercent = config["stopLossPercent"];
        }
        if (config.contains("takeProfitPercent")) {
            mParams.mTakeProfitPercent = config["takeProfitPercent"];
        }
        if (config.contains("useTripleMA")) {
            mParams.mUseTripleMA = config["useTripleMA"];
            mConfiguration = mParams.mUseTripleMA ? ESMAConfiguration::TRIPLE_MA : ESMAConfiguration::DUAL_MA;
        }
        if (config.contains("useSlopeFilter")) {
            mParams.mUseSlopeFilter = config["useSlopeFilter"];
            mSlopeFilterEnabled = mParams.mUseSlopeFilter;
        }
        if (config.contains("minSlope")) {
            mParams.mMinSlope = config["minSlope"];
            mMinSlope = mParams.mMinSlope;
        }
        if (config.contains("useVolumeFilter")) {
            mParams.mUseVolumeFilter = config["useVolumeFilter"];
            mVolumeFilterEnabled = mParams.mUseVolumeFilter;
        }
        if (config.contains("volumeThreshold")) {
            mParams.mVolumeThreshold = config["volumeThreshold"];
            mVolumeThreshold = mParams.mVolumeThreshold;
        }

        // Validation des paramètres
        if (mParams.mFastPeriod >= mParams.mSlowPeriod) {
            throw std::invalid_argument("Fast period must be less than slow period");
        }
        if (mParams.mSlowPeriod >= mParams.mLongPeriod && mParams.mUseTripleMA) {
            throw std::invalid_argument("Slow period must be less than long period");
        }

    } catch (const std::exception& e) {
        throw std::runtime_error("SMA configuration error: " + std::string(e.what()));
    }
}

nlohmann::json CSMAStrategy::GetDefaultConfig() const {
    nlohmann::json config;
    config["fastPeriod"] = 10;
    config["slowPeriod"] = 20;
    config["longPeriod"] = 50;
    config["positionSize"] = 0.1;
    config["stopLossPercent"] = 2.0;
    config["takeProfitPercent"] = 4.0;
    config["useTripleMA"] = false;
    config["useSlopeFilter"] = true;
    config["minSlope"] = 0.001;
    config["useVolumeFilter"] = false;
    config["volumeThreshold"] = 1.5;
    return config;
}

nlohmann::json CSMAStrategy::GetCurrentConfig() const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    nlohmann::json config;
    config["fastPeriod"] = mParams.mFastPeriod;
    config["slowPeriod"] = mParams.mSlowPeriod;
    config["longPeriod"] = mParams.mLongPeriod;
    config["positionSize"] = mParams.mPositionSize;
    config["stopLossPercent"] = mParams.mStopLossPercent;
    config["takeProfitPercent"] = mParams.mTakeProfitPercent;
    config["useTripleMA"] = mParams.mUseTripleMA;
    config["useSlopeFilter"] = mParams.mUseSlopeFilter;
    config["minSlope"] = mParams.mMinSlope;
    config["useVolumeFilter"] = mParams.mUseVolumeFilter;
    config["volumeThreshold"] = mParams.mVolumeThreshold;
    return config;
}

void CSMAStrategy::SetConfig(const SStrategyConfig& config) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mConfig = config;
}

SStrategyConfig CSMAStrategy::GetConfig() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mConfig;
}

// ============================================================================
// Initialisation et gestion de l'état
// ============================================================================

void CSMAStrategy::Initialize() {
    std::lock_guard<std::mutex> guard(mDataMutex);

    if (mIsInitialized.load()) {
        return;
    }

    // Réinitialiser les données
    mClosePrices.clear();
    mVolumes.clear();
    mSMAHistory.clear();
    mSignalHistory.clear();

    // Réinitialiser l'état
    mInPosition = false;
    mCurrentPositionId.clear();

    // Réinitialiser les valeurs SMA
    mCurrentSMAValues = SSMAValues();
    mPreviousSMAValues = SSMAValues();

    // Réinitialiser l'analyse de tendance
    mTrendAnalysis = SSMATrendAnalysis();

    // Réinitialiser les métriques
    ResetMetrics();

    mIsInitialized.store(true);

    std::cout << "[SMAStrategy] Initialized with parameters: "
              << "Fast=" << mParams.mFastPeriod
              << ", Slow=" << mParams.mSlowPeriod
              << ", Long=" << mParams.mLongPeriod
              << ", TripleMA=" << (mParams.mUseTripleMA ? "true" : "false") << std::endl;
}

void CSMAStrategy::Shutdown() {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mIsInitialized.store(false);
    std::cout << "[SMAStrategy] Shutdown completed" << std::endl;
}

void CSMAStrategy::Reset() {
    std::lock_guard<std::mutex> guard(mDataMutex);

    // Conserver la configuration mais réinitialiser les données
    mClosePrices.clear();
    mVolumes.clear();
    mSMAHistory.clear();
    mSignalHistory.clear();

    mInPosition = false;
    mCurrentPositionId.clear();

    mCurrentSMAValues = SSMAValues();
    mPreviousSMAValues = SSMAValues();
    mTrendAnalysis = SSMATrendAnalysis();

    ResetMetrics();

    std::cout << "[SMAStrategy] Reset completed" << std::endl;
}

void CSMAStrategy::Start() {
    if (!mIsInitialized.load()) {
        Initialize();
    }
    CBaseStrategy::Start();
    std::cout << "[SMAStrategy] Started" << std::endl;
}

void CSMAStrategy::Stop() {
    CBaseStrategy::Stop();
    std::cout << "[SMAStrategy] Stopped" << std::endl;
}

void CSMAStrategy::Pause() {
    CBaseStrategy::Pause();
    std::cout << "[SMAStrategy] Paused" << std::endl;
}

void CSMAStrategy::Resume() {
    CBaseStrategy::Resume();
    std::cout << "[SMAStrategy] Resumed" << std::endl;
}

// ============================================================================
// Mise à jour et génération de signaux
// ============================================================================

SSignal CSMAStrategy::Update(const std::vector<API::SKline>& klines, const API::STicker& ticker) {
    std::lock_guard<std::mutex> guard(mDataMutex);

    SSignal signal;
    signal.mType = ESignalType::HOLD;
    signal.mStrategyName = GetName();
    signal.mSymbol = ticker.mSymbol;
    signal.mTimestamp = std::chrono::system_clock::now();

    if (!mIsInitialized.load() || klines.empty()) {
        signal.mMessage = "Strategy not initialized or no data";
        return signal;
    }

    try {
        // Mettre à jour les données
        UpdateClosePrices(klines);
        UpdateVolumes(klines);

        // Vérifier si on a suffisamment de données
        if (!HasSufficientData()) {
            signal.mMessage = "Insufficient data for SMA calculation";
            return signal;
        }

        // Sauvegarder les valeurs précédentes
        mPreviousSMAValues = mCurrentSMAValues;

        // Calculer les nouvelles valeurs SMA
        CalculateSMAValues(mClosePrices, mCurrentSMAValues);

        if (!IsValidSMAValues(mCurrentSMAValues)) {
            signal.mMessage = "Invalid SMA values calculated";
            return signal;
        }

        // Ajouter à l'historique
        UpdateSMAHistory();

        // Mettre à jour l'analyse de tendance
        UpdateTrendAnalysis();

        // Analyser les signaux
        ESMASignalType signalType = AnalyzeSMASignal(mCurrentSMAValues, mPreviousSMAValues);

        if (signalType != ESMASignalType::NONE && ShouldGenerateSignal(signalType)) {
            // Convertir le signal SMA en signal général
            switch (signalType) {
                case ESMASignalType::GOLDEN_CROSS:
                case ESMASignalType::PRICE_ABOVE_MA:
                case ESMASignalType::TREND_ACCELERATION:
                case ESMASignalType::PULLBACK_BUY:
                case ESMASignalType::TRIPLE_ALIGNMENT_BULL:
                    signal.mType = ESignalType::BUY;
                    break;

                case ESMASignalType::DEATH_CROSS:
                case ESMASignalType::PRICE_BELOW_MA:
                case ESMASignalType::TREND_DECELERATION:
                case ESMASignalType::PULLBACK_SELL:
                case ESMASignalType::TRIPLE_ALIGNMENT_BEAR:
                    signal.mType = ESignalType::SELL;
                    break;

                default:
                    signal.mType = ESignalType::HOLD;
                    break;
            }

            signal.mPrice = ticker.mPrice;
            signal.mMessage = SignalTypeToString(signalType);
            signal.mStrength = GetSignalStrength(signalType, mCurrentSMAValues);

            // Ajouter à l'historique des signaux
            AddSignalToHistory(signalType, mCurrentSMAValues, ticker.mPrice,
                              ticker.mVolume, signal.mMessage);

            LogSignal(signalType, mCurrentSMAValues, ticker.mPrice);
        }

        // Nettoyer les anciennes données
        CleanupOldData();

    } catch (const std::exception& e) {
        signal.mMessage = "SMA update error: " + std::string(e.what());
        std::cerr << "[SMAStrategy] " << signal.mMessage << std::endl;
    }

    return signal;
}

std::vector<SSignal> CSMAStrategy::ProcessMarketData(const std::vector<API::SKline>& klines, const API::STicker& ticker) {
    std::vector<SSignal> signals;

    // Pour cette implémentation simple, on retourne un seul signal
    SSignal signal = Update(klines, ticker);
    if (signal.mType != ESignalType::HOLD) {
        signals.push_back(signal);
    }

    return signals;
}

// ============================================================================
// Gestion des positions
// ============================================================================

void CSMAStrategy::OnPositionOpened(const SPosition& position) {
    std::lock_guard<std::mutex> guard(mPositionMutex);

    if (position.mStrategyName == GetName()) {
        mInPosition = true;
        mCurrentPositionSide = position.mSide;
        mCurrentPositionId = position.mId;

        std::cout << "[SMAStrategy] Position opened: " << position.mId
                  << " (" << (position.mSide == API::EOrderSide::BUY ? "BUY" : "SELL") << ")" << std::endl;
    }
}

void CSMAStrategy::OnPositionClosed(const SPosition& position, double exitPrice, double pnl) {
    std::lock_guard<std::mutex> guard(mPositionMutex);

    if (position.mStrategyName == GetName() && position.mId == mCurrentPositionId) {
        mInPosition = false;
        mCurrentPositionId.clear();

        // Mettre à jour les métriques
        UpdateMetrics(position, pnl);

        std::cout << "[SMAStrategy] Position closed: " << position.mId
                  << " PnL: " << std::fixed << std::setprecision(2) << pnl << std::endl;
    }
}

void CSMAStrategy::OnPositionUpdated(const SPosition& position) {
    if (position.mStrategyName == GetName() && position.mId == mCurrentPositionId) {
        UpdatePositionState(position);
    }
}

// ============================================================================
// Gestion des ordres
// ============================================================================

void CSMAStrategy::OnOrderFilled(const std::string& orderId, const SPosition& position) {
    std::cout << "[SMAStrategy] Order filled: " << orderId << std::endl;
}

void CSMAStrategy::OnOrderCanceled(const std::string& orderId, const std::string& reason) {
    std::cout << "[SMAStrategy] Order canceled: " << orderId << " Reason: " << reason << std::endl;
}

void CSMAStrategy::OnOrderRejected(const std::string& orderId, const std::string& reason) {
    std::cerr << "[SMAStrategy] Order rejected: " << orderId << " Reason: " << reason << std::endl;
}

// ============================================================================
// Métriques et statistiques
// ============================================================================

SStrategyMetrics CSMAStrategy::GetMetrics() const {
    std::lock_guard<std::mutex> guard(mMetricsMutex);

    SStrategyMetrics metrics = CBaseStrategy::GetMetrics();

    // Ajouter les métriques spécifiques à SMA
    metrics.mTotalTrades = mTotalTrades;
    metrics.mWinningTrades = mWinningTrades;
    metrics.mTotalPnL = mTotalPnL;
    metrics.mMaxDrawdown = mMaxDrawdown;

    if (mTotalTrades > 0) {
        metrics.mWinRate = static_cast<double>(mWinningTrades) / mTotalTrades * 100.0;
        metrics.mAverageReturn = mTotalPnL / mTotalTrades;
    }

    return metrics;
}

std::map<std::string, double> CSMAStrategy::GetCustomMetrics() const {
    std::lock_guard<std::mutex> guard(mMetricsMutex);

    std::map<std::string, double> metrics;

    metrics["GoldenCrosses"] = mGoldenCrosses;
    metrics["DeathCrosses"] = mDeathCrosses;
    metrics["TrendChanges"] = mTrendChanges;
    metrics["CurrentFastSMA"] = mCurrentSMAValues.mFastSMA;
    metrics["CurrentSlowSMA"] = mCurrentSMAValues.mSlowSMA;
    metrics["CurrentSpread"] = mCurrentSMAValues.mSpread;
    metrics["TrendStrength"] = mTrendAnalysis.mTrendStrength;

    // Statistiques des signaux
    for (const auto& pair : mSignalCounts) {
        metrics["Signal_" + SignalTypeToString(pair.first)] = pair.second;
    }

    // Temps passé dans chaque tendance
    for (const auto& pair : mTrendTimeSpent) {
        metrics["Trend_" + TrendToString(pair.first)] = pair.second;
    }

    return metrics;
}

void CSMAStrategy::UpdateMetrics(const SPosition& position, double pnl) {
    std::lock_guard<std::mutex> guard(mMetricsMutex);

    mTotalTrades++;
    mTotalPnL += pnl;

    if (pnl > 0) {
        mWinningTrades++;
        mConsecutiveWins++;
        mConsecutiveLosses = 0;
        mMaxConsecutiveWins = std::max(mMaxConsecutiveWins, mConsecutiveWins);
    } else {
        mConsecutiveLosses++;
        mConsecutiveWins = 0;
        mMaxConsecutiveLosses = std::max(mMaxConsecutiveLosses, mConsecutiveLosses);
    }

    // Mettre à jour le drawdown
    mCurrentBalance += pnl;
    mPeakBalance = std::max(mPeakBalance, mCurrentBalance);
    double currentDrawdown = mPeakBalance - mCurrentBalance;
    mMaxDrawdown = std::max(mMaxDrawdown, currentDrawdown);

    CalculateAdvancedMetrics();
}

// ============================================================================
// Validation et tests
// ============================================================================

bool CSMAStrategy::ValidateSignal(const SSignal& signal) const {
    if (signal.mStrategyName != GetName()) {
        return false;
    }

    if (signal.mType == ESignalType::HOLD) {
        return true;
    }

    // Valider la force du signal
    if (signal.mStrength < 0.3) {
        return false;
    }

    // Valider les filtres
    if (mSlopeFilterEnabled && !PassesSlopeFilter(mCurrentSMAValues)) {
        return false;
    }

    return true;
}

bool CSMAStrategy::CanTrade(const std::string& symbol) const {
    return IsSymbolSupported(symbol) && HasSufficientData();
}

double CSMAStrategy::CalculatePositionSize(const std::string& symbol, double price, double availableBalance) const {
    double positionValue = availableBalance * mParams.mPositionSize;
    return positionValue / price;
}

// ============================================================================
// Sérialisation
// ============================================================================

nlohmann::json CSMAStrategy::Serialize() const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    nlohmann::json data;
    data["type"] = "SMAStrategy";
    data["name"] = GetName();
    data["config"] = GetCurrentConfig();
    data["metrics"] = GetCustomMetrics();
    data["inPosition"] = mInPosition;
    data["currentPositionId"] = mCurrentPositionId;

    // Sérialiser l'historique (dernières 100 valeurs)
    nlohmann::json history = nlohmann::json::array();
    size_t count = std::min(size_t(100), mSMAHistory.size());
    for (size_t i = mSMAHistory.size() - count; i < mSMAHistory.size(); ++i) {
        nlohmann::json item;
        item["fastSMA"] = mSMAHistory[i].mFastSMA;
        item["slowSMA"] = mSMAHistory[i].mSlowSMA;
        item["longSMA"] = mSMAHistory[i].mLongSMA;
        item["spread"] = mSMAHistory[i].mSpread;
        history.push_back(item);
    }
    data["history"] = history;

    return data;
}

void CSMAStrategy::Deserialize(const nlohmann::json& data) {
    std::lock_guard<std::mutex> guard(mDataMutex);

    if (data.contains("config")) {
        Configure(data["config"]);
    }

    if (data.contains("inPosition")) {
        mInPosition = data["inPosition"];
    }

    if (data.contains("currentPositionId")) {
        mCurrentPositionId = data["currentPositionId"];
    }

    // Charger l'historique si disponible
    if (data.contains("history")) {
        mSMAHistory.clear();
        for (const auto& item : data["history"]) {
            SSMAValues values;
            values.mFastSMA = item["fastSMA"];
            values.mSlowSMA = item["slowSMA"];
            values.mLongSMA = item["longSMA"];
            values.mSpread = item["spread"];
            values.mIsValid = true;
            mSMAHistory.push_back(values);
        }
    }
}

// ============================================================================
// Utilitaires
// ============================================================================

bool CSMAStrategy::IsSymbolSupported(const std::string& symbol) const {
    // Pour cette implémentation, tous les symboles sont supportés
    return !symbol.empty();
}

std::vector<std::string> CSMAStrategy::GetSupportedSymbols() const {
    // Retourner une liste vide signifie que tous les symboles sont supportés
    return {};
}

std::vector<std::string> CSMAStrategy::GetRequiredIndicators() const {
    return {"SMA"};
}

// ============================================================================
// Méthodes spécifiques à SMA
// ============================================================================

void CSMAStrategy::SetSMAParams(const SSMAParams& params) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mParams = params;
    mConfiguration = params.mUseTripleMA ? ESMAConfiguration::TRIPLE_MA : ESMAConfiguration::DUAL_MA;
}

SSMAParams CSMAStrategy::GetSMAParams() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mParams;
}

SSMAValues CSMAStrategy::GetCurrentSMAValues() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mCurrentSMAValues;
}

std::vector<SSMAValues> CSMAStrategy::GetSMAHistory(size_t count) const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    std::vector<SSMAValues> result;
    size_t start = (mSMAHistory.size() > count) ? mSMAHistory.size() - count : 0;

    for (size_t i = start; i < mSMAHistory.size(); ++i) {
        result.push_back(mSMAHistory[i]);
    }

    return result;
}

std::vector<SSMASignalHistory> CSMAStrategy::GetSignalHistory(size_t count) const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    std::vector<SSMASignalHistory> result;
    size_t start = (mSignalHistory.size() > count) ? mSignalHistory.size() - count : 0;

    for (size_t i = start; i < mSignalHistory.size(); ++i) {
        result.push_back(mSignalHistory[i]);
    }

    return result;
}

// ============================================================================
// Configuration avancée
// ============================================================================

void CSMAStrategy::SetConfiguration(ESMAConfiguration config) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mConfiguration = config;
    mParams.mUseTripleMA = (config == ESMAConfiguration::TRIPLE_MA);
}

void CSMAStrategy::SetSlopeFilter(bool enable, double minSlope) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mSlopeFilterEnabled = enable;
    mMinSlope = minSlope;
    mParams.mUseSlopeFilter = enable;
    mParams.mMinSlope = minSlope;
}

void CSMAStrategy::SetVolumeFilter(bool enable, double threshold) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mVolumeFilterEnabled = enable;
    mVolumeThreshold = threshold;
    mParams.mUseVolumeFilter = enable;
    mParams.mVolumeThreshold = threshold;
}

void CSMAStrategy::SetTripleMAMode(bool enable) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mParams.mUseTripleMA = enable;
    mConfiguration = enable ? ESMAConfiguration::TRIPLE_MA : ESMAConfiguration::DUAL_MA;
}

// ============================================================================
// Analyse technique SMA
// ============================================================================

ESMATrend CSMAStrategy::GetCurrentTrend() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mTrendAnalysis.mCurrentTrend;
}

SSMATrendAnalysis CSMAStrategy::GetTrendAnalysis() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mTrendAnalysis;
}

ESMASignalType CSMAStrategy::AnalyzeSMASignal(const SSMAValues& current, const SSMAValues& previous) const {
    if (!IsValidSMAValues(current) || !IsValidSMAValues(previous)) {
        return ESMASignalType::NONE;
    }

    // Détecter les croisements
    ESMASignalType crossSignal = DetectCrossover(current, previous);
    if (crossSignal != ESMASignalType::NONE) {
        return crossSignal;
    }

    // Détecter les signaux de tendance
    ESMASignalType trendSignal = DetectTrendSignals(current, previous);
    if (trendSignal != ESMASignalType::NONE) {
        return trendSignal;
    }

    // Détecter les signaux triple MA
    if (mConfiguration == ESMAConfiguration::TRIPLE_MA) {
        ESMASignalType tripleSignal = DetectTripleMASignals(current, previous);
        if (tripleSignal != ESMASignalType::NONE) {
            return tripleSignal;
        }
    }

    return ESMASignalType::NONE;
}

double CSMAStrategy::GetTrendStrength() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mTrendAnalysis.mTrendStrength;
}

double CSMAStrategy::GetSignalStrength(ESMASignalType signalType, const SSMAValues& values) const {
    double strength = 0.5; // Valeur par défaut

    switch (signalType) {
        case ESMASignalType::GOLDEN_CROSS:
        case ESMASignalType::DEATH_CROSS:
            // Force basée sur l'écart entre les moyennes et leur pente
            strength = std::min(1.0, std::abs(values.mSpreadPercent) * 2.0 +
                               std::abs(values.mFastSlope) * 100.0);
            break;

        case ESMASignalType::TRIPLE_ALIGNMENT_BULL:
        case ESMASignalType::TRIPLE_ALIGNMENT_BEAR:
            // Force basée sur l'alignement parfait
            if (mConfiguration == ESMAConfiguration::TRIPLE_MA) {
                strength = 0.8 + std::min(0.2, std::abs(values.mFastSlope) * 50.0);
            }
            break;

        case ESMASignalType::TREND_ACCELERATION:
        case ESMASignalType::TREND_DECELERATION:
            // Force basée sur la variation de pente
            strength = std::min(1.0, std::abs(values.mFastSlope - mPreviousSMAValues.mFastSlope) * 1000.0);
            break;

        default:
            strength = 0.5;
            break;
    }

    return std::max(0.0, std::min(1.0, strength));
}

// ============================================================================
// Détection de configurations
// ============================================================================

bool CSMAStrategy::IsGoldenCross(const SSMAValues& current, const SSMAValues& previous) const {
    return previous.mFastSMA <= previous.mSlowSMA && current.mFastSMA > current.mSlowSMA;
}

bool CSMAStrategy::IsDeathCross(const SSMAValues& current, const SSMAValues& previous) const {
    return previous.mFastSMA >= previous.mSlowSMA && current.mFastSMA < current.mSlowSMA;
}

bool CSMAStrategy::IsTripleAlignment(bool bullish) const {
    if (mConfiguration != ESMAConfiguration::TRIPLE_MA) {
        return false;
    }

    const SSMAValues& values = mCurrentSMAValues;

    if (bullish) {
        return values.mFastSMA > values.mSlowSMA && values.mSlowSMA > values.mLongSMA;
    } else {
        return values.mFastSMA < values.mSlowSMA && values.mSlowSMA < values.mLongSMA;
    }
}

bool CSMAStrategy::IsPullbackOpportunity(bool bullish) const {
    const SSMAValues& values = mCurrentSMAValues;

    if (bullish) {
        // Tendance haussière mais prix proche de la moyenne rapide
        return values.mFastSMA > values.mSlowSMA &&
               mTrendAnalysis.mCurrentTrend == ESMATrend::STRONG_UPTREND &&
               !mClosePrices.empty() &&
               std::abs(mClosePrices.back() - values.mFastSMA) / values.mFastSMA < 0.005;
    } else {
        // Tendance baissière mais prix proche de la moyenne rapide
        return values.mFastSMA < values.mSlowSMA &&
               mTrendAnalysis.mCurrentTrend == ESMATrend::STRONG_DOWNTREND &&
               !mClosePrices.empty() &&
               std::abs(mClosePrices.back() - values.mFastSMA) / values.mFastSMA < 0.005;
    }
}

// ============================================================================
// Niveaux de support/résistance
// ============================================================================

double CSMAStrategy::GetDynamicSupport() const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    const SSMAValues& values = mCurrentSMAValues;

    // Le support est généralement la moyenne la plus basse en tendance haussière
    if (mTrendAnalysis.mCurrentTrend == ESMATrend::STRONG_UPTREND ||
        mTrendAnalysis.mCurrentTrend == ESMATrend::WEAK_UPTREND) {

        if (mConfiguration == ESMAConfiguration::TRIPLE_MA) {
            return std::min({values.mFastSMA, values.mSlowSMA, values.mLongSMA});
        } else {
            return std::min(values.mFastSMA, values.mSlowSMA);
        }
    }

    return values.mSlowSMA; // Défaut
}

double CSMAStrategy::GetDynamicResistance() const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    const SSMAValues& values = mCurrentSMAValues;

    // La résistance est généralement la moyenne la plus haute en tendance baissière
    if (mTrendAnalysis.mCurrentTrend == ESMATrend::STRONG_DOWNTREND ||
        mTrendAnalysis.mCurrentTrend == ESMATrend::WEAK_DOWNTREND) {

        if (mConfiguration == ESMAConfiguration::TRIPLE_MA) {
            return std::max({values.mFastSMA, values.mSlowSMA, values.mLongSMA});
        } else {
            return std::max(values.mFastSMA, values.mSlowSMA);
        }
    }

    return values.mSlowSMA; // Défaut
}

std::vector<double> CSMAStrategy::GetSMALevels() const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    std::vector<double> levels;
    const SSMAValues& values = mCurrentSMAValues;

    levels.push_back(values.mFastSMA);
    levels.push_back(values.mSlowSMA);

    if (mConfiguration == ESMAConfiguration::TRIPLE_MA) {
        levels.push_back(values.mLongSMA);
    }

    // Trier les niveaux
    std::sort(levels.begin(), levels.end());

    return levels;
}

// ============================================================================
// Méthodes de calcul privées
// ============================================================================

double CSMAStrategy::CalculateSMA(const std::deque<double>& prices, int period) const {
    if (prices.size() < static_cast<size_t>(period)) {
        return 0.0;
    }

    double sum = 0.0;
    for (int i = 0; i < period; ++i) {
        sum += prices[prices.size() - 1 - i];
    }

    return sum / period;
}

void CSMAStrategy::CalculateSMAValues(const std::deque<double>& prices, SSMAValues& outValues) {
    if (prices.size() < static_cast<size_t>(mParams.mSlowPeriod)) {
        outValues.mIsValid = false;
        return;
    }

    // Calculer les moyennes mobiles
    outValues.mFastSMA = CalculateSMA(prices, mParams.mFastPeriod);
    outValues.mSlowSMA = CalculateSMA(prices, mParams.mSlowPeriod);

    if (mConfiguration == ESMAConfiguration::TRIPLE_MA &&
        prices.size() >= static_cast<size_t>(mParams.mLongPeriod)) {
        outValues.mLongSMA = CalculateSMA(prices, mParams.mLongPeriod);
    }

    // Calculer les pentes
    if (mSMAHistory.size() >= 3) {
        std::deque<double> fastSMAValues, slowSMAValues, longSMAValues;

        // Collecter les dernières valeurs pour calculer la pente
        size_t start = std::max(size_t(0), mSMAHistory.size() - 3);
        for (size_t i = start; i < mSMAHistory.size(); ++i) {
            fastSMAValues.push_back(mSMAHistory[i].mFastSMA);
            slowSMAValues.push_back(mSMAHistory[i].mSlowSMA);
            if (mConfiguration == ESMAConfiguration::TRIPLE_MA) {
                longSMAValues.push_back(mSMAHistory[i].mLongSMA);
            }
        }

        // Ajouter la valeur actuelle
        fastSMAValues.push_back(outValues.mFastSMA);
        slowSMAValues.push_back(outValues.mSlowSMA);
        if (mConfiguration == ESMAConfiguration::TRIPLE_MA) {
            longSMAValues.push_back(outValues.mLongSMA);
        }

        outValues.mFastSlope = CalculateSlope(fastSMAValues);
        outValues.mSlowSlope = CalculateSlope(slowSMAValues);
        if (mConfiguration == ESMAConfiguration::TRIPLE_MA) {
            outValues.mLongSlope = CalculateSlope(longSMAValues);
        }
    }

    // Calculer l'écart
    outValues.mSpread = CalculateSpread(outValues.mFastSMA, outValues.mSlowSMA);
    if (outValues.mSlowSMA != 0.0) {
        outValues.mSpreadPercent = outValues.mSpread / outValues.mSlowSMA * 100.0;
    }

    outValues.mTimestamp = std::chrono::system_clock::now();
    outValues.mPeriodCount = static_cast<int>(prices.size());
    outValues.mIsValid = true;
}

double CSMAStrategy::CalculateSlope(const std::deque<double>& values, int period) const {
    if (values.size() < static_cast<size_t>(period)) {
        return 0.0;
    }

    // Calculer la pente par régression linéaire simple
    double n = period;
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0;

    for (int i = 0; i < period; ++i) {
        double x = i;
        double y = values[values.size() - period + i];
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }

    double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
    return slope;
}

double CSMAStrategy::CalculateSpread(double fastSMA, double slowSMA) const {
    return fastSMA - slowSMA;
}

double CSMAStrategy::CalculateAverageVolume(size_t periods) const {
    if (mVolumes.size() < periods) {
        return 0.0;
    }

    double sum = 0.0;
    for (size_t i = mVolumes.size() - periods; i < mVolumes.size(); ++i) {
        sum += mVolumes[i];
    }

    return sum / periods;
}

// ============================================================================
// Analyse de tendance
// ============================================================================

ESMATrend CSMAStrategy::DetermineTrend(const SSMAValues& values) const {
    if (!IsValidSMAValues(values)) {
        return ESMATrend::SIDEWAYS;
    }

    double spreadPercent = std::abs(values.mSpreadPercent);
    bool fastAboveSlow = values.mFastSMA > values.mSlowSMA;

    // Seuils pour déterminer la force de la tendance
    double weakThreshold = 0.5;   // 0.5%
    double strongThreshold = 1.0; // 1.0%

    if (fastAboveSlow) {
        if (spreadPercent > strongThreshold && values.mFastSlope > mMinSlope) {
            return ESMATrend::STRONG_UPTREND;
        } else if (spreadPercent > weakThreshold) {
            return ESMATrend::WEAK_UPTREND;
        }
    } else {
        if (spreadPercent > strongThreshold && values.mFastSlope < -mMinSlope) {
            return ESMATrend::STRONG_DOWNTREND;
        } else if (spreadPercent > weakThreshold) {
            return ESMATrend::WEAK_DOWNTREND;
        }
    }

    return ESMATrend::SIDEWAYS;
}

double CSMAStrategy::CalculateTrendStrength(const SSMAValues& values) const {
    if (!IsValidSMAValues(values)) {
        return 0.0;
    }

    // Combiner l'écart entre moyennes et la pente
    double spreadStrength = std::min(1.0, std::abs(values.mSpreadPercent) / 2.0);
    double slopeStrength = std::min(1.0, std::abs(values.mFastSlope) * 200.0);

    return (spreadStrength + slopeStrength) / 2.0;
}

void CSMAStrategy::UpdateTrendAnalysis() {
    ESMATrend newTrend = DetermineTrend(mCurrentSMAValues);

    if (newTrend != mTrendAnalysis.mCurrentTrend) {
        if (IsTrendChanging(newTrend)) {
            mTrendAnalysis.mPreviousTrend = mTrendAnalysis.mCurrentTrend;
            mTrendAnalysis.mCurrentTrend = newTrend;
            mTrendAnalysis.mTrendStartTime = std::chrono::system_clock::now();
            mTrendAnalysis.mIsTrendChanging = true;
            mTrendChanges++;
        }
    } else {
        mTrendAnalysis.mIsTrendChanging = false;
    }

    mTrendAnalysis.mTrendStrength = CalculateTrendStrength(mCurrentSMAValues);

    // Calculer la durée de la tendance
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(now - mTrendAnalysis.mTrendStartTime);
    mTrendAnalysis.mTrendDuration = duration.count();

    UpdateSupportResistanceLevels();
    UpdateTrendStatistics(mTrendAnalysis.mCurrentTrend);
}

bool CSMAStrategy::IsTrendChanging(ESMATrend newTrend) const {
    // Éviter les changements trop fréquents
    return newTrend != mTrendAnalysis.mCurrentTrend;
}

// ============================================================================
// Détection de signaux
// ============================================================================

ESMASignalType CSMAStrategy::DetectCrossover(const SSMAValues& current, const SSMAValues& previous) const {
    if (IsGoldenCross(current, previous)) {
        return ESMASignalType::GOLDEN_CROSS;
    }

    if (IsDeathCross(current, previous)) {
        return ESMASignalType::DEATH_CROSS;
    }

    return ESMASignalType::NONE;
}

ESMASignalType CSMAStrategy::DetectPriceMARelation(double price, const SSMAValues& values) const {
    if (price > values.mFastSMA && values.mFastSMA > values.mSlowSMA) {
        return ESMASignalType::PRICE_ABOVE_MA;
    }

    if (price < values.mFastSMA && values.mFastSMA < values.mSlowSMA) {
        return ESMASignalType::PRICE_BELOW_MA;
    }

    return ESMASignalType::NONE;
}

ESMASignalType CSMAStrategy::DetectTrendSignals(const SSMAValues& current, const SSMAValues& previous) const {
    // Détection d'accélération de tendance
    if (current.mFastSlope > previous.mFastSlope && current.mFastSlope > mMinSlope * 2) {
        return ESMASignalType::TREND_ACCELERATION;
    }

    // Détection de décélération de tendance
    if (current.mFastSlope < previous.mFastSlope && std::abs(current.mFastSlope) < mMinSlope) {
        return ESMASignalType::TREND_DECELERATION;
    }

    // Détection de pullback
    if (IsPullbackOpportunity(true)) {
        return ESMASignalType::PULLBACK_BUY;
    }

    if (IsPullbackOpportunity(false)) {
        return ESMASignalType::PULLBACK_SELL;
    }

    return ESMASignalType::NONE;
}

ESMASignalType CSMAStrategy::DetectTripleMASignals(const SSMAValues& current, const SSMAValues& previous) const {
    if (mConfiguration != ESMAConfiguration::TRIPLE_MA) {
        return ESMASignalType::NONE;
    }

    // Alignement haussier des 3 moyennes
    if (IsTripleAlignment(true) && !IsTripleAlignment(true)) { // Nouvelle condition
        return ESMASignalType::TRIPLE_ALIGNMENT_BULL;
    }

    // Alignement baissier des 3 moyennes
    if (IsTripleAlignment(false) && !IsTripleAlignment(false)) { // Nouvelle condition
        return ESMASignalType::TRIPLE_ALIGNMENT_BEAR;
    }

    return ESMASignalType::NONE;
}

bool CSMAStrategy::IsVolumeConfirmed(double currentVolume) const {
    if (!mVolumeFilterEnabled) {
        return true;
    }

    double avgVolume = CalculateAverageVolume();
    return currentVolume >= avgVolume * mVolumeThreshold;
}

// ============================================================================
// Gestion des données
// ============================================================================

void CSMAStrategy::UpdateClosePrices(const std::vector<API::SKline>& klines) {
    for (const auto& kline : klines) {
        mClosePrices.push_back(kline.mClose);
    }

    // Limiter la taille de l'historique
    size_t maxSize = std::max(mParams.mLongPeriod * 2, 200);
    while (mClosePrices.size() > maxSize) {
        mClosePrices.pop_front();
    }
}

void CSMAStrategy::UpdateVolumes(const std::vector<API::SKline>& klines) {
    for (const auto& kline : klines) {
        mVolumes.push_back(kline.mVolume);
    }

    // Limiter la taille de l'historique
    size_t maxSize = 200;
    while (mVolumes.size() > maxSize) {
        mVolumes.pop_front();
    }
}

void CSMAStrategy::UpdateSMAHistory() {
    mSMAHistory.push_back(mCurrentSMAValues);

    // Limiter la taille de l'historique
    size_t maxSize = 500;
    while (mSMAHistory.size() > maxSize) {
        mSMAHistory.pop_front();
    }
}

void CSMAStrategy::AddSignalToHistory(ESMASignalType signalType, const SSMAValues& values,
                                     double price, double volume, const std::string& description) {
    SSMASignalHistory signal;
    signal.mType = signalType;
    signal.mValues = values;
    signal.mTrend = mTrendAnalysis.mCurrentTrend;
    signal.mPrice = price;
    signal.mVolume = volume;
    signal.mTimestamp = std::chrono::system_clock::now();
    signal.mDescription = description;
    signal.mStrength = GetSignalStrength(signalType, values);

    mSignalHistory.push_back(signal);

    // Limiter la taille de l'historique
    size_t maxSize = 100;
    while (mSignalHistory.size() > maxSize) {
        mSignalHistory.pop_front();
    }
}

void CSMAStrategy::CleanupOldData() {
    // Cette méthode est appelée périodiquement pour nettoyer les anciennes données
    // La limitation est déjà gérée dans les méthodes Update*
}

// ============================================================================
// Validation et contrôles
// ============================================================================

bool CSMAStrategy::HasSufficientData() const {
    size_t requiredSize = std::max({
        static_cast<size_t>(mParams.mFastPeriod),
        static_cast<size_t>(mParams.mSlowPeriod),
        static_cast<size_t>(mParams.mLongPeriod)
    });

    return mClosePrices.size() >= requiredSize;
}

bool CSMAStrategy::IsValidSMAValues(const SSMAValues& values) const {
    return values.mIsValid &&
           values.mFastSMA > 0.0 &&
           values.mSlowSMA > 0.0 &&
           (!mParams.mUseTripleMA || values.mLongSMA > 0.0);
}

bool CSMAStrategy::ShouldGenerateSignal(ESMASignalType signalType) const {
    // Éviter de générer le même signal trop fréquemment
    if (!mSignalHistory.empty()) {
        const auto& lastSignal = mSignalHistory.back();
        auto now = std::chrono::system_clock::now();
        auto timeDiff = std::chrono::duration_cast<std::chrono::minutes>(now - lastSignal.mTimestamp);

        if (lastSignal.mType == signalType && timeDiff.count() < 15) {
            return false; // Même signal il y a moins de 15 minutes
        }
    }

    return true;
}

bool CSMAStrategy::PassesSlopeFilter(const SSMAValues& values) const {
    if (!mSlopeFilterEnabled) {
        return true;
    }

    return std::abs(values.mFastSlope) >= mMinSlope;
}

bool CSMAStrategy::PassesVolumeFilter(double volume) const {
    return IsVolumeConfirmed(volume);
}

// ============================================================================
// Support et résistance
// ============================================================================

void CSMAStrategy::UpdateSupportResistanceLevels() {
    // Mettre à jour les niveaux de support et résistance dynamiques
    mTrendAnalysis.mSupportLevel = GetDynamicSupport();
    mTrendAnalysis.mResistanceLevel = GetDynamicResistance();
}

double CSMAStrategy::FindNearestSMALevel(double price) const {
    std::vector<double> levels = GetSMALevels();

    if (levels.empty()) {
        return price;
    }

    double nearest = levels[0];
    double minDistance = std::abs(price - nearest);

    for (double level : levels) {
        double distance = std::abs(price - level);
        if (distance < minDistance) {
            minDistance = distance;
            nearest = level;
        }
    }

    return nearest;
}

bool CSMAStrategy::IsPriceNearMA(double price, double ma, double tolerance) const {
    return std::abs(price - ma) / ma * 100.0 <= tolerance;
}

// ============================================================================
// Gestion des positions
// ============================================================================

void CSMAStrategy::UpdatePositionState(const SPosition& position) {
    // Mettre à jour l'état interne basé sur la position
    if (position.mId == mCurrentPositionId) {
        // Vérifier si la position doit être fermée basée sur les conditions SMA
        if (ShouldClosePosition(mCurrentSMAValues)) {
            // Envoyer un signal de fermeture (implémentation dépendante du système)
            std::cout << "[SMAStrategy] Position should be closed based on SMA conditions" << std::endl;
        }
    }
}

double CSMAStrategy::CalculateStopLoss(double entryPrice, API::EOrderSide side) const {
    double stopLossPercent = mParams.mStopLossPercent / 100.0;

    if (side == API::EOrderSide::BUY) {
        return entryPrice * (1.0 - stopLossPercent);
    } else {
        return entryPrice * (1.0 + stopLossPercent);
    }
}

double CSMAStrategy::CalculateTakeProfit(double entryPrice, API::EOrderSide side) const {
    double takeProfitPercent = mParams.mTakeProfitPercent / 100.0;

    if (side == API::EOrderSide::BUY) {
        return entryPrice * (1.0 + takeProfitPercent);
    } else {
        return entryPrice * (1.0 - takeProfitPercent);
    }
}

bool CSMAStrategy::ShouldClosePosition(const SSMAValues& values) const {
    if (!mInPosition) {
        return false;
    }

    // Fermer en cas de croisement inverse
    if (mCurrentPositionSide == API::EOrderSide::BUY && values.mFastSMA < values.mSlowSMA) {
        return true;
    }

    if (mCurrentPositionSide == API::EOrderSide::SELL && values.mFastSMA > values.mSlowSMA) {
        return true;
    }

    // Fermer en cas de changement de tendance majeur
    if (mTrendAnalysis.mIsTrendChanging) {
        ESMATrend trend = mTrendAnalysis.mCurrentTrend;
        if ((mCurrentPositionSide == API::EOrderSide::BUY &&
             (trend == ESMATrend::STRONG_DOWNTREND || trend == ESMATrend::WEAK_DOWNTREND)) ||
            (mCurrentPositionSide == API::EOrderSide::SELL &&
             (trend == ESMATrend::STRONG_UPTREND || trend == ESMATrend::WEAK_UPTREND))) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Utilitaires
// ============================================================================

std::string CSMAStrategy::SignalTypeToString(ESMASignalType type) const {
    switch (type) {
        case ESMASignalType::NONE: return "None";
        case ESMASignalType::GOLDEN_CROSS: return "Golden Cross";
        case ESMASignalType::DEATH_CROSS: return "Death Cross";
        case ESMASignalType::PRICE_ABOVE_MA: return "Price Above MA";
        case ESMASignalType::PRICE_BELOW_MA: return "Price Below MA";
        case ESMASignalType::TREND_ACCELERATION: return "Trend Acceleration";
        case ESMASignalType::TREND_DECELERATION: return "Trend Deceleration";
        case ESMASignalType::PULLBACK_BUY: return "Pullback Buy";
        case ESMASignalType::PULLBACK_SELL: return "Pullback Sell";
        case ESMASignalType::TRIPLE_ALIGNMENT_BULL: return "Triple Alignment Bull";
        case ESMASignalType::TRIPLE_ALIGNMENT_BEAR: return "Triple Alignment Bear";
        case ESMASignalType::CONVERGENCE: return "Convergence";
        case ESMASignalType::DIVERGENCE: return "Divergence";
        default: return "Unknown";
    }
}

std::string CSMAStrategy::TrendToString(ESMATrend trend) const {
    switch (trend) {
        case ESMATrend::STRONG_UPTREND: return "Strong Uptrend";
        case ESMATrend::WEAK_UPTREND: return "Weak Uptrend";
        case ESMATrend::SIDEWAYS: return "Sideways";
        case ESMATrend::WEAK_DOWNTREND: return "Weak Downtrend";
        case ESMATrend::STRONG_DOWNTREND: return "Strong Downtrend";
        default: return "Unknown";
    }
}

std::string CSMAStrategy::ConfigurationToString(ESMAConfiguration config) const {
    switch (config) {
        case ESMAConfiguration::DUAL_MA: return "Dual MA";
        case ESMAConfiguration::TRIPLE_MA: return "Triple MA";
        case ESMAConfiguration::SINGLE_MA_PRICE: return "Single MA vs Price";
        default: return "Unknown";
    }
}

ESMASignalType CSMAStrategy::StringToSignalType(const std::string& typeStr) const {
    if (typeStr == "Golden Cross") return ESMASignalType::GOLDEN_CROSS;
    if (typeStr == "Death Cross") return ESMASignalType::DEATH_CROSS;
    if (typeStr == "Price Above MA") return ESMASignalType::PRICE_ABOVE_MA;
    if (typeStr == "Price Below MA") return ESMASignalType::PRICE_BELOW_MA;
    if (typeStr == "Trend Acceleration") return ESMASignalType::TREND_ACCELERATION;
    if (typeStr == "Trend Deceleration") return ESMASignalType::TREND_DECELERATION;
    if (typeStr == "Pullback Buy") return ESMASignalType::PULLBACK_BUY;
    if (typeStr == "Pullback Sell") return ESMASignalType::PULLBACK_SELL;
    if (typeStr == "Triple Alignment Bull") return ESMASignalType::TRIPLE_ALIGNMENT_BULL;
    if (typeStr == "Triple Alignment Bear") return ESMASignalType::TRIPLE_ALIGNMENT_BEAR;
    if (typeStr == "Convergence") return ESMASignalType::CONVERGENCE;
    if (typeStr == "Divergence") return ESMASignalType::DIVERGENCE;
    return ESMASignalType::NONE;
}

ESMATrend CSMAStrategy::StringToTrend(const std::string& trendStr) const {
    if (trendStr == "Strong Uptrend") return ESMATrend::STRONG_UPTREND;
    if (trendStr == "Weak Uptrend") return ESMATrend::WEAK_UPTREND;
    if (trendStr == "Sideways") return ESMATrend::SIDEWAYS;
    if (trendStr == "Weak Downtrend") return ESMATrend::WEAK_DOWNTREND;
    if (trendStr == "Strong Downtrend") return ESMATrend::STRONG_DOWNTREND;
    return ESMATrend::SIDEWAYS;
}

void CSMAStrategy::LogSignal(ESMASignalType signalType, const SSMAValues& values, double price) const {
    std::cout << "[SMAStrategy] Signal: " << SignalTypeToString(signalType)
              << " | Price: " << std::fixed << std::setprecision(4) << price
              << " | Fast SMA: " << values.mFastSMA
              << " | Slow SMA: " << values.mSlowSMA
              << " | Spread: " << values.mSpreadPercent << "%"
              << " | Trend: " << TrendToString(mTrendAnalysis.mCurrentTrend) << std::endl;
}

// ============================================================================
// Métriques
// ============================================================================

void CSMAStrategy::UpdateSignalStatistics(ESMASignalType signalType, bool successful) {
    std::lock_guard<std::mutex> guard(mMetricsMutex);

    mSignalCounts[signalType]++;

    if (mSignalCounts[signalType] > 0) {
        double oldSuccessRate = mSignalSuccessRates[signalType];
        int count = mSignalCounts[signalType];

        // Mise à jour de la moyenne mobile du taux de succès
        mSignalSuccessRates[signalType] =
            (oldSuccessRate * (count - 1) + (successful ? 100.0 : 0.0)) / count;
    }

    // Compter les types spéciaux
    if (signalType == ESMASignalType::GOLDEN_CROSS) {
        mGoldenCrosses++;
    } else if (signalType == ESMASignalType::DEATH_CROSS) {
        mDeathCrosses++;
    }
}

void CSMAStrategy::UpdateTrendStatistics(ESMATrend trend) {
    std::lock_guard<std::mutex> guard(mMetricsMutex);
    mTrendTimeSpent[trend]++;
}

void CSMAStrategy::CalculateAdvancedMetrics() {
    // Calculer des métriques avancées comme le Sharpe ratio, etc.
    // Implémentation future
}

void CSMAStrategy::ResetMetrics() {
    std::lock_guard<std::mutex> guard(mMetricsMutex);

    mTotalTrades = 0;
    mWinningTrades = 0;
    mTotalPnL = 0.0;
    mMaxDrawdown = 0.0;
    mPeakBalance = 0.0;
    mCurrentBalance = 0.0;
    mConsecutiveWins = 0;
    mConsecutiveLosses = 0;
    mMaxConsecutiveWins = 0;
    mMaxConsecutiveLosses = 0;

    mSignalCounts.clear();
    mSignalSuccessRates.clear();
    mTrendTimeSpent.clear();
    mGoldenCrosses = 0;
    mDeathCrosses = 0;
    mTrendChanges = 0;
}

// ============================================================================
// CSMAStrategyFactory - Implémentation des méthodes statiques
// ============================================================================

std::shared_ptr<CSMAStrategy> CSMAStrategyFactory::CreateDefault() {
    return std::make_shared<CSMAStrategy>(GetDefaultParams());
}

std::shared_ptr<CSMAStrategy> CSMAStrategyFactory::CreateScalping() {
    return std::make_shared<CSMAStrategy>(GetScalpingParams());
}

std::shared_ptr<CSMAStrategy> CSMAStrategyFactory::CreateSwing() {
    return std::make_shared<CSMAStrategy>(GetSwingParams());
}

std::shared_ptr<CSMAStrategy> CSMAStrategyFactory::CreateTrend() {
    return std::make_shared<CSMAStrategy>(GetTrendParams());
}

std::shared_ptr<CSMAStrategy> CSMAStrategyFactory::CreateBreakout() {
    return std::make_shared<CSMAStrategy>(GetBreakoutParams());
}

std::shared_ptr<CSMAStrategy> CSMAStrategyFactory::CreatePullback() {
    return std::make_shared<CSMAStrategy>(GetPullbackParams());
}

std::shared_ptr<CSMAStrategy> CSMAStrategyFactory::CreateTripleMA() {
    return std::make_shared<CSMAStrategy>(GetTripleMAParams());
}

std::shared_ptr<CSMAStrategy> CSMAStrategyFactory::CreateCustom(const SSMAParams& params) {
    return std::make_shared<CSMAStrategy>(params);
}

std::shared_ptr<CSMAStrategy> CSMAStrategyFactory::CreateFromConfig(const nlohmann::json& config) {
    auto strategy = std::make_shared<CSMAStrategy>();
    strategy->Configure(config);
    return strategy;
}

SSMAParams CSMAStrategyFactory::GetDefaultParams() {
    SSMAParams params;
    params.mFastPeriod = 10;
    params.mSlowPeriod = 20;
    params.mLongPeriod = 50;
    params.mPositionSize = 0.1;
    params.mStopLossPercent = 2.0;
    params.mTakeProfitPercent = 4.0;
    params.mUseTripleMA = false;
    params.mUseSlopeFilter = true;
    params.mMinSlope = 0.001;
    return params;
}

SSMAParams CSMAStrategyFactory::GetScalpingParams() {
    SSMAParams params;
    params.mFastPeriod = 5;
    params.mSlowPeriod = 10;
    params.mLongPeriod = 20;
    params.mPositionSize = 0.05;
    params.mStopLossPercent = 0.5;
    params.mTakeProfitPercent = 1.0;
    params.mUseTripleMA = false;
    params.mUseSlopeFilter = true;
    params.mMinSlope = 0.002;
    params.mUseVolumeFilter = true;
    params.mVolumeThreshold = 2.0;
    return params;
}

SSMAParams CSMAStrategyFactory::GetSwingParams() {
    SSMAParams params;
    params.mFastPeriod = 20;
    params.mSlowPeriod = 50;
    params.mLongPeriod = 100;
    params.mPositionSize = 0.15;
    params.mStopLossPercent = 3.0;
    params.mTakeProfitPercent = 6.0;
    params.mUseTripleMA = true;
    params.mUseSlopeFilter = true;
    params.mMinSlope = 0.0005;
    return params;
}

SSMAParams CSMAStrategyFactory::GetTrendParams() {
    SSMAParams params;
    params.mFastPeriod = 50;
    params.mSlowPeriod = 100;
    params.mLongPeriod = 200;
    params.mPositionSize = 0.2;
    params.mStopLossPercent = 5.0;
    params.mTakeProfitPercent = 10.0;
    params.mUseTripleMA = true;
    params.mUseSlopeFilter = true;
    params.mMinSlope = 0.0001;
    return params;
}

SSMAParams CSMAStrategyFactory::GetBreakoutParams() {
    SSMAParams params;
    params.mFastPeriod = 10;
    params.mSlowPeriod = 30;
    params.mLongPeriod = 60;
    params.mPositionSize = 0.1;
    params.mStopLossPercent = 2.5;
    params.mTakeProfitPercent = 5.0;
    params.mUseTripleMA = false;
    params.mUseSlopeFilter = true;
    params.mMinSlope = 0.002;
    params.mUseVolumeFilter = true;
    params.mVolumeThreshold = 1.5;
    return params;
}

SSMAParams CSMAStrategyFactory::GetPullbackParams() {
    SSMAParams params;
    params.mFastPeriod = 15;
    params.mSlowPeriod = 30;
    params.mLongPeriod = 60;
    params.mPositionSize = 0.08;
    params.mStopLossPercent = 1.5;
    params.mTakeProfitPercent = 3.0;
    params.mUseTripleMA = true;
    params.mUseSlopeFilter = true;
    params.mMinSlope = 0.001;
    return params;
}

SSMAParams CSMAStrategyFactory::GetTripleMAParams() {
    SSMAParams params;
    params.mFastPeriod = 12;
    params.mSlowPeriod = 26;
    params.mLongPeriod = 50;
    params.mPositionSize = 0.12;
    params.mStopLossPercent = 2.5;
    params.mTakeProfitPercent = 5.0;
    params.mUseTripleMA = true;
    params.mUseSlopeFilter = true;
    params.mMinSlope = 0.001;
    return params;
}

SSMAParams CSMAStrategyFactory::GetCryptoParams() {
    SSMAParams params = GetDefaultParams();
    params.mStopLossPercent = 3.0;
    params.mTakeProfitPercent = 6.0;
    params.mUseVolumeFilter = true;
    params.mVolumeThreshold = 1.8;
    return params;
}

SSMAParams CSMAStrategyFactory::GetForexParams() {
    SSMAParams params = GetDefaultParams();
    params.mStopLossPercent = 1.0;
    params.mTakeProfitPercent = 2.0;
    params.mMinSlope = 0.0005;
    return params;
}

SSMAParams CSMAStrategyFactory::GetStockParams() {
    SSMAParams params = GetDefaultParams();
    params.mFastPeriod = 20;
    params.mSlowPeriod = 50;
    params.mStopLossPercent = 2.5;
    params.mTakeProfitPercent = 5.0;
    return params;
}

SSMAParams CSMAStrategyFactory::GetCommodityParams() {
    SSMAParams params = GetDefaultParams();
    params.mFastPeriod = 15;
    params.mSlowPeriod = 35;
    params.mStopLossPercent = 3.5;
    params.mTakeProfitPercent = 7.0;
    return params;
}

SSMAParams CSMAStrategyFactory::GetIntraday() {
    SSMAParams params = GetScalpingParams();
    params.mFastPeriod = 5;
    params.mSlowPeriod = 15;
    return params;
}

SSMAParams CSMAStrategyFactory::GetDaily() {
    return GetDefaultParams();
}

SSMAParams CSMAStrategyFactory::GetWeekly() {
    return GetTrendParams();
}

} // namespace Strategy