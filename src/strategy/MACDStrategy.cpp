//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#include "MACDStrategy.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <iomanip>

namespace Strategy {

// ============================================================================
// CMACDStrategy - Constructeurs et destructeur
// ============================================================================

CMACDStrategy::CMACDStrategy()
    : CBaseStrategy()
    , mParams()
    , mIsInitialized(false) {

    SetName("MACD Strategy");
    mParams = SMACDParams(); // Utilise les valeurs par défaut
}

CMACDStrategy::CMACDStrategy(const SMACDParams& params)
    : CBaseStrategy()
    , mParams(params)
    , mIsInitialized(false) {

    SetName("MACD Strategy");
}

// ============================================================================
// Configuration de la stratégie
// ============================================================================

void CMACDStrategy::Configure(const nlohmann::json& config) {
    std::lock_guard<std::mutex> guard(mDataMutex);

    try {
        if (config.contains("fastPeriod")) {
            mParams.mFastPeriod = config["fastPeriod"];
        }
        if (config.contains("slowPeriod")) {
            mParams.mSlowPeriod = config["slowPeriod"];
        }
        if (config.contains("signalPeriod")) {
            mParams.mSignalPeriod = config["signalPeriod"];
        }
        if (config.contains("histogramThreshold")) {
            mParams.mHistogramThreshold = config["histogramThreshold"];
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
        if (config.contains("useDivergence")) {
            mParams.mUseDivergence = config["useDivergence"];
            mDivergenceDetectionEnabled = mParams.mUseDivergence;
        }
        if (config.contains("useHistogramAnalysis")) {
            mParams.mUseHistogramAnalysis = config["useHistogramAnalysis"];
            mHistogramAnalysisEnabled = mParams.mUseHistogramAnalysis;
        }
        if (config.contains("useZeroLineCross")) {
            mParams.mUseZeroLineCross = config["useZeroLineCross"];
            mZeroLineCrossEnabled = mParams.mUseZeroLineCross;
        }
        if (config.contains("minHistogramChange")) {
            mParams.mMinHistogramChange = config["minHistogramChange"];
            mMinHistogramChange = mParams.mMinHistogramChange;
        }
        if (config.contains("trendConfirmationPeriods")) {
            mParams.mTrendConfirmationPeriods = config["trendConfirmationPeriods"];
            mTrendConfirmationPeriods = mParams.mTrendConfirmationPeriods;
        }

        // Validation des paramètres
        if (mParams.mFastPeriod <= 0 || mParams.mFastPeriod > 50) {
            throw std::invalid_argument("Fast period must be between 1 and 50");
        }
        if (mParams.mSlowPeriod <= 0 || mParams.mSlowPeriod > 100) {
            throw std::invalid_argument("Slow period must be between 1 and 100");
        }
        if (mParams.mFastPeriod >= mParams.mSlowPeriod) {
            throw std::invalid_argument("Fast period must be less than slow period");
        }
        if (mParams.mSignalPeriod <= 0 || mParams.mSignalPeriod > 20) {
            throw std::invalid_argument("Signal period must be between 1 and 20");
        }

    } catch (const std::exception& e) {
        throw std::runtime_error("MACD configuration error: " + std::string(e.what()));
    }
}

nlohmann::json CMACDStrategy::GetDefaultConfig() const {
    nlohmann::json config;
    config["fastPeriod"] = 12;
    config["slowPeriod"] = 26;
    config["signalPeriod"] = 9;
    config["histogramThreshold"] = 0.001;
    config["positionSize"] = 0.1;
    config["stopLossPercent"] = 2.0;
    config["takeProfitPercent"] = 4.0;
    config["useDivergence"] = true;
    config["useHistogramAnalysis"] = true;
    config["useZeroLineCross"] = true;
    config["minHistogramChange"] = 0.0005;
    config["trendConfirmationPeriods"] = 3;
    return config;
}

nlohmann::json CMACDStrategy::GetCurrentConfig() const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    nlohmann::json config;
    config["fastPeriod"] = mParams.mFastPeriod;
    config["slowPeriod"] = mParams.mSlowPeriod;
    config["signalPeriod"] = mParams.mSignalPeriod;
    config["histogramThreshold"] = mParams.mHistogramThreshold;
    config["positionSize"] = mParams.mPositionSize;
    config["stopLossPercent"] = mParams.mStopLossPercent;
    config["takeProfitPercent"] = mParams.mTakeProfitPercent;
    config["useDivergence"] = mParams.mUseDivergence;
    config["useHistogramAnalysis"] = mParams.mUseHistogramAnalysis;
    config["useZeroLineCross"] = mParams.mUseZeroLineCross;
    config["minHistogramChange"] = mParams.mMinHistogramChange;
    config["trendConfirmationPeriods"] = mParams.mTrendConfirmationPeriods;
    return config;
}

void CMACDStrategy::SetConfig(const SStrategyConfig& config) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mConfig = config;
}

SStrategyConfig CMACDStrategy::GetConfig() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mConfig;
}

// ============================================================================
// Initialisation et gestion de l'état
// ============================================================================

void CMACDStrategy::Initialize() {
    std::lock_guard<std::mutex> guard(mDataMutex);

    if (mIsInitialized.load()) {
        return;
    }

    // Réinitialiser les données
    mClosePrices.clear();
    mFastEMA.clear();
    mSlowEMA.clear();
    mMACDHistory.clear();
    mSignalHistory.clear();

    // Réinitialiser l'état
    mInPosition = false;
    mCurrentPositionId.clear();

    // Réinitialiser les valeurs MACD
    mCurrentMACDValues = SMACDValues();
    mPreviousMACDValues = SMACDValues();
    mCurrentTrend = EMACDTrend::NEUTRAL;
    mPreviousTrend = EMACDTrend::NEUTRAL;

    // Réinitialiser les métriques
    ResetMetrics();

    mIsInitialized.store(true);

    std::cout << "[MACDStrategy] Initialized with parameters: "
              << "Fast=" << mParams.mFastPeriod
              << ", Slow=" << mParams.mSlowPeriod
              << ", Signal=" << mParams.mSignalPeriod
              << ", Divergence=" << (mParams.mUseDivergence ? "true" : "false") << std::endl;
}

void CMACDStrategy::Shutdown() {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mIsInitialized.store(false);
    std::cout << "[MACDStrategy] Shutdown completed" << std::endl;
}

void CMACDStrategy::Reset() {
    std::lock_guard<std::mutex> guard(mDataMutex);

    // Conserver la configuration mais réinitialiser les données
    mClosePrices.clear();
    mFastEMA.clear();
    mSlowEMA.clear();
    mMACDHistory.clear();
    mSignalHistory.clear();

    mInPosition = false;
    mCurrentPositionId.clear();

    mCurrentMACDValues = SMACDValues();
    mPreviousMACDValues = SMACDValues();
    mCurrentTrend = EMACDTrend::NEUTRAL;
    mPreviousTrend = EMACDTrend::NEUTRAL;

    ResetMetrics();

    std::cout << "[MACDStrategy] Reset completed" << std::endl;
}

void CMACDStrategy::Start() {
    if (!mIsInitialized.load()) {
        Initialize();
    }
    CBaseStrategy::Start();
    std::cout << "[MACDStrategy] Started" << std::endl;
}

void CMACDStrategy::Stop() {
    CBaseStrategy::Stop();
    std::cout << "[MACDStrategy] Stopped" << std::endl;
}

void CMACDStrategy::Pause() {
    CBaseStrategy::Pause();
    std::cout << "[MACDStrategy] Paused" << std::endl;
}

void CMACDStrategy::Resume() {
    CBaseStrategy::Resume();
    std::cout << "[MACDStrategy] Resumed" << std::endl;
}

// ============================================================================
// Mise à jour et génération de signaux
// ============================================================================

SSignal CMACDStrategy::Update(const std::vector<API::SKline>& klines, const API::STicker& ticker) {
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

        // Vérifier si on a suffisamment de données
        if (!HasSufficientData()) {
            signal.mMessage = "Insufficient data for MACD calculation";
            return signal;
        }

        // Sauvegarder les valeurs précédentes
        mPreviousMACDValues = mCurrentMACDValues;
        mPreviousTrend = mCurrentTrend;

        // Calculer les nouvelles valeurs MACD
        CalculateMACDValues(mClosePrices, mCurrentMACDValues);

        if (!IsValidMACDValues(mCurrentMACDValues)) {
            signal.mMessage = "Invalid MACD values calculated";
            return signal;
        }

        // Déterminer la tendance MACD actuelle
        mCurrentTrend = DetermineMACDTrend(mCurrentMACDValues);

        // Ajouter à l'historique
        UpdateMACDHistory();

        // Analyser les signaux
        EMACDSignalType signalType = AnalyzeMACDSignal(mCurrentMACDValues, mPreviousMACDValues);

        // Vérifier les croisements si aucun signal détecté
        if (signalType == EMACDSignalType::NONE) {
            signalType = DetectCrossoverSignals(mCurrentMACDValues, mPreviousMACDValues);
        }

        // Vérifier l'analyse de l'histogramme si activée
        if (signalType == EMACDSignalType::NONE && mHistogramAnalysisEnabled) {
            signalType = AnalyzeHistogram(mCurrentMACDValues, mPreviousMACDValues);
        }

        // Vérifier la divergence si activée
        if (signalType == EMACDSignalType::NONE && mDivergenceDetectionEnabled) {
            SMACDDivergence divergence = DetectDivergence(mDivergenceLookback);
            if (divergence.mIsBullish) {
                signalType = EMACDSignalType::DIVERGENCE_BULLISH;
            } else if (divergence.mIsBearish) {
                signalType = EMACDSignalType::DIVERGENCE_BEARISH;
            }
        }

        if (signalType != EMACDSignalType::NONE && ShouldGenerateSignal(signalType)) {
            // Convertir le signal MACD en signal général
            switch (signalType) {
                case EMACDSignalType::BULLISH_CROSSOVER:
                case EMACDSignalType::ZERO_LINE_CROSS_UP:
                case EMACDSignalType::HISTOGRAM_TURN_POSITIVE:
                case EMACDSignalType::DIVERGENCE_BULLISH:
                case EMACDSignalType::MOMENTUM_ACCELERATION_UP:
                case EMACDSignalType::TREND_CONFIRMATION_BULLISH:
                    signal.mType = ESignalType::BUY;
                    break;

                case EMACDSignalType::BEARISH_CROSSOVER:
                case EMACDSignalType::ZERO_LINE_CROSS_DOWN:
                case EMACDSignalType::HISTOGRAM_TURN_NEGATIVE:
                case EMACDSignalType::DIVERGENCE_BEARISH:
                case EMACDSignalType::MOMENTUM_ACCELERATION_DOWN:
                case EMACDSignalType::TREND_CONFIRMATION_BEARISH:
                    signal.mType = ESignalType::SELL;
                    break;

                default:
                    signal.mType = ESignalType::HOLD;
                    break;
            }

            signal.mPrice = ticker.mPrice;
            signal.mMessage = SignalTypeToString(signalType);
            signal.mStrength = GetSignalStrength(signalType, mCurrentMACDValues);

            // Ajouter à l'historique des signaux
            AddSignalToHistory(signalType, mCurrentMACDValues, ticker.mPrice, signal.mMessage);

            LogSignal(signalType, mCurrentMACDValues, ticker.mPrice);
        }

        // Mettre à jour les statistiques de tendance
        UpdateTrendStatistics(mCurrentTrend);

        // Nettoyer les anciennes données
        CleanupOldData();

    } catch (const std::exception& e) {
        signal.mMessage = "MACD update error: " + std::string(e.what());
        std::cerr << "[MACDStrategy] " << signal.mMessage << std::endl;
    }

    return signal;
}

std::vector<SSignal> CMACDStrategy::ProcessMarketData(const std::vector<API::SKline>& klines, const API::STicker& ticker) {
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

void CMACDStrategy::OnPositionOpened(const SPosition& position) {
    std::lock_guard<std::mutex> guard(mPositionMutex);

    if (position.mStrategyName == GetName()) {
        mInPosition = true;
        mCurrentPositionSide = position.mSide;
        mCurrentPositionId = position.mId;

        std::cout << "[MACDStrategy] Position opened: " << position.mId
                  << " (" << (position.mSide == API::EOrderSide::BUY ? "BUY" : "SELL") << ")"
                  << " MACD: " << std::fixed << std::setprecision(4) << mCurrentMACDValues.mMACD
                  << " Signal: " << mCurrentMACDValues.mSignal
                  << " Histogram: " << mCurrentMACDValues.mHistogram << std::endl;
    }
}

void CMACDStrategy::OnPositionClosed(const SPosition& position, double exitPrice, double pnl) {
    std::lock_guard<std::mutex> guard(mPositionMutex);

    if (position.mStrategyName == GetName() && position.mId == mCurrentPositionId) {
        mInPosition = false;
        mCurrentPositionId.clear();

        // Mettre à jour les métriques
        UpdateMetrics(position, pnl);

        std::cout << "[MACDStrategy] Position closed: " << position.mId
                  << " PnL: " << std::fixed << std::setprecision(2) << pnl
                  << " MACD: " << std::setprecision(4) << mCurrentMACDValues.mMACD << std::endl;
    }
}

void CMACDStrategy::OnPositionUpdated(const SPosition& position) {
    if (position.mStrategyName == GetName() && position.mId == mCurrentPositionId) {
        UpdatePositionState(position);
    }
}

// ============================================================================
// Gestion des ordres
// ============================================================================

void CMACDStrategy::OnOrderFilled(const std::string& orderId, const SPosition& position) {
    std::cout << "[MACDStrategy] Order filled: " << orderId << std::endl;
}

void CMACDStrategy::OnOrderCanceled(const std::string& orderId, const std::string& reason) {
    std::cout << "[MACDStrategy] Order canceled: " << orderId << " Reason: " << reason << std::endl;
}

void CMACDStrategy::OnOrderRejected(const std::string& orderId, const std::string& reason) {
    std::cerr << "[MACDStrategy] Order rejected: " << orderId << " Reason: " << reason << std::endl;
}

// ============================================================================
// Métriques et statistiques
// ============================================================================

SStrategyMetrics CMACDStrategy::GetMetrics() const {
    std::lock_guard<std::mutex> guard(mMetricsMutex);

    SStrategyMetrics metrics = CBaseStrategy::GetMetrics();

    // Ajouter les métriques spécifiques à MACD
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

std::map<std::string, double> CMACDStrategy::GetCustomMetrics() const {
    std::lock_guard<std::mutex> guard(mMetricsMutex);

    std::map<std::string, double> metrics;

    metrics["CurrentMACD"] = mCurrentMACDValues.mMACD;
    metrics["CurrentSignal"] = mCurrentMACDValues.mSignal;
    metrics["CurrentHistogram"] = mCurrentMACDValues.mHistogram;
    metrics["HistogramChange"] = mCurrentMACDValues.mHistogramChange;
    metrics["CurrentTrend"] = static_cast<double>(mCurrentTrend);
    metrics["CrossoverSignals"] = mCrossoverSignals;
    metrics["DivergenceSignals"] = mDivergenceSignals;
    metrics["ZeroLineCrosses"] = mZeroLineCrosses;
    metrics["HistogramReversals"] = mHistogramReversals;

    // Statistiques des signaux
    for (const auto& pair : mSignalCounts) {
        metrics["Signal_" + SignalTypeToString(pair.first)] = pair.second;
    }

    // Temps passé dans chaque tendance
    for (const auto& pair : mTrendTimeSpent) {
        metrics["Trend_" + TrendToString(pair.first)] = pair.second;
    }

    // Taux de succès par signal
    for (const auto& pair : mSignalSuccessRates) {
        metrics["SuccessRate_" + SignalTypeToString(pair.first)] = pair.second;
    }

    return metrics;
}

void CMACDStrategy::UpdateMetrics(const SPosition& position, double pnl) {
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

bool CMACDStrategy::ValidateSignal(const SSignal& signal) const {
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

    // Valider que le signal correspond aux filtres actifs
    if (!IsSignalFilterPassed(EMACDSignalType::NONE, mCurrentMACDValues)) {
        return false;
    }

    return true;
}

bool CMACDStrategy::CanTrade(const std::string& symbol) const {
    return IsSymbolSupported(symbol) && HasSufficientData();
}

double CMACDStrategy::CalculatePositionSize(const std::string& symbol, double price, double availableBalance) const {
    double positionValue = availableBalance * mParams.mPositionSize;
    return positionValue / price;
}

// ============================================================================
// Sérialisation
// ============================================================================

nlohmann::json CMACDStrategy::Serialize() const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    nlohmann::json data;
    data["type"] = "MACDStrategy";
    data["name"] = GetName();
    data["config"] = GetCurrentConfig();
    data["metrics"] = GetCustomMetrics();
    data["inPosition"] = mInPosition;
    data["currentPositionId"] = mCurrentPositionId;
    data["currentMACD"] = mCurrentMACDValues.mMACD;
    data["currentSignal"] = mCurrentMACDValues.mSignal;
    data["currentHistogram"] = mCurrentMACDValues.mHistogram;
    data["currentTrend"] = static_cast<int>(mCurrentTrend);

    // Sérialiser l'historique MACD (dernières 100 valeurs)
    nlohmann::json history = nlohmann::json::array();
    size_t count = std::min(size_t(100), mMACDHistory.size());
    for (size_t i = mMACDHistory.size() - count; i < mMACDHistory.size(); ++i) {
        nlohmann::json item;
        item["macd"] = mMACDHistory[i].mMACD;
        item["signal"] = mMACDHistory[i].mSignal;
        item["histogram"] = mMACDHistory[i].mHistogram;
        item["fastEMA"] = mMACDHistory[i].mFastEMA;
        item["slowEMA"] = mMACDHistory[i].mSlowEMA;
        history.push_back(item);
    }
    data["history"] = history;

    return data;
}

void CMACDStrategy::Deserialize(const nlohmann::json& data) {
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

    if (data.contains("currentMACD")) {
        mCurrentMACDValues.mMACD = data["currentMACD"];
    }

    if (data.contains("currentSignal")) {
        mCurrentMACDValues.mSignal = data["currentSignal"];
    }

    if (data.contains("currentHistogram")) {
        mCurrentMACDValues.mHistogram = data["currentHistogram"];
        mCurrentMACDValues.mIsValid = true;
    }

    if (data.contains("currentTrend")) {
        mCurrentTrend = static_cast<EMACDTrend>(data["currentTrend"]);
    }

    // Charger l'historique si disponible
    if (data.contains("history")) {
        mMACDHistory.clear();
        for (const auto& item : data["history"]) {
            SMACDValues values;
            values.mMACD = item["macd"];
            values.mSignal = item["signal"];
            values.mHistogram = item["histogram"];
            values.mFastEMA = item["fastEMA"];
            values.mSlowEMA = item["slowEMA"];
            values.mIsValid = true;
            mMACDHistory.push_back(values);
        }
    }
}

// ============================================================================
// Utilitaires
// ============================================================================

bool CMACDStrategy::IsSymbolSupported(const std::string& symbol) const {
    // Pour cette implémentation, tous les symboles sont supportés
    return !symbol.empty();
}

std::vector<std::string> CMACDStrategy::GetSupportedSymbols() const {
    // Retourner une liste vide signifie que tous les symboles sont supportés
    return {};
}

std::vector<std::string> CMACDStrategy::GetRequiredIndicators() const {
    return {"MACD", "EMA_FAST", "EMA_SLOW"};
}

// ============================================================================
// Méthodes spécifiques à MACD
// ============================================================================

void CMACDStrategy::SetMACDParams(const SMACDParams& params) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mParams = params;
}

SMACDParams CMACDStrategy::GetMACDParams() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mParams;
}

SMACDValues CMACDStrategy::GetCurrentMACDValues() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mCurrentMACDValues;
}

std::vector<SMACDValues> CMACDStrategy::GetMACDHistory(size_t count) const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    std::vector<SMACDValues> result;
    size_t start = (mMACDHistory.size() > count) ? mMACDHistory.size() - count : 0;

    for (size_t i = start; i < mMACDHistory.size(); ++i) {
        result.push_back(mMACDHistory[i]);
    }

    return result;
}

std::vector<SMACDSignalHistory> CMACDStrategy::GetSignalHistory(size_t count) const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    std::vector<SMACDSignalHistory> result;
    size_t start = (mSignalHistory.size() > count) ? mSignalHistory.size() - count : 0;

    for (size_t i = start; i < mSignalHistory.size(); ++i) {
        result.push_back(mSignalHistory[i]);
    }

    return result;
}

// ============================================================================
// Configuration avancée
// ============================================================================

void CMACDStrategy::SetFastPeriod(int period) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mParams.mFastPeriod = period;
}

void CMACDStrategy::SetSlowPeriod(int period) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mParams.mSlowPeriod = period;
}

void CMACDStrategy::SetSignalPeriod(int period) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mParams.mSignalPeriod = period;
}

void CMACDStrategy::SetDivergenceDetection(bool enable) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mDivergenceDetectionEnabled = enable;
    mParams.mUseDivergence = enable;
}

void CMACDStrategy::SetHistogramAnalysis(bool enable) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mHistogramAnalysisEnabled = enable;
    mParams.mUseHistogramAnalysis = enable;
}

void CMACDStrategy::SetZeroLineCrossDetection(bool enable) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mZeroLineCrossEnabled = enable;
    mParams.mUseZeroLineCross = enable;
}

// ============================================================================
// Analyse technique MACD
// ============================================================================

EMACDTrend CMACDStrategy::GetCurrentMACDTrend() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mCurrentTrend;
}

EMACDSignalType CMACDStrategy::AnalyzeMACDSignal(const SMACDValues& current, const SMACDValues& previous) const {
    if (!IsValidMACDValues(current) || !IsValidMACDValues(previous)) {
        return EMACDSignalType::NONE;
    }

    // Détecter les signaux de crossover
    EMACDSignalType crossoverSignal = DetectCrossoverSignals(current, previous);
    if (crossoverSignal != EMACDSignalType::NONE) {
        return crossoverSignal;
    }

    // Détecter les signaux de momentum
    EMACDSignalType momentumSignal = DetectMomentumSignals(current, previous);
    if (momentumSignal != EMACDSignalType::NONE) {
        return momentumSignal;
    }

    return EMACDSignalType::NONE;
}

EMACDSignalType CMACDStrategy::DetectCrossoverSignals(const SMACDValues& current, const SMACDValues& previous) const {
    // Crossover haussier : MACD passe au-dessus de la ligne de signal
    if (previous.mMACD <= previous.mSignal && current.mMACD > current.mSignal) {
        return EMACDSignalType::BULLISH_CROSSOVER;
    }

    // Crossover baissier : MACD passe en-dessous de la ligne de signal
    if (previous.mMACD >= previous.mSignal && current.mMACD < current.mSignal) {
        return EMACDSignalType::BEARISH_CROSSOVER;
    }

    // Vérifier les croisements de ligne zéro si activé
    if (mZeroLineCrossEnabled) {
        // Croisement haussier de la ligne zéro
        if (previous.mMACD <= 0.0 && current.mMACD > 0.0) {
            return EMACDSignalType::ZERO_LINE_CROSS_UP;
        }

        // Croisement baissier de la ligne zéro
        if (previous.mMACD >= 0.0 && current.mMACD < 0.0) {
            return EMACDSignalType::ZERO_LINE_CROSS_DOWN;
        }
    }

    return EMACDSignalType::NONE;
}

EMACDSignalType CMACDStrategy::AnalyzeHistogram(const SMACDValues& current, const SMACDValues& previous) const {
    // Histogramme qui passe au positif
    if (previous.mHistogram <= 0.0 && current.mHistogram > 0.0) {
        return EMACDSignalType::HISTOGRAM_TURN_POSITIVE;
    }

    // Histogramme qui passe au négatif
    if (previous.mHistogram >= 0.0 && current.mHistogram < 0.0) {
        return EMACDSignalType::HISTOGRAM_TURN_NEGATIVE;
    }

    // Vérifier les changements significatifs de l'histogramme
    if (std::abs(current.mHistogramChange) > mMinHistogramChange) {
        if (current.mHistogramChange > 0 && current.mHistogram > 0) {
            return EMACDSignalType::HISTOGRAM_ACCELERATING_UP;
        }
        if (current.mHistogramChange < 0 && current.mHistogram < 0) {
            return EMACDSignalType::HISTOGRAM_ACCELERATING_DOWN;
        }
    }

    return EMACDSignalType::NONE;
}

EMACDSignalType CMACDStrategy::DetectMomentumSignals(const SMACDValues& current, const SMACDValues& previous) const {
    // Accélération du momentum haussier
    if (current.mMACD > previous.mMACD && current.mHistogram > previous.mHistogram &&
        current.mHistogramChange > 0) {
        return EMACDSignalType::MOMENTUM_ACCELERATION_UP;
    }

    // Accélération du momentum baissier
    if (current.mMACD < previous.mMACD && current.mHistogram < previous.mHistogram &&
        current.mHistogramChange < 0) {
        return EMACDSignalType::MOMENTUM_ACCELERATION_DOWN;
    }

    // Confirmation de tendance
    if (IsTrendConfirmed(EMACDTrend::BULLISH)) {
        return EMACDSignalType::TREND_CONFIRMATION_BULLISH;
    }

    if (IsTrendConfirmed(EMACDTrend::BEARISH)) {
        return EMACDSignalType::TREND_CONFIRMATION_BEARISH;
    }

    return EMACDSignalType::NONE;
}

SMACDDivergence CMACDStrategy::DetectDivergence(size_t lookbackPeriods) const {
    return AnalyzeDivergence(mClosePrices, mMACDHistory, lookbackPeriods);
}

double CMACDStrategy::GetMACDMomentum(size_t periods) const {
    if (mMACDHistory.size() < periods + 1) {
        return 0.0;
    }

    // Calculer la variation moyenne du MACD sur les dernières périodes
    double totalChange = 0.0;
    for (size_t i = mMACDHistory.size() - periods; i < mMACDHistory.size(); ++i) {
        if (i > 0) {
            totalChange += mMACDHistory[i].mMACD - mMACDHistory[i - 1].mMACD;
        }
    }

    return totalChange / periods;
}

double CMACDStrategy::GetSignalStrength(EMACDSignalType signalType, const SMACDValues& values) const {
    double strength = 0.5; // Valeur par défaut

    switch (signalType) {
        case EMACDSignalType::BULLISH_CROSSOVER:
        case EMACDSignalType::BEARISH_CROSSOVER:
            // Force basée sur l'écart entre MACD et signal
            strength = std::min(1.0, std::abs(values.mMACD - values.mSignal) / 0.01);
            break;

        case EMACDSignalType::ZERO_LINE_CROSS_UP:
        case EMACDSignalType::ZERO_LINE_CROSS_DOWN:
            // Force basée sur la vitesse de croisement
            strength = std::min(1.0, std::abs(values.mMACD) / 0.005);
            break;

        case EMACDSignalType::HISTOGRAM_TURN_POSITIVE:
        case EMACDSignalType::HISTOGRAM_TURN_NEGATIVE:
            // Force basée sur l'ampleur du changement d'histogramme
            strength = std::min(1.0, std::abs(values.mHistogramChange) / 0.001);
            break;

        case EMACDSignalType::DIVERGENCE_BULLISH:
        case EMACDSignalType::DIVERGENCE_BEARISH:
            // Force maximale pour les divergences
            strength = 0.9;
            break;

        case EMACDSignalType::MOMENTUM_ACCELERATION_UP:
        case EMACDSignalType::MOMENTUM_ACCELERATION_DOWN:
            // Force basée sur l'accélération
            strength = std::min(1.0, std::abs(values.mHistogramChange) / 0.002);
            break;

        default:
            strength = 0.5;
            break;
    }

    return std::max(0.0, std::min(1.0, strength));
}

// ============================================================================
// Tendances et niveaux
// ============================================================================

bool CMACDStrategy::IsMACDAboveSignal(const SMACDValues& values) const {
    if (!values.mIsValid) return false;
    return values.mMACD > values.mSignal;
}

bool CMACDStrategy::IsMACDAboveZero(const SMACDValues& values) const {
    if (!values.mIsValid) return false;
    return values.mMACD > 0.0;
}

bool CMACDStrategy::IsHistogramPositive(const SMACDValues& values) const {
    if (!values.mIsValid) return false;
    return values.mHistogram > 0.0;
}

bool CMACDStrategy::IsHistogramIncreasing() const {
    return mCurrentMACDValues.mHistogramChange > 0.0;
}

bool CMACDStrategy::IsTrendConfirmed(EMACDTrend trend) const {
    if (mMACDHistory.size() < static_cast<size_t>(mTrendConfirmationPeriods)) {
        return false;
    }

    // Vérifier la cohérence de la tendance sur les dernières périodes
    int confirmationCount = 0;
    for (size_t i = mMACDHistory.size() - mTrendConfirmationPeriods; i < mMACDHistory.size(); ++i) {
        EMACDTrend periodTrend = DetermineMACDTrend(mMACDHistory[i]);
        if (periodTrend == trend) {
            confirmationCount++;
        }
    }

    return confirmationCount >= (mTrendConfirmationPeriods * 2 / 3); // 2/3 des périodes doivent confirmer
}

// ============================================================================
// Méthodes de calcul privées
// ============================================================================

double CMACDStrategy::CalculateEMA(const std::deque<double>& prices, int period) {
    if (prices.size() < static_cast<size_t>(period)) {
        return 0.0;
    }

    double multiplier = 2.0 / (period + 1.0);
    double ema = prices[0];

    for (size_t i = 1; i < prices.size(); ++i) {
        ema = (prices[i] * multiplier) + (ema * (1.0 - multiplier));
    }

    return ema;
}

void CMACDStrategy::CalculateMACDValues(const std::deque<double>& prices, SMACDValues& outValues) {
    if (prices.size() < static_cast<size_t>(mParams.mSlowPeriod)) {
        outValues.mIsValid = false;
        return;
    }

    // Calculer les EMAs
    outValues.mFastEMA = CalculateEMA(prices, mParams.mFastPeriod);
    outValues.mSlowEMA = CalculateEMA(prices, mParams.mSlowPeriod);

    // Calculer MACD
    outValues.mMACD = outValues.mFastEMA - outValues.mSlowEMA;

    // Calculer la ligne de signal (EMA du MACD)
    UpdateMACDForSignal(outValues.mMACD);
    outValues.mSignal = CalculateSignalLine();

    // Calculer l'histogramme
    outValues.mHistogram = outValues.mMACD - outValues.mSignal;

    // Calculer les changements
    outValues.mPreviousMACD = mCurrentMACDValues.mMACD;
    outValues.mMACDChange = outValues.mMACD - outValues.mPreviousMACD;
    outValues.mPreviousHistogram = mCurrentMACDValues.mHistogram;
    outValues.mHistogramChange = outValues.mHistogram - outValues.mPreviousHistogram;

    outValues.mTimestamp = std::chrono::system_clock::now();
    outValues.mIsValid = true;
}

void CMACDStrategy::UpdateMACDForSignal(double macdValue) {
    mMACDForSignal.push_back(macdValue);

    // Limiter la taille pour le calcul de la ligne de signal
    size_t maxSize = std::max(mParams.mSignalPeriod * 2, 50);
    while (mMACDForSignal.size() > maxSize) {
        mMACDForSignal.pop_front();
    }
}

double CMACDStrategy::CalculateSignalLine() {
    if (mMACDForSignal.size() < static_cast<size_t>(mParams.mSignalPeriod)) {
        return 0.0;
    }

    return CalculateEMA(mMACDForSignal, mParams.mSignalPeriod);
}

// ============================================================================
// Analyse des tendances
// ============================================================================

EMACDTrend CMACDStrategy::DetermineMACDTrend(const SMACDValues& values) const {
    if (!values.mIsValid) {
        return EMACDTrend::NEUTRAL;
    }

    // Tendance basée sur la position du MACD par rapport à la ligne de signal et zéro
    if (values.mMACD > values.mSignal && values.mMACD > 0.0) {
        return EMACDTrend::STRONG_BULLISH;
    } else if (values.mMACD > values.mSignal && values.mMACD <= 0.0) {
        return EMACDTrend::BULLISH;
    } else if (values.mMACD < values.mSignal && values.mMACD < 0.0) {
        return EMACDTrend::STRONG_BEARISH;
    } else if (values.mMACD < values.mSignal && values.mMACD >= 0.0) {
        return EMACDTrend::BEARISH;
    }

    return EMACDTrend::NEUTRAL;
}

// ============================================================================
// Détection de divergence
// ============================================================================

SMACDDivergence CMACDStrategy::AnalyzeDivergence(const std::deque<double>& prices, const std::deque<SMACDValues>& macdHistory, size_t lookback) const {
    SMACDDivergence divergence;

    if (prices.size() < lookback || macdHistory.size() < lookback) {
        return divergence;
    }

    // Trouver les hauts et bas récents
    std::vector<size_t> priceHighs, priceLows, macdHighs, macdLows;

    if (!FindPriceHighsAndLows(prices, lookback, priceHighs, priceLows) ||
        !FindMACDHighsAndLows(macdHistory, lookback, macdHighs, macdLows)) {
        return divergence;
    }

    // Analyser la divergence haussière
    if (priceLows.size() >= 2 && macdLows.size() >= 2) {
        size_t lastPriceLow = priceLows.back();
        size_t prevPriceLow = priceLows[priceLows.size() - 2];
        size_t lastMACDLow = macdLows.back();
        size_t prevMACDLow = macdLows[macdLows.size() - 2];

        if (prices[lastPriceLow] < prices[prevPriceLow] &&
            macdHistory[lastMACDLow].mMACD > macdHistory[prevMACDLow].mMACD) {
            divergence.mIsBullish = true;
            divergence.mPriceLow = prices[lastPriceLow];
            divergence.mMACDLow = macdHistory[lastMACDLow].mMACD;
            divergence.mStrength = CalculateDivergenceStrength(divergence);
        }
    }

    // Analyser la divergence baissière
    if (priceHighs.size() >= 2 && macdHighs.size() >= 2) {
        size_t lastPriceHigh = priceHighs.back();
        size_t prevPriceHigh = priceHighs[priceHighs.size() - 2];
        size_t lastMACDHigh = macdHighs.back();
        size_t prevMACDHigh = macdHighs[macdHighs.size() - 2];

        if (prices[lastPriceHigh] > prices[prevPriceHigh] &&
            macdHistory[lastMACDHigh].mMACD < macdHistory[prevMACDHigh].mMACD) {
            divergence.mIsBearish = true;
            divergence.mPriceHigh = prices[lastPriceHigh];
            divergence.mMACDHigh = macdHistory[lastMACDHigh].mMACD;
            divergence.mStrength = CalculateDivergenceStrength(divergence);
        }
    }

    if (divergence.mIsBullish || divergence.mIsBearish) {
        divergence.mDetectedAt = std::chrono::system_clock::now();
        divergence.mPeriodsSpan = static_cast<int>(lookback);
    }

    return divergence;
}

bool CMACDStrategy::FindPriceHighsAndLows(const std::deque<double>& prices, size_t lookback,
                                         std::vector<size_t>& highs, std::vector<size_t>& lows) const {
    if (prices.size() < lookback + 2) {
        return false;
    }

    size_t start = prices.size() - lookback;

    // Chercher les hauts et bas locaux avec une fenêtre de 3
    for (size_t i = start + 1; i < prices.size() - 1; ++i) {
        // Haut local
        if (prices[i] > prices[i-1] && prices[i] > prices[i+1]) {
            highs.push_back(i);
        }
        // Bas local
        if (prices[i] < prices[i-1] && prices[i] < prices[i+1]) {
            lows.push_back(i);
        }
    }

    return !highs.empty() && !lows.empty();
}

bool CMACDStrategy::FindMACDHighsAndLows(const std::deque<SMACDValues>& macdHistory, size_t lookback,
                                        std::vector<size_t>& highs, std::vector<size_t>& lows) const {
    if (macdHistory.size() < lookback + 2) {
        return false;
    }

    size_t start = macdHistory.size() - lookback;

    // Chercher les hauts et bas locaux MACD avec une fenêtre de 3
    for (size_t i = start + 1; i < macdHistory.size() - 1; ++i) {
        // Haut local MACD
        if (macdHistory[i].mMACD > macdHistory[i-1].mMACD &&
            macdHistory[i].mMACD > macdHistory[i+1].mMACD) {
            highs.push_back(i);
        }
        // Bas local MACD
        if (macdHistory[i].mMACD < macdHistory[i-1].mMACD &&
            macdHistory[i].mMACD < macdHistory[i+1].mMACD) {
            lows.push_back(i);
        }
    }

    return !highs.empty() && !lows.empty();
}

double CMACDStrategy::CalculateDivergenceStrength(const SMACDDivergence& divergence) const {
    // Calculer la force de la divergence basée sur l'écart
    double strength = 0.5;

    if (divergence.mIsBullish) {
        // Plus le MACD est éloigné de zéro lors de la divergence, plus c'est fort
        strength = std::min(1.0, std::abs(divergence.mMACDLow) / 0.01);
    } else if (divergence.mIsBearish) {
        // Plus le MACD est éloigné de zéro lors de la divergence, plus c'est fort
        strength = std::min(1.0, std::abs(divergence.mMACDHigh) / 0.01);
    }

    return std::max(0.1, strength);
}

// ============================================================================
// Gestion des données
// ============================================================================

void CMACDStrategy::UpdateClosePrices(const std::vector<API::SKline>& klines) {
    for (const auto& kline : klines) {
        mClosePrices.push_back(kline.mClose);
    }

    // Limiter la taille de l'historique
    size_t maxSize = std::max(mParams.mSlowPeriod * 3, 200);
    while (mClosePrices.size() > maxSize) {
        mClosePrices.pop_front();
    }
}

void CMACDStrategy::UpdateMACDHistory() {
    mMACDHistory.push_back(mCurrentMACDValues);

    // Limiter la taille de l'historique
    size_t maxSize = 500;
    while (mMACDHistory.size() > maxSize) {
        mMACDHistory.pop_front();
    }
}

void CMACDStrategy::AddSignalToHistory(EMACDSignalType signalType, const SMACDValues& values,
                                      double price, const std::string& description) {
    SMACDSignalHistory signal;
    signal.mType = signalType;
    signal.mValues = values;
    signal.mTrend = mCurrentTrend;
    signal.mPrice = price;
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

void CMACDStrategy::CleanupOldData() {
    // Cette méthode est appelée périodiquement pour nettoyer les anciennes données
    // La limitation est déjà gérée dans les méthodes Update*
}

// ============================================================================
// Validation et contrôles
// ============================================================================

bool CMACDStrategy::HasSufficientData() const {
    return mClosePrices.size() >= static_cast<size_t>(mParams.mSlowPeriod + mParams.mSignalPeriod);
}

bool CMACDStrategy::IsValidMACDValues(const SMACDValues& values) const {
    return values.mIsValid && std::isfinite(values.mMACD) &&
           std::isfinite(values.mSignal) && std::isfinite(values.mHistogram);
}

bool CMACDStrategy::ShouldGenerateSignal(EMACDSignalType signalType) const {
    // Éviter de générer le même signal trop fréquemment
    if (!mSignalHistory.empty()) {
        const auto& lastSignal = mSignalHistory.back();
        auto now = std::chrono::system_clock::now();
        auto timeDiff = std::chrono::duration_cast<std::chrono::minutes>(now - lastSignal.mTimestamp);

        if (lastSignal.mType == signalType && timeDiff.count() < 5) {
            return false; // Même signal il y a moins de 5 minutes
        }
    }

    return true;
}

bool CMACDStrategy::IsSignalFilterPassed(EMACDSignalType signalType, const SMACDValues& values) const {
    // Vérifier si le changement d'histogramme est suffisant
    if (std::abs(values.mHistogramChange) < mMinHistogramChange / 2.0) {
        return false;
    }

    return true;
}

// ============================================================================
// Gestion des positions
// ============================================================================

void CMACDStrategy::UpdatePositionState(const SPosition& position) {
    // Mettre à jour l'état interne basé sur la position
    if (position.mId == mCurrentPositionId) {
        // Vérifier si la position doit être fermée basée sur les conditions MACD
        if (ShouldClosePosition(mCurrentMACDValues)) {
            // Envoyer un signal de fermeture (implémentation dépendante du système)
            std::cout << "[MACDStrategy] Position should be closed based on MACD conditions" << std::endl;
        }
    }
}

double CMACDStrategy::CalculateStopLoss(double entryPrice, API::EOrderSide side) const {
    double stopLossPercent = mParams.mStopLossPercent / 100.0;

    if (side == API::EOrderSide::BUY) {
        return entryPrice * (1.0 - stopLossPercent);
    } else {
        return entryPrice * (1.0 + stopLossPercent);
    }
}

double CMACDStrategy::CalculateTakeProfit(double entryPrice, API::EOrderSide side) const {
    double takeProfitPercent = mParams.mTakeProfitPercent / 100.0;

    if (side == API::EOrderSide::BUY) {
        return entryPrice * (1.0 + takeProfitPercent);
    } else {
        return entryPrice * (1.0 - takeProfitPercent);
    }
}

bool CMACDStrategy::ShouldClosePosition(const SMACDValues& values) const {
    if (!mInPosition) {
        return false;
    }

    // Fermer les positions longues si crossover baissier
    if (mCurrentPositionSide == API::EOrderSide::BUY &&
        values.mMACD < values.mSignal && mPreviousMACDValues.mMACD >= mPreviousMACDValues.mSignal) {
        return true;
    }

    // Fermer les positions courtes si crossover haussier
    if (mCurrentPositionSide == API::EOrderSide::SELL &&
        values.mMACD > values.mSignal && mPreviousMACDValues.mMACD <= mPreviousMACDValues.mSignal) {
        return true;
    }

    // Fermer si l'histogramme change de direction de manière significative
    if (std::abs(values.mHistogramChange) > mMinHistogramChange * 2.0) {
        if (mCurrentPositionSide == API::EOrderSide::BUY && values.mHistogramChange < 0) {
            return true;
        }
        if (mCurrentPositionSide == API::EOrderSide::SELL && values.mHistogramChange > 0) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// Utilitaires
// ============================================================================

std::string CMACDStrategy::SignalTypeToString(EMACDSignalType type) const {
    switch (type) {
        case EMACDSignalType::NONE: return "None";
        case EMACDSignalType::BULLISH_CROSSOVER: return "Bullish Crossover";
        case EMACDSignalType::BEARISH_CROSSOVER: return "Bearish Crossover";
        case EMACDSignalType::ZERO_LINE_CROSS_UP: return "Zero Line Cross Up";
        case EMACDSignalType::ZERO_LINE_CROSS_DOWN: return "Zero Line Cross Down";
        case EMACDSignalType::HISTOGRAM_TURN_POSITIVE: return "Histogram Turn Positive";
        case EMACDSignalType::HISTOGRAM_TURN_NEGATIVE: return "Histogram Turn Negative";
        case EMACDSignalType::HISTOGRAM_ACCELERATING_UP: return "Histogram Accelerating Up";
        case EMACDSignalType::HISTOGRAM_ACCELERATING_DOWN: return "Histogram Accelerating Down";
        case EMACDSignalType::DIVERGENCE_BULLISH: return "Bullish Divergence";
        case EMACDSignalType::DIVERGENCE_BEARISH: return "Bearish Divergence";
        case EMACDSignalType::MOMENTUM_ACCELERATION_UP: return "Momentum Acceleration Up";
        case EMACDSignalType::MOMENTUM_ACCELERATION_DOWN: return "Momentum Acceleration Down";
        case EMACDSignalType::TREND_CONFIRMATION_BULLISH: return "Trend Confirmation Bullish";
        case EMACDSignalType::TREND_CONFIRMATION_BEARISH: return "Trend Confirmation Bearish";
        default: return "Unknown";
    }
}

std::string CMACDStrategy::TrendToString(EMACDTrend trend) const {
    switch (trend) {
        case EMACDTrend::STRONG_BEARISH: return "Strong Bearish";
        case EMACDTrend::BEARISH: return "Bearish";
        case EMACDTrend::NEUTRAL: return "Neutral";
        case EMACDTrend::BULLISH: return "Bullish";
        case EMACDTrend::STRONG_BULLISH: return "Strong Bullish";
        default: return "Unknown";
    }
}

EMACDSignalType CMACDStrategy::StringToSignalType(const std::string& typeStr) const {
    if (typeStr == "Bullish Crossover") return EMACDSignalType::BULLISH_CROSSOVER;
    if (typeStr == "Bearish Crossover") return EMACDSignalType::BEARISH_CROSSOVER;
    if (typeStr == "Zero Line Cross Up") return EMACDSignalType::ZERO_LINE_CROSS_UP;
    if (typeStr == "Zero Line Cross Down") return EMACDSignalType::ZERO_LINE_CROSS_DOWN;
    if (typeStr == "Histogram Turn Positive") return EMACDSignalType::HISTOGRAM_TURN_POSITIVE;
    if (typeStr == "Histogram Turn Negative") return EMACDSignalType::HISTOGRAM_TURN_NEGATIVE;
    if (typeStr == "Histogram Accelerating Up") return EMACDSignalType::HISTOGRAM_ACCELERATING_UP;
    if (typeStr == "Histogram Accelerating Down") return EMACDSignalType::HISTOGRAM_ACCELERATING_DOWN;
    if (typeStr == "Bullish Divergence") return EMACDSignalType::DIVERGENCE_BULLISH;
    if (typeStr == "Bearish Divergence") return EMACDSignalType::DIVERGENCE_BEARISH;
    if (typeStr == "Momentum Acceleration Up") return EMACDSignalType::MOMENTUM_ACCELERATION_UP;
    if (typeStr == "Momentum Acceleration Down") return EMACDSignalType::MOMENTUM_ACCELERATION_DOWN;
    if (typeStr == "Trend Confirmation Bullish") return EMACDSignalType::TREND_CONFIRMATION_BULLISH;
    if (typeStr == "Trend Confirmation Bearish") return EMACDSignalType::TREND_CONFIRMATION_BEARISH;
    return EMACDSignalType::NONE;
}

EMACDTrend CMACDStrategy::StringToTrend(const std::string& trendStr) const {
    if (trendStr == "Strong Bearish") return EMACDTrend::STRONG_BEARISH;
    if (trendStr == "Bearish") return EMACDTrend::BEARISH;
    if (trendStr == "Neutral") return EMACDTrend::NEUTRAL;
    if (trendStr == "Bullish") return EMACDTrend::BULLISH;
    if (trendStr == "Strong Bullish") return EMACDTrend::STRONG_BULLISH;
    return EMACDTrend::NEUTRAL;
}

void CMACDStrategy::LogSignal(EMACDSignalType signalType, const SMACDValues& values, double price) const {
    std::cout << "[MACDStrategy] Signal: " << SignalTypeToString(signalType)
              << " | Price: " << std::fixed << std::setprecision(4) << price
              << " | MACD: " << values.mMACD
              << " | Signal: " << values.mSignal
              << " | Histogram: " << values.mHistogram
              << " | Trend: " << TrendToString(mCurrentTrend) << std::endl;
}

// ============================================================================
// Métriques
// ============================================================================

void CMACDStrategy::UpdateSignalStatistics(EMACDSignalType signalType, bool successful) {
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
    if (signalType == EMACDSignalType::BULLISH_CROSSOVER || signalType == EMACDSignalType::BEARISH_CROSSOVER) {
        mCrossoverSignals++;
    } else if (signalType == EMACDSignalType::ZERO_LINE_CROSS_UP || signalType == EMACDSignalType::ZERO_LINE_CROSS_DOWN) {
        mZeroLineCrosses++;
    } else if (signalType == EMACDSignalType::DIVERGENCE_BULLISH || signalType == EMACDSignalType::DIVERGENCE_BEARISH) {
        mDivergenceSignals++;
    } else if (signalType == EMACDSignalType::HISTOGRAM_TURN_POSITIVE || signalType == EMACDSignalType::HISTOGRAM_TURN_NEGATIVE) {
        mHistogramReversals++;
    }
}

void CMACDStrategy::UpdateTrendStatistics(EMACDTrend trend) {
    std::lock_guard<std::mutex> guard(mMetricsMutex);
    mTrendTimeSpent[trend]++;
}

void CMACDStrategy::CalculateAdvancedMetrics() {
    // Calculer des métriques avancées comme le Sharpe ratio, etc.
    // Implémentation future
}

void CMACDStrategy::ResetMetrics() {
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
    mCrossoverSignals = 0;
    mDivergenceSignals = 0;
    mZeroLineCrosses = 0;
    mHistogramReversals = 0;
}

// ============================================================================
// CMACDStrategyFactory - Implémentation des méthodes statiques
// ============================================================================

std::shared_ptr<CMACDStrategy> CMACDStrategyFactory::CreateDefault() {
    return std::make_shared<CMACDStrategy>(GetDefaultParams());
}

std::shared_ptr<CMACDStrategy> CMACDStrategyFactory::CreateScalping() {
    return std::make_shared<CMACDStrategy>(GetScalpingParams());
}

std::shared_ptr<CMACDStrategy> CMACDStrategyFactory::CreateSwing() {
    return std::make_shared<CMACDStrategy>(GetSwingParams());
}

std::shared_ptr<CMACDStrategy> CMACDStrategyFactory::CreateTrendFollowing() {
    return std::make_shared<CMACDStrategy>(GetTrendFollowingParams());
}

std::shared_ptr<CMACDStrategy> CMACDStrategyFactory::CreateDivergenceHunter() {
    return std::make_shared<CMACDStrategy>(GetDivergenceParams());
}

std::shared_ptr<CMACDStrategy> CMACDStrategyFactory::CreateHistogramFocused() {
    return std::make_shared<CMACDStrategy>(GetHistogramParams());
}

std::shared_ptr<CMACDStrategy> CMACDStrategyFactory::CreateConservative() {
    return std::make_shared<CMACDStrategy>(GetConservativeParams());
}

std::shared_ptr<CMACDStrategy> CMACDStrategyFactory::CreateAggressive() {
    return std::make_shared<CMACDStrategy>(GetAggressiveParams());
}

std::shared_ptr<CMACDStrategy> CMACDStrategyFactory::CreateCustom(const SMACDParams& params) {
    return std::make_shared<CMACDStrategy>(params);
}

std::shared_ptr<CMACDStrategy> CMACDStrategyFactory::CreateFromConfig(const nlohmann::json& config) {
    auto strategy = std::make_shared<CMACDStrategy>();
    strategy->Configure(config);
    return strategy;
}

SMACDParams CMACDStrategyFactory::GetDefaultParams() {
    SMACDParams params;
    params.mFastPeriod = 12;
    params.mSlowPeriod = 26;
    params.mSignalPeriod = 9;
    params.mHistogramThreshold = 0.001;
    params.mPositionSize = 0.1;
    params.mStopLossPercent = 2.0;
    params.mTakeProfitPercent = 4.0;
    params.mUseDivergence = true;
    params.mUseHistogramAnalysis = true;
    params.mUseZeroLineCross = true;
    return params;
}

SMACDParams CMACDStrategyFactory::GetScalpingParams() {
    SMACDParams params;
    params.mFastPeriod = 5;
    params.mSlowPeriod = 13;
    params.mSignalPeriod = 5;
    params.mHistogramThreshold = 0.0005;
    params.mPositionSize = 0.05;
    params.mStopLossPercent = 0.5;
    params.mTakeProfitPercent = 1.0;
    params.mMinHistogramChange = 0.0002;
    params.mUseDivergence = false;
    params.mUseHistogramAnalysis = true;
    return params;
}

SMACDParams CMACDStrategyFactory::GetSwingParams() {
    SMACDParams params;
    params.mFastPeriod = 12;
    params.mSlowPeriod = 26;
    params.mSignalPeriod = 9;
    params.mHistogramThreshold = 0.002;
    params.mPositionSize = 0.15;
    params.mStopLossPercent = 3.0;
    params.mTakeProfitPercent = 6.0;
    params.mTrendConfirmationPeriods = 5;
    params.mUseDivergence = true;
    return params;
}

SMACDParams CMACDStrategyFactory::GetTrendFollowingParams() {
    SMACDParams params;
    params.mFastPeriod = 8;
    params.mSlowPeriod = 21;
    params.mSignalPeriod = 5;
    params.mHistogramThreshold = 0.0015;
    params.mPositionSize = 0.12;
    params.mStopLossPercent = 2.5;
    params.mTakeProfitPercent = 5.0;
    params.mTrendConfirmationPeriods = 3;
    params.mUseZeroLineCross = true;
    return params;
}

SMACDParams CMACDStrategyFactory::GetDivergenceParams() {
    SMACDParams params;
    params.mFastPeriod = 12;
    params.mSlowPeriod = 26;
    params.mSignalPeriod = 9;
    params.mHistogramThreshold = 0.001;
    params.mPositionSize = 0.1;
    params.mStopLossPercent = 2.0;
    params.mTakeProfitPercent = 4.0;
    params.mUseDivergence = true;
    params.mUseHistogramAnalysis = false;
    params.mUseZeroLineCross = false;
    return params;
}

SMACDParams CMACDStrategyFactory::GetHistogramParams() {
    SMACDParams params;
    params.mFastPeriod = 12;
    params.mSlowPeriod = 26;
    params.mSignalPeriod = 9;
    params.mHistogramThreshold = 0.0005;
    params.mPositionSize = 0.08;
    params.mStopLossPercent = 1.5;
    params.mTakeProfitPercent = 3.0;
    params.mMinHistogramChange = 0.0003;
    params.mUseHistogramAnalysis = true;
    params.mUseDivergence = false;
    return params;
}

SMACDParams CMACDStrategyFactory::GetConservativeParams() {
    SMACDParams params;
    params.mFastPeriod = 12;
    params.mSlowPeriod = 26;
    params.mSignalPeriod = 9;
    params.mHistogramThreshold = 0.002;
    params.mPositionSize = 0.05;
    params.mStopLossPercent = 1.5;
    params.mTakeProfitPercent = 3.0;
    params.mTrendConfirmationPeriods = 5;
    params.mMinHistogramChange = 0.001;
    return params;
}

SMACDParams CMACDStrategyFactory::GetAggressiveParams() {
    SMACDParams params;
    params.mFastPeriod = 8;
    params.mSlowPeriod = 17;
    params.mSignalPeriod = 5;
    params.mHistogramThreshold = 0.0005;
    params.mPositionSize = 0.2;
    params.mStopLossPercent = 3.0;
    params.mTakeProfitPercent = 6.0;
    params.mMinHistogramChange = 0.0002;
    params.mTrendConfirmationPeriods = 2;
    return params;
}

SMACDParams CMACDStrategyFactory::GetCryptoParams() {
    SMACDParams params = GetDefaultParams();
    params.mHistogramThreshold = 0.002;
    params.mStopLossPercent = 3.0;
    params.mTakeProfitPercent = 6.0;
    params.mMinHistogramChange = 0.001;
    return params;
}

SMACDParams CMACDStrategyFactory::GetForexParams() {
    SMACDParams params = GetDefaultParams();
    params.mHistogramThreshold = 0.0005;
    params.mStopLossPercent = 1.0;
    params.mTakeProfitPercent = 2.0;
    params.mMinHistogramChange = 0.0002;
    return params;
}

SMACDParams CMACDStrategyFactory::GetStockParams() {
    SMACDParams params = GetDefaultParams();
    params.mHistogramThreshold = 0.001;
    params.mStopLossPercent = 2.5;
    params.mTakeProfitPercent = 5.0;
    params.mTrendConfirmationPeriods = 3;
    return params;
}

} // namespace Strategy