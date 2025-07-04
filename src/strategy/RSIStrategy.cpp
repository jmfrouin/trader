
//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#include "RSIStrategy.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iostream>
#include <iomanip>

namespace Strategy {

// ============================================================================
// CRSIStrategy - Constructeurs et destructeur
// ============================================================================

CRSIStrategy::CRSIStrategy()
    : CBaseStrategy()
    , mParams()
    , mIsInitialized(false) {

    SetName("RSI Strategy");
    mParams = SRSIParams(); // Utilise les valeurs par défaut
}

CRSIStrategy::CRSIStrategy(const SRSIParams& params)
    : CBaseStrategy()
    , mParams(params)
    , mIsInitialized(false) {

    SetName("RSI Strategy");
}

// ============================================================================
// Configuration de la stratégie
// ============================================================================

void CRSIStrategy::Configure(const nlohmann::json& config) {
    std::lock_guard<std::mutex> guard(mDataMutex);

    try {
        if (config.contains("rsiPeriod")) {
            mParams.mRSIPeriod = config["rsiPeriod"];
        }
        if (config.contains("oversoldThreshold")) {
            mParams.mOversoldThreshold = config["oversoldThreshold"];
        }
        if (config.contains("overboughtThreshold")) {
            mParams.mOverboughtThreshold = config["overboughtThreshold"];
        }
        if (config.contains("extremeOversold")) {
            mParams.mExtremeOversold = config["extremeOversold"];
        }
        if (config.contains("extremeOverbought")) {
            mParams.mExtremeOverbought = config["extremeOverbought"];
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
        if (config.contains("useMultiTimeframe")) {
            mParams.mUseMultiTimeframe = config["useMultiTimeframe"];
            mMultiTimeframeEnabled = mParams.mUseMultiTimeframe;
        }
        if (config.contains("rsiChangeThreshold")) {
            mParams.mRSIChangeThreshold = config["rsiChangeThreshold"];
            mMinRSIChange = mParams.mRSIChangeThreshold;
        }

        // Validation des paramètres
        if (mParams.mRSIPeriod < 2 || mParams.mRSIPeriod > 50) {
            throw std::invalid_argument("RSI period must be between 2 and 50");
        }
        if (mParams.mOversoldThreshold >= mParams.mOverboughtThreshold) {
            throw std::invalid_argument("Oversold threshold must be less than overbought threshold");
        }
        if (mParams.mExtremeOversold >= mParams.mOversoldThreshold) {
            throw std::invalid_argument("Extreme oversold must be less than oversold threshold");
        }
        if (mParams.mExtremeOverbought <= mParams.mOverboughtThreshold) {
            throw std::invalid_argument("Extreme overbought must be greater than overbought threshold");
        }

    } catch (const std::exception& e) {
        throw std::runtime_error("RSI configuration error: " + std::string(e.what()));
    }
}

nlohmann::json CRSIStrategy::GetDefaultConfig() const {
    nlohmann::json config;
    config["rsiPeriod"] = 14;
    config["oversoldThreshold"] = 30.0;
    config["overboughtThreshold"] = 70.0;
    config["extremeOversold"] = 20.0;
    config["extremeOverbought"] = 80.0;
    config["positionSize"] = 0.1;
    config["stopLossPercent"] = 2.0;
    config["takeProfitPercent"] = 4.0;
    config["useDivergence"] = true;
    config["useMultiTimeframe"] = false;
    config["rsiChangeThreshold"] = 5.0;
    return config;
}

nlohmann::json CRSIStrategy::GetCurrentConfig() const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    nlohmann::json config;
    config["rsiPeriod"] = mParams.mRSIPeriod;
    config["oversoldThreshold"] = mParams.mOversoldThreshold;
    config["overboughtThreshold"] = mParams.mOverboughtThreshold;
    config["extremeOversold"] = mParams.mExtremeOversold;
    config["extremeOverbought"] = mParams.mExtremeOverbought;
    config["positionSize"] = mParams.mPositionSize;
    config["stopLossPercent"] = mParams.mStopLossPercent;
    config["takeProfitPercent"] = mParams.mTakeProfitPercent;
    config["useDivergence"] = mParams.mUseDivergence;
    config["useMultiTimeframe"] = mParams.mUseMultiTimeframe;
    config["rsiChangeThreshold"] = mParams.mRSIChangeThreshold;
    return config;
}

void CRSIStrategy::SetConfig(const SStrategyConfig& config) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mConfig = config;
}

SStrategyConfig CRSIStrategy::GetConfig() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mConfig;
}

// ============================================================================
// Initialisation et gestion de l'état
// ============================================================================

void CRSIStrategy::Initialize() {
    std::lock_guard<std::mutex> guard(mDataMutex);

    if (mIsInitialized.load()) {
        return;
    }

    // Réinitialiser les données
    mClosePrices.clear();
    mGains.clear();
    mLosses.clear();
    mRSIHistory.clear();
    mSignalHistory.clear();

    // Réinitialiser l'état
    mInPosition = false;
    mCurrentPositionId.clear();

    // Réinitialiser les valeurs RSI
    mCurrentRSIValues = SRSIValues();
    mPreviousRSIValues = SRSIValues();
    mCurrentZone = ERSIZone::NEUTRAL_LOW;
    mPreviousZone = ERSIZone::NEUTRAL_LOW;

    // Réinitialiser les métriques
    ResetMetrics();

    mIsInitialized.store(true);

    std::cout << "[RSIStrategy] Initialized with parameters: "
              << "Period=" << mParams.mRSIPeriod
              << ", Oversold=" << mParams.mOversoldThreshold
              << ", Overbought=" << mParams.mOverboughtThreshold
              << ", Divergence=" << (mParams.mUseDivergence ? "true" : "false") << std::endl;
}

void CRSIStrategy::Shutdown() {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mIsInitialized.store(false);
    std::cout << "[RSIStrategy] Shutdown completed" << std::endl;
}

void CRSIStrategy::Reset() {
    std::lock_guard<std::mutex> guard(mDataMutex);

    // Conserver la configuration mais réinitialiser les données
    mClosePrices.clear();
    mGains.clear();
    mLosses.clear();
    mRSIHistory.clear();
    mSignalHistory.clear();

    mInPosition = false;
    mCurrentPositionId.clear();

    mCurrentRSIValues = SRSIValues();
    mPreviousRSIValues = SRSIValues();
    mCurrentZone = ERSIZone::NEUTRAL_LOW;
    mPreviousZone = ERSIZone::NEUTRAL_LOW;

    ResetMetrics();

    std::cout << "[RSIStrategy] Reset completed" << std::endl;
}

void CRSIStrategy::Start() {
    if (!mIsInitialized.load()) {
        Initialize();
    }
    CBaseStrategy::Start();
    std::cout << "[RSIStrategy] Started" << std::endl;
}

void CRSIStrategy::Stop() {
    CBaseStrategy::Stop();
    std::cout << "[RSIStrategy] Stopped" << std::endl;
}

void CRSIStrategy::Pause() {
    CBaseStrategy::Pause();
    std::cout << "[RSIStrategy] Paused" << std::endl;
}

void CRSIStrategy::Resume() {
    CBaseStrategy::Resume();
    std::cout << "[RSIStrategy] Resumed" << std::endl;
}

// ============================================================================
// Mise à jour et génération de signaux
// ============================================================================

SSignal CRSIStrategy::Update(const std::vector<API::SKline>& klines, const API::STicker& ticker) {
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
            signal.mMessage = "Insufficient data for RSI calculation";
            return signal;
        }

        // Sauvegarder les valeurs précédentes
        mPreviousRSIValues = mCurrentRSIValues;
        mPreviousZone = mCurrentZone;

        // Calculer les nouvelles valeurs RSI
        CalculateRSIValues(mClosePrices, mCurrentRSIValues);

        if (!IsValidRSIValues(mCurrentRSIValues)) {
            signal.mMessage = "Invalid RSI values calculated";
            return signal;
        }

        // Déterminer la zone RSI actuelle
        mCurrentZone = DetermineRSIZone(mCurrentRSIValues.mRSI);

        // Ajouter à l'historique
        UpdateRSIHistory();

        // Analyser les signaux
        ERSISignalType signalType = AnalyzeRSISignal(mCurrentRSIValues, mPreviousRSIValues);

        // Vérifier les transitions de zone
        if (signalType == ERSISignalType::NONE) {
            signalType = DetectZoneTransition(mCurrentZone, mPreviousZone, mCurrentRSIValues);
        }

        // Vérifier la divergence si activée
        if (signalType == ERSISignalType::NONE && mDivergenceDetectionEnabled) {
            SRSIDivergence divergence = DetectDivergence(mDivergenceLookback);
            if (divergence.mIsBullish) {
                signalType = ERSISignalType::DIVERGENCE_BULLISH;
            } else if (divergence.mIsBearish) {
                signalType = ERSISignalType::DIVERGENCE_BEARISH;
            }
        }

        if (signalType != ERSISignalType::NONE && ShouldGenerateSignal(signalType)) {
            // Convertir le signal RSI en signal général
            switch (signalType) {
                case ERSISignalType::BUY_OVERSOLD:
                case ERSISignalType::BUY_OVERSOLD_EXIT:
                case ERSISignalType::DIVERGENCE_BULLISH:
                case ERSISignalType::MOMENTUM_BULLISH:
                case ERSISignalType::EXTREME_REVERSAL_BUY:
                    signal.mType = ESignalType::BUY;
                    break;

                case ERSISignalType::SELL_OVERBOUGHT:
                case ERSISignalType::SELL_OVERBOUGHT_EXIT:
                case ERSISignalType::DIVERGENCE_BEARISH:
                case ERSISignalType::MOMENTUM_BEARISH:
                case ERSISignalType::EXTREME_REVERSAL_SELL:
                    signal.mType = ESignalType::SELL;
                    break;

                default:
                    signal.mType = ESignalType::HOLD;
                    break;
            }

            signal.mPrice = ticker.mPrice;
            signal.mMessage = SignalTypeToString(signalType);
            signal.mStrength = GetSignalStrength(signalType, mCurrentRSIValues);

            // Ajouter à l'historique des signaux
            AddSignalToHistory(signalType, mCurrentRSIValues, ticker.mPrice, signal.mMessage);

            LogSignal(signalType, mCurrentRSIValues, ticker.mPrice);
        }

        // Mettre à jour les statistiques de zone
        UpdateZoneStatistics(mCurrentZone);

        // Nettoyer les anciennes données
        CleanupOldData();

    } catch (const std::exception& e) {
        signal.mMessage = "RSI update error: " + std::string(e.what());
        std::cerr << "[RSIStrategy] " << signal.mMessage << std::endl;
    }

    return signal;
}

std::vector<SSignal> CRSIStrategy::ProcessMarketData(const std::vector<API::SKline>& klines, const API::STicker& ticker) {
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

void CRSIStrategy::OnPositionOpened(const SPosition& position) {
    std::lock_guard<std::mutex> guard(mPositionMutex);

    if (position.mStrategyName == GetName()) {
        mInPosition = true;
        mCurrentPositionSide = position.mSide;
        mCurrentPositionId = position.mId;

        std::cout << "[RSIStrategy] Position opened: " << position.mId
                  << " (" << (position.mSide == API::EOrderSide::BUY ? "BUY" : "SELL") << ")"
                  << " RSI: " << std::fixed << std::setprecision(2) << mCurrentRSIValues.mRSI << std::endl;
    }
}

void CRSIStrategy::OnPositionClosed(const SPosition& position, double exitPrice, double pnl) {
    std::lock_guard<std::mutex> guard(mPositionMutex);

    if (position.mStrategyName == GetName() && position.mId == mCurrentPositionId) {
        mInPosition = false;
        mCurrentPositionId.clear();

        // Mettre à jour les métriques
        UpdateMetrics(position, pnl);

        std::cout << "[RSIStrategy] Position closed: " << position.mId
                  << " PnL: " << std::fixed << std::setprecision(2) << pnl
                  << " RSI: " << mCurrentRSIValues.mRSI << std::endl;
    }
}

void CRSIStrategy::OnPositionUpdated(const SPosition& position) {
    if (position.mStrategyName == GetName() && position.mId == mCurrentPositionId) {
        UpdatePositionState(position);
    }
}

// ============================================================================
// Gestion des ordres
// ============================================================================

void CRSIStrategy::OnOrderFilled(const std::string& orderId, const SPosition& position) {
    std::cout << "[RSIStrategy] Order filled: " << orderId << std::endl;
}

void CRSIStrategy::OnOrderCanceled(const std::string& orderId, const std::string& reason) {
    std::cout << "[RSIStrategy] Order canceled: " << orderId << " Reason: " << reason << std::endl;
}

void CRSIStrategy::OnOrderRejected(const std::string& orderId, const std::string& reason) {
    std::cerr << "[RSIStrategy] Order rejected: " << orderId << " Reason: " << reason << std::endl;
}

// ============================================================================
// Métriques et statistiques
// ============================================================================

SStrategyMetrics CRSIStrategy::GetMetrics() const {
    std::lock_guard<std::mutex> guard(mMetricsMutex);

    SStrategyMetrics metrics = CBaseStrategy::GetMetrics();

    // Ajouter les métriques spécifiques à RSI
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

std::map<std::string, double> CRSIStrategy::GetCustomMetrics() const {
    std::lock_guard<std::mutex> guard(mMetricsMutex);

    std::map<std::string, double> metrics;

    metrics["CurrentRSI"] = mCurrentRSIValues.mRSI;
    metrics["RSIChange"] = mCurrentRSIValues.mRSIChange;
    metrics["CurrentZone"] = static_cast<double>(mCurrentZone);
    metrics["OversoldEntries"] = mOversoldEntries;
    metrics["OverboughtEntries"] = mOverboughtEntries;
    metrics["DivergenceSignals"] = mDivergenceSignals;

    // Statistiques des signaux
    for (const auto& pair : mSignalCounts) {
        metrics["Signal_" + SignalTypeToString(pair.first)] = pair.second;
    }

    // Temps passé dans chaque zone
    for (const auto& pair : mZoneTimeSpent) {
        metrics["Zone_" + ZoneToString(pair.first)] = pair.second;
    }

    // Taux de succès par signal
    for (const auto& pair : mSignalSuccessRates) {
        metrics["SuccessRate_" + SignalTypeToString(pair.first)] = pair.second;
    }

    return metrics;
}

void CRSIStrategy::UpdateMetrics(const SPosition& position, double pnl) {
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

bool CRSIStrategy::ValidateSignal(const SSignal& signal) const {
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
    if (!IsSignalFilterPassed(ERSISignalType::NONE, mCurrentRSIValues)) {
        return false;
    }

    return true;
}

bool CRSIStrategy::CanTrade(const std::string& symbol) const {
    return IsSymbolSupported(symbol) && HasSufficientData();
}

double CRSIStrategy::CalculatePositionSize(const std::string& symbol, double price, double availableBalance) const {
    double positionValue = availableBalance * mParams.mPositionSize;
    return positionValue / price;
}

// ============================================================================
// Sérialisation
// ============================================================================

nlohmann::json CRSIStrategy::Serialize() const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    nlohmann::json data;
    data["type"] = "RSIStrategy";
    data["name"] = GetName();
    data["config"] = GetCurrentConfig();
    data["metrics"] = GetCustomMetrics();
    data["inPosition"] = mInPosition;
    data["currentPositionId"] = mCurrentPositionId;
    data["currentRSI"] = mCurrentRSIValues.mRSI;
    data["currentZone"] = static_cast<int>(mCurrentZone);

    // Sérialiser l'historique RSI (dernières 100 valeurs)
    nlohmann::json history = nlohmann::json::array();
    size_t count = std::min(size_t(100), mRSIHistory.size());
    for (size_t i = mRSIHistory.size() - count; i < mRSIHistory.size(); ++i) {
        nlohmann::json item;
        item["rsi"] = mRSIHistory[i].mRSI;
        item["change"] = mRSIHistory[i].mRSIChange;
        item["averageGain"] = mRSIHistory[i].mAverageGain;
        item["averageLoss"] = mRSIHistory[i].mAverageLoss;
        history.push_back(item);
    }
    data["history"] = history;

    return data;
}

void CRSIStrategy::Deserialize(const nlohmann::json& data) {
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

    if (data.contains("currentRSI")) {
        mCurrentRSIValues.mRSI = data["currentRSI"];
        mCurrentRSIValues.mIsValid = true;
    }

    if (data.contains("currentZone")) {
        mCurrentZone = static_cast<ERSIZone>(data["currentZone"]);
    }

    // Charger l'historique si disponible
    if (data.contains("history")) {
        mRSIHistory.clear();
        for (const auto& item : data["history"]) {
            SRSIValues values;
            values.mRSI = item["rsi"];
            values.mRSIChange = item["change"];
            values.mAverageGain = item["averageGain"];
            values.mAverageLoss = item["averageLoss"];
            values.mIsValid = true;
            mRSIHistory.push_back(values);
        }
    }
}

// ============================================================================
// Utilitaires
// ============================================================================

bool CRSIStrategy::IsSymbolSupported(const std::string& symbol) const {
    // Pour cette implémentation, tous les symboles sont supportés
    return !symbol.empty();
}

std::vector<std::string> CRSIStrategy::GetSupportedSymbols() const {
    // Retourner une liste vide signifie que tous les symboles sont supportés
    return {};
}

std::vector<std::string> CRSIStrategy::GetRequiredIndicators() const {
    return {"RSI"};
}

// ============================================================================
// Méthodes spécifiques à RSI
// ============================================================================

void CRSIStrategy::SetRSIParams(const SRSIParams& params) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mParams = params;
}

SRSIParams CRSIStrategy::GetRSIParams() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mParams;
}

SRSIValues CRSIStrategy::GetCurrentRSIValues() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mCurrentRSIValues;
}

std::vector<SRSIValues> CRSIStrategy::GetRSIHistory(size_t count) const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    std::vector<SRSIValues> result;
    size_t start = (mRSIHistory.size() > count) ? mRSIHistory.size() - count : 0;

    for (size_t i = start; i < mRSIHistory.size(); ++i) {
        result.push_back(mRSIHistory[i]);
    }

    return result;
}

std::vector<SRSISignalHistory> CRSIStrategy::GetSignalHistory(size_t count) const {
    std::lock_guard<std::mutex> guard(mDataMutex);

    std::vector<SRSISignalHistory> result;
    size_t start = (mSignalHistory.size() > count) ? mSignalHistory.size() - count : 0;

    for (size_t i = start; i < mSignalHistory.size(); ++i) {
        result.push_back(mSignalHistory[i]);
    }

    return result;
}

// ============================================================================
// Configuration avancée
// ============================================================================

void CRSIStrategy::SetOversoldThreshold(double threshold) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mParams.mOversoldThreshold = threshold;
}

void CRSIStrategy::SetOverboughtThreshold(double threshold) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mParams.mOverboughtThreshold = threshold;
}

void CRSIStrategy::SetDivergenceDetection(bool enable) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mDivergenceDetectionEnabled = enable;
    mParams.mUseDivergence = enable;
}

void CRSIStrategy::SetMultiTimeframeAnalysis(bool enable) {
    std::lock_guard<std::mutex> guard(mDataMutex);
    mMultiTimeframeEnabled = enable;
    mParams.mUseMultiTimeframe = enable;
}

// ============================================================================
// Analyse technique RSI
// ============================================================================

ERSIZone CRSIStrategy::GetCurrentRSIZone() const {
    std::lock_guard<std::mutex> guard(mDataMutex);
    return mCurrentZone;
}

ERSISignalType CRSIStrategy::AnalyzeRSISignal(const SRSIValues& current, const SRSIValues& previous) const {
    if (!IsValidRSIValues(current) || !IsValidRSIValues(previous)) {
        return ERSISignalType::NONE;
    }

    // Détecter les signaux de momentum
    ERSISignalType momentumSignal = DetectMomentumSignal(current, previous);
    if (momentumSignal != ERSISignalType::NONE) {
        return momentumSignal;
    }

    // Détecter les signaux de zone
    ERSIZone currentZone = DetermineRSIZone(current.mRSI);
    ERSIZone previousZone = DetermineRSIZone(previous.mRSI);

    return DetectZoneTransition(currentZone, previousZone, current);
}

SRSIDivergence CRSIStrategy::DetectDivergence(size_t lookbackPeriods) const {
    return AnalyzeDivergence(mClosePrices, mRSIHistory, lookbackPeriods);
}

double CRSIStrategy::GetRSIMomentum(size_t periods) const {
    if (mRSIHistory.size() < periods + 1) {
        return 0.0;
    }

    // Calculer la variation moyenne du RSI sur les dernières périodes
    double totalChange = 0.0;
    for (size_t i = mRSIHistory.size() - periods; i < mRSIHistory.size(); ++i) {
        totalChange += mRSIHistory[i].mRSIChange;
    }

    return totalChange / periods;
}

double CRSIStrategy::GetSignalStrength(ERSISignalType signalType, const SRSIValues& values) const {
    double strength = 0.5; // Valeur par défaut

    switch (signalType) {
        case ERSISignalType::BUY_OVERSOLD:
        case ERSISignalType::SELL_OVERBOUGHT:
            // Force basée sur la distance des seuils
            if (signalType == ERSISignalType::BUY_OVERSOLD) {
                strength = std::max(0.0, (mParams.mOversoldThreshold - values.mRSI) / mParams.mOversoldThreshold);
            } else {
                strength = std::max(0.0, (values.mRSI - mParams.mOverboughtThreshold) / (100.0 - mParams.mOverboughtThreshold));
            }
            break;

        case ERSISignalType::EXTREME_REVERSAL_BUY:
        case ERSISignalType::EXTREME_REVERSAL_SELL:
            // Force maximale pour les signaux extrêmes
            strength = 0.9;
            break;

        case ERSISignalType::DIVERGENCE_BULLISH:
        case ERSISignalType::DIVERGENCE_BEARISH:
            // Force basée sur la divergence (implémentation simplifiée)
            strength = 0.8;
            break;

        case ERSISignalType::MOMENTUM_BULLISH:
        case ERSISignalType::MOMENTUM_BEARISH:
            // Force basée sur le changement de RSI
            strength = std::min(1.0, std::abs(values.mRSIChange) / 20.0);
            break;

        default:
            strength = 0.5;
            break;
    }

    return std::max(0.0, std::min(1.0, strength));
}

// ============================================================================
// Zones et niveaux
// ============================================================================

bool CRSIStrategy::IsInOversoldZone(double rsi) const {
    if (rsi < 0) rsi = mCurrentRSIValues.mRSI;
    return rsi <= mParams.mOversoldThreshold;
}

bool CRSIStrategy::IsInOverboughtZone(double rsi) const {
    if (rsi < 0) rsi = mCurrentRSIValues.mRSI;
    return rsi >= mParams.mOverboughtThreshold;
}

bool CRSIStrategy::IsInExtremeZone(double rsi) const {
    if (rsi < 0) rsi = mCurrentRSIValues.mRSI;
    return rsi <= mParams.mExtremeOversold || rsi >= mParams.mExtremeOverbought;
}

bool CRSIStrategy::IsExitingOversold() const {
    return mPreviousZone == ERSIZone::OVERSOLD && mCurrentZone != ERSIZone::OVERSOLD &&
           mCurrentZone != ERSIZone::EXTREME_OVERSOLD;
}

bool CRSIStrategy::IsExitingOverbought() const {
    return mPreviousZone == ERSIZone::OVERBOUGHT && mCurrentZone != ERSIZone::OVERBOUGHT &&
           mCurrentZone != ERSIZone::EXTREME_OVERBOUGHT;
}

// ============================================================================
// Méthodes de calcul privées
// ============================================================================

double CRSIStrategy::CalculateRSI(const std::deque<double>& prices, int period) {
    if (prices.size() < static_cast<size_t>(period + 1)) {
        return 50.0; // Valeur neutre par défaut
    }

    // Calculer les gains et pertes
    std::vector<double> gains, losses;
    for (size_t i = prices.size() - period; i < prices.size(); ++i) {
        double change = prices[i] - prices[i - 1];
        gains.push_back(change > 0 ? change : 0.0);
        losses.push_back(change < 0 ? -change : 0.0);
    }

    // Calculer les moyennes avec le lissage de Wilder
    double avgGain = CalculateWildersSmoothing(std::deque<double>(gains.begin(), gains.end()), period);
    double avgLoss = CalculateWildersSmoothing(std::deque<double>(losses.begin(), losses.end()), period);

    if (avgLoss == 0.0) {
        return 100.0; // RSI maximum si pas de pertes
    }

    double rs = avgGain / avgLoss;
    return 100.0 - (100.0 / (1.0 + rs));
}

void CRSIStrategy::CalculateRSIValues(const std::deque<double>& prices, SRSIValues& outValues) {
    if (prices.size() < static_cast<size_t>(mParams.mRSIPeriod + 1)) {
        outValues.mIsValid = false;
        return;
    }

    // Calculer le RSI
    outValues.mRSI = CalculateRSI(prices, mParams.mRSIPeriod);
    outValues.mPreviousRSI = mCurrentRSIValues.mRSI;
    outValues.mRSIChange = outValues.mRSI - outValues.mPreviousRSI;

    // Calculer les moyennes de gains et pertes pour les métadonnées
    std::vector<double> gains, losses;
    for (size_t i = prices.size() - mParams.mRSIPeriod; i < prices.size(); ++i) {
        double change = prices[i] - prices[i - 1];
        gains.push_back(change > 0 ? change : 0.0);
        losses.push_back(change < 0 ? -change : 0.0);
    }

    outValues.mAverageGain = CalculateWildersSmoothing(std::deque<double>(gains.begin(), gains.end()), mParams.mRSIPeriod);
    outValues.mAverageLoss = CalculateWildersSmoothing(std::deque<double>(losses.begin(), losses.end()), mParams.mRSIPeriod);

    outValues.mTimestamp = std::chrono::system_clock::now();
    outValues.mPeriodCount = static_cast<int>(prices.size());
    outValues.mIsValid = true;
}

double CRSIStrategy::CalculateWildersSmoothing(const std::deque<double>& values, int period) {
    if (values.empty()) {
        return 0.0;
    }

    // Première moyenne simple
    double sum = 0.0;
    int count = std::min(period, static_cast<int>(values.size()));
    for (int i = 0; i < count; ++i) {
        sum += values[i];
    }

    return sum / count;
}

void CRSIStrategy::UpdateGainsAndLosses(double currentPrice, double previousPrice) {
    double change = currentPrice - previousPrice;

    if (change > 0) {
        mGains.push_back(change);
        mLosses.push_back(0.0);
    } else {
        mGains.push_back(0.0);
        mLosses.push_back(-change);
    }

    // Limiter la taille
    size_t maxSize = mParams.mRSIPeriod * 2;
    while (mGains.size() > maxSize) {
        mGains.pop_front();
        mLosses.pop_front();
    }
}

// ============================================================================
// Analyse des zones et signaux
// ============================================================================

ERSIZone CRSIStrategy::DetermineRSIZone(double rsi) const {
    if (rsi <= mParams.mExtremeOversold) {
        return ERSIZone::EXTREME_OVERSOLD;
    } else if (rsi <= mParams.mOversoldThreshold) {
        return ERSIZone::OVERSOLD;
    } else if (rsi < 50.0) {
        return ERSIZone::NEUTRAL_LOW;
    } else if (rsi < mParams.mOverboughtThreshold) {
        return ERSIZone::NEUTRAL_HIGH;
    } else if (rsi < mParams.mExtremeOverbought) {
        return ERSIZone::OVERBOUGHT;
    } else {
        return ERSIZone::EXTREME_OVERBOUGHT;
    }
}

ERSISignalType CRSIStrategy::DetectZoneTransition(ERSIZone currentZone, ERSIZone previousZone, const SRSIValues& values) const {
    // Signaux d'entrée en zone
    if (currentZone == ERSIZone::OVERSOLD && previousZone != ERSIZone::OVERSOLD &&
        previousZone != ERSIZone::EXTREME_OVERSOLD) {
        return ERSISignalType::BUY_OVERSOLD;
    }

    if (currentZone == ERSIZone::OVERBOUGHT && previousZone != ERSIZone::OVERBOUGHT &&
        previousZone != ERSIZone::EXTREME_OVERBOUGHT) {
        return ERSISignalType::SELL_OVERBOUGHT;
    }

    // Signaux de sortie de zone
    if ((previousZone == ERSIZone::OVERSOLD || previousZone == ERSIZone::EXTREME_OVERSOLD) &&
        (currentZone == ERSIZone::NEUTRAL_LOW || currentZone == ERSIZone::NEUTRAL_HIGH)) {
        return ERSISignalType::BUY_OVERSOLD_EXIT;
    }

    if ((previousZone == ERSIZone::OVERBOUGHT || previousZone == ERSIZone::EXTREME_OVERBOUGHT) &&
        (currentZone == ERSIZone::NEUTRAL_HIGH || currentZone == ERSIZone::NEUTRAL_LOW)) {
        return ERSISignalType::SELL_OVERBOUGHT_EXIT;
    }

    // Signaux de retournement extrême
    if (currentZone == ERSIZone::EXTREME_OVERSOLD && IsRSIReversing()) {
        return ERSISignalType::EXTREME_REVERSAL_BUY;
    }

    if (currentZone == ERSIZone::EXTREME_OVERBOUGHT && IsRSIReversing()) {
        return ERSISignalType::EXTREME_REVERSAL_SELL;
    }

    return ERSISignalType::NONE;
}

ERSISignalType CRSIStrategy::DetectMomentumSignal(const SRSIValues& current, const SRSIValues& previous) const {
    // Détecter l'accélération du momentum haussier
    if (current.mRSIChange > mMinRSIChange && current.mRSIChange > previous.mRSIChange &&
        current.mRSI > 50.0) {
        return ERSISignalType::MOMENTUM_BULLISH;
    }

    // Détecter l'accélération du momentum baissier
    if (current.mRSIChange < -mMinRSIChange && current.mRSIChange < previous.mRSIChange &&
        current.mRSI < 50.0) {
        return ERSISignalType::MOMENTUM_BEARISH;
    }

    return ERSISignalType::NONE;
}

bool CRSIStrategy::IsRSIReversing(size_t periods) const {
    if (mRSIHistory.size() < periods + 1) {
        return false;
    }

    // Vérifier si le RSI change de direction
    bool wasIncreasing = true;
    bool wasDecreasing = true;

    for (size_t i = mRSIHistory.size() - periods; i < mRSIHistory.size() - 1; ++i) {
        if (mRSIHistory[i + 1].mRSI <= mRSIHistory[i].mRSI) {
            wasIncreasing = false;
        }
        if (mRSIHistory[i + 1].mRSI >= mRSIHistory[i].mRSI) {
            wasDecreasing = false;
        }
    }

    // Retournement si la tendance récente change
    double currentChange = mCurrentRSIValues.mRSIChange;
    return (wasIncreasing && currentChange < 0) || (wasDecreasing && currentChange > 0);
}

// ============================================================================
// Détection de divergence
// ============================================================================

SRSIDivergence CRSIStrategy::AnalyzeDivergence(const std::deque<double>& prices, const std::deque<SRSIValues>& rsiHistory, size_t lookback) const {
    SRSIDivergence divergence;

    if (prices.size() < lookback || rsiHistory.size() < lookback) {
        return divergence;
    }

    // Trouver les hauts et bas récents
    std::vector<size_t> priceHighs, priceLows, rsiHighs, rsiLows;

    if (!FindPriceHighsAndLows(prices, lookback, priceHighs, priceLows) ||
        !FindRSIHighsAndLows(rsiHistory, lookback, rsiHighs, rsiLows)) {
        return divergence;
    }

    // Analyser la divergence haussière (prix fait des bas plus bas, RSI fait des bas plus hauts)
    if (priceLows.size() >= 2 && rsiLows.size() >= 2) {
        size_t lastPriceLow = priceLows.back();
        size_t prevPriceLow = priceLows[priceLows.size() - 2];
        size_t lastRSILow = rsiLows.back();
        size_t prevRSILow = rsiLows[rsiLows.size() - 2];

        if (prices[lastPriceLow] < prices[prevPriceLow] &&
            rsiHistory[lastRSILow].mRSI > rsiHistory[prevRSILow].mRSI) {
            divergence.mIsBullish = true;
            divergence.mPriceLow = prices[lastPriceLow];
            divergence.mRSILow = rsiHistory[lastRSILow].mRSI;
            divergence.mStrength = CalculateDivergenceStrength(divergence);
        }
    }

    // Analyser la divergence baissière (prix fait des hauts plus hauts, RSI fait des hauts plus bas)
    if (priceHighs.size() >= 2 && rsiHighs.size() >= 2) {
        size_t lastPriceHigh = priceHighs.back();
        size_t prevPriceHigh = priceHighs[priceHighs.size() - 2];
        size_t lastRSIHigh = rsiHighs.back();
        size_t prevRSIHigh = rsiHighs[rsiHighs.size() - 2];

        if (prices[lastPriceHigh] > prices[prevPriceHigh] &&
            rsiHistory[lastRSIHigh].mRSI < rsiHistory[prevRSIHigh].mRSI) {
            divergence.mIsBearish = true;
            divergence.mPriceHigh = prices[lastPriceHigh];
            divergence.mRSIHigh = rsiHistory[lastRSIHigh].mRSI;
            divergence.mStrength = CalculateDivergenceStrength(divergence);
        }
    }

    if (divergence.mIsBullish || divergence.mIsBearish) {
        divergence.mDetectedAt = std::chrono::system_clock::now();
        divergence.mPeriodsSpan = static_cast<int>(lookback);
    }

    return divergence;
}

bool CRSIStrategy::FindPriceHighsAndLows(const std::deque<double>& prices, size_t lookback,
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

bool CRSIStrategy::FindRSIHighsAndLows(const std::deque<SRSIValues>& rsiHistory, size_t lookback,
                                      std::vector<size_t>& highs, std::vector<size_t>& lows) const {
    if (rsiHistory.size() < lookback + 2) {
        return false;
    }

    size_t start = rsiHistory.size() - lookback;

    // Chercher les hauts et bas locaux RSI avec une fenêtre de 3
    for (size_t i = start + 1; i < rsiHistory.size() - 1; ++i) {
        // Haut local RSI
        if (rsiHistory[i].mRSI > rsiHistory[i-1].mRSI &&
            rsiHistory[i].mRSI > rsiHistory[i+1].mRSI) {
            highs.push_back(i);
        }
        // Bas local RSI
        if (rsiHistory[i].mRSI < rsiHistory[i-1].mRSI &&
            rsiHistory[i].mRSI < rsiHistory[i+1].mRSI) {
            lows.push_back(i);
        }
    }

    return !highs.empty() && !lows.empty();
}

double CRSIStrategy::CalculateDivergenceStrength(const SRSIDivergence& divergence) const {
    // Calculer la force de la divergence basée sur l'écart
    double strength = 0.5;

    if (divergence.mIsBullish) {
        // Plus le RSI est éloigné de la survente lors de la divergence, plus c'est fort
        strength = std::min(1.0, (50.0 - divergence.mRSILow) / 30.0);
    } else if (divergence.mIsBearish) {
        // Plus le RSI est éloigné du surachat lors de la divergence, plus c'est fort
        strength = std::min(1.0, (divergence.mRSIHigh - 50.0) / 30.0);
    }

    return std::max(0.1, strength);
}

// ============================================================================
// Gestion des données
// ============================================================================

void CRSIStrategy::UpdateClosePrices(const std::vector<API::SKline>& klines) {
    for (const auto& kline : klines) {
        if (!mClosePrices.empty()) {
            UpdateGainsAndLosses(kline.mClose, mClosePrices.back());
        }
        mClosePrices.push_back(kline.mClose);
    }

    // Limiter la taille de l'historique
    size_t maxSize = std::max(mParams.mRSIPeriod * 3, 200);
    while (mClosePrices.size() > maxSize) {
        mClosePrices.pop_front();
    }
}

void CRSIStrategy::UpdateRSIHistory() {
    mRSIHistory.push_back(mCurrentRSIValues);

    // Limiter la taille de l'historique
    size_t maxSize = 500;
    while (mRSIHistory.size() > maxSize) {
        mRSIHistory.pop_front();
    }
}

void CRSIStrategy::AddSignalToHistory(ERSISignalType signalType, const SRSIValues& values,
                                     double price, const std::string& description) {
    SRSISignalHistory signal;
    signal.mType = signalType;
    signal.mValues = values;
    signal.mZone = mCurrentZone;
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

void CRSIStrategy::CleanupOldData() {
    // Cette méthode est appelée périodiquement pour nettoyer les anciennes données
    // La limitation est déjà gérée dans les méthodes Update*
}

// ============================================================================
// Validation et contrôles
// ============================================================================

bool CRSIStrategy::HasSufficientData() const {
    return mClosePrices.size() >= static_cast<size_t>(mParams.mRSIPeriod + 1);
}

bool CRSIStrategy::IsValidRSIValues(const SRSIValues& values) const {
    return values.mIsValid && values.mRSI >= 0.0 && values.mRSI <= 100.0;
}

bool CRSIStrategy::ShouldGenerateSignal(ERSISignalType signalType) const {
    // Éviter de générer le même signal trop fréquemment
    if (!mSignalHistory.empty()) {
        const auto& lastSignal = mSignalHistory.back();
        auto now = std::chrono::system_clock::now();
        auto timeDiff = std::chrono::duration_cast<std::chrono::minutes>(now - lastSignal.mTimestamp);

        if (lastSignal.mType == signalType && timeDiff.count() < 10) {
            return false; // Même signal il y a moins de 10 minutes
        }
    }

    return true;
}

bool CRSIStrategy::IsSignalFilterPassed(ERSISignalType signalType, const SRSIValues& values) const {
    // Vérifier si le changement de RSI est suffisant
    if (std::abs(values.mRSIChange) < mMinRSIChange / 2.0) {
        return false;
    }

    return true;
}

// ============================================================================
// Gestion des positions
// ============================================================================

void CRSIStrategy::UpdatePositionState(const SPosition& position) {
    // Mettre à jour l'état interne basé sur la position
    if (position.mId == mCurrentPositionId) {
        // Vérifier si la position doit être fermée basée sur les conditions RSI
        if (ShouldClosePosition(mCurrentRSIValues)) {
            // Envoyer un signal de fermeture (implémentation dépendante du système)
            std::cout << "[RSIStrategy] Position should be closed based on RSI conditions" << std::endl;
        }
    }
}

double CRSIStrategy::CalculateStopLoss(double entryPrice, API::EOrderSide side) const {
    double stopLossPercent = mParams.mStopLossPercent / 100.0;

    if (side == API::EOrderSide::BUY) {
        return entryPrice * (1.0 - stopLossPercent);
    } else {
        return entryPrice * (1.0 + stopLossPercent);
    }
}

double CRSIStrategy::CalculateTakeProfit(double entryPrice, API::EOrderSide side) const {
    double takeProfitPercent = mParams.mTakeProfitPercent / 100.0;

    if (side == API::EOrderSide::BUY) {
        return entryPrice * (1.0 + takeProfitPercent);
    } else {
        return entryPrice * (1.0 - takeProfitPercent);
    }
}

bool CRSIStrategy::ShouldClosePosition(const SRSIValues& values) const {
    if (!mInPosition) {
        return false;
    }

    // Fermer les positions longues si RSI devient surachetè
    if (mCurrentPositionSide == API::EOrderSide::BUY &&
        values.mRSI >= mParams.mOverboughtThreshold) {
        return true;
    }

    // Fermer les positions courtes si RSI devient survendu
    if (mCurrentPositionSide == API::EOrderSide::SELL &&
        values.mRSI <= mParams.mOversoldThreshold) {
        return true;
    }

    // Fermer en cas de retournement fort
    if (IsRSIReversing(2)) {
        return true;
    }

    return false;
}

// ============================================================================
// Utilitaires
// ============================================================================

std::string CRSIStrategy::SignalTypeToString(ERSISignalType type) const {
    switch (type) {
        case ERSISignalType::NONE: return "None";
        case ERSISignalType::BUY_OVERSOLD: return "Buy Oversold";
        case ERSISignalType::SELL_OVERBOUGHT: return "Sell Overbought";
        case ERSISignalType::BUY_OVERSOLD_EXIT: return "Buy Oversold Exit";
        case ERSISignalType::SELL_OVERBOUGHT_EXIT: return "Sell Overbought Exit";
        case ERSISignalType::DIVERGENCE_BULLISH: return "Bullish Divergence";
        case ERSISignalType::DIVERGENCE_BEARISH: return "Bearish Divergence";
        case ERSISignalType::MOMENTUM_BULLISH: return "Bullish Momentum";
        case ERSISignalType::MOMENTUM_BEARISH: return "Bearish Momentum";
        case ERSISignalType::EXTREME_REVERSAL_BUY: return "Extreme Reversal Buy";
        case ERSISignalType::EXTREME_REVERSAL_SELL: return "Extreme Reversal Sell";
        default: return "Unknown";
    }
}

std::string CRSIStrategy::ZoneToString(ERSIZone zone) const {
    switch (zone) {
        case ERSIZone::EXTREME_OVERSOLD: return "Extreme Oversold";
        case ERSIZone::OVERSOLD: return "Oversold";
        case ERSIZone::NEUTRAL_LOW: return "Neutral Low";
        case ERSIZone::NEUTRAL_HIGH: return "Neutral High";
        case ERSIZone::OVERBOUGHT: return "Overbought";
        case ERSIZone::EXTREME_OVERBOUGHT: return "Extreme Overbought";
        default: return "Unknown";
    }
}

ERSISignalType CRSIStrategy::StringToSignalType(const std::string& typeStr) const {
    if (typeStr == "Buy Oversold") return ERSISignalType::BUY_OVERSOLD;
    if (typeStr == "Sell Overbought") return ERSISignalType::SELL_OVERBOUGHT;
    if (typeStr == "Buy Oversold Exit") return ERSISignalType::BUY_OVERSOLD_EXIT;
    if (typeStr == "Sell Overbought Exit") return ERSISignalType::SELL_OVERBOUGHT_EXIT;
    if (typeStr == "Bullish Divergence") return ERSISignalType::DIVERGENCE_BULLISH;
    if (typeStr == "Bearish Divergence") return ERSISignalType::DIVERGENCE_BEARISH;
    if (typeStr == "Bullish Momentum") return ERSISignalType::MOMENTUM_BULLISH;
    if (typeStr == "Bearish Momentum") return ERSISignalType::MOMENTUM_BEARISH;
    if (typeStr == "Extreme Reversal Buy") return ERSISignalType::EXTREME_REVERSAL_BUY;
    if (typeStr == "Extreme Reversal Sell") return ERSISignalType::EXTREME_REVERSAL_SELL;
    return ERSISignalType::NONE;
}

ERSIZone CRSIStrategy::StringToZone(const std::string& zoneStr) const {
    if (zoneStr == "Extreme Oversold") return ERSIZone::EXTREME_OVERSOLD;
    if (zoneStr == "Oversold") return ERSIZone::OVERSOLD;
    if (zoneStr == "Neutral Low") return ERSIZone::NEUTRAL_LOW;
    if (zoneStr == "Neutral High") return ERSIZone::NEUTRAL_HIGH;
    if (zoneStr == "Overbought") return ERSIZone::OVERBOUGHT;
    if (zoneStr == "Extreme Overbought") return ERSIZone::EXTREME_OVERBOUGHT;
    return ERSIZone::NEUTRAL_LOW;
}

void CRSIStrategy::LogSignal(ERSISignalType signalType, const SRSIValues& values, double price) const {
    std::cout << "[RSIStrategy] Signal: " << SignalTypeToString(signalType)
              << " | Price: " << std::fixed << std::setprecision(4) << price
              << " | RSI: " << std::setprecision(2) << values.mRSI
              << " | Change: " << values.mRSIChange
              << " | Zone: " << ZoneToString(mCurrentZone) << std::endl;
}

// ============================================================================
// Métriques
// ============================================================================

void CRSIStrategy::UpdateSignalStatistics(ERSISignalType signalType, bool successful) {
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
    if (signalType == ERSISignalType::BUY_OVERSOLD || signalType == ERSISignalType::BUY_OVERSOLD_EXIT) {
        mOversoldEntries++;
    } else if (signalType == ERSISignalType::SELL_OVERBOUGHT || signalType == ERSISignalType::SELL_OVERBOUGHT_EXIT) {
        mOverboughtEntries++;
    } else if (signalType == ERSISignalType::DIVERGENCE_BULLISH || signalType == ERSISignalType::DIVERGENCE_BEARISH) {
        mDivergenceSignals++;
    }
}

void CRSIStrategy::UpdateZoneStatistics(ERSIZone zone) {
    std::lock_guard<std::mutex> guard(mMetricsMutex);
    mZoneTimeSpent[zone]++;
}

void CRSIStrategy::CalculateAdvancedMetrics() {
    // Calculer des métriques avancées comme le Sharpe ratio, etc.
    // Implémentation future
}

void CRSIStrategy::ResetMetrics() {
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
    mZoneTimeSpent.clear();
    mOversoldEntries = 0;
    mOverboughtEntries = 0;
    mDivergenceSignals = 0;
}

// ============================================================================
// CRSIStrategyFactory - Implémentation des méthodes statiques
// ============================================================================

std::shared_ptr<CRSIStrategy> CRSIStrategyFactory::CreateDefault() {
    return std::make_shared<CRSIStrategy>(GetDefaultParams());
}

std::shared_ptr<CRSIStrategy> CRSIStrategyFactory::CreateScalping() {
    return std::make_shared<CRSIStrategy>(GetScalpingParams());
}

std::shared_ptr<CRSIStrategy> CRSIStrategyFactory::CreateSwing() {
    return std::make_shared<CRSIStrategy>(GetSwingParams());
}

std::shared_ptr<CRSIStrategy> CRSIStrategyFactory::CreateConservative() {
    return std::make_shared<CRSIStrategy>(GetConservativeParams());
}

std::shared_ptr<CRSIStrategy> CRSIStrategyFactory::CreateAggressive() {
    return std::make_shared<CRSIStrategy>(GetAggressiveParams());
}

std::shared_ptr<CRSIStrategy> CRSIStrategyFactory::CreateMeanReversion() {
    return std::make_shared<CRSIStrategy>(GetMeanReversionParams());
}

std::shared_ptr<CRSIStrategy> CRSIStrategyFactory::CreateDivergenceHunter() {
    return std::make_shared<CRSIStrategy>(GetDivergenceParams());
}

std::shared_ptr<CRSIStrategy> CRSIStrategyFactory::CreateCustom(const SRSIParams& params) {
    return std::make_shared<CRSIStrategy>(params);
}

std::shared_ptr<CRSIStrategy> CRSIStrategyFactory::CreateFromConfig(const nlohmann::json& config) {
    auto strategy = std::make_shared<CRSIStrategy>();
    strategy->Configure(config);
    return strategy;
}

SRSIParams CRSIStrategyFactory::GetDefaultParams() {
    SRSIParams params;
    params.mRSIPeriod = 14;
    params.mOversoldThreshold = 30.0;
    params.mOverboughtThreshold = 70.0;
    params.mExtremeOversold = 20.0;
    params.mExtremeOverbought = 80.0;
    params.mPositionSize = 0.1;
    params.mStopLossPercent = 2.0;
    params.mTakeProfitPercent = 4.0;
    params.mUseDivergence = true;
    return params;
}

SRSIParams CRSIStrategyFactory::GetScalpingParams() {
    SRSIParams params;
    params.mRSIPeriod = 7;
    params.mOversoldThreshold = 25.0;
    params.mOverboughtThreshold = 75.0;
    params.mExtremeOversold = 15.0;
    params.mExtremeOverbought = 85.0;
    params.mPositionSize = 0.05;
    params.mStopLossPercent = 0.5;
    params.mTakeProfitPercent = 1.0;
    params.mRSIChangeThreshold = 3.0;
    params.mUseDivergence = false;
    return params;
}

SRSIParams CRSIStrategyFactory::GetSwingParams() {
    SRSIParams params;
    params.mRSIPeriod = 21;
    params.mOversoldThreshold = 35.0;
    params.mOverboughtThreshold = 65.0;
    params.mExtremeOversold = 25.0;
    params.mExtremeOverbought = 75.0;
    params.mPositionSize = 0.15;
    params.mStopLossPercent = 3.0;
    params.mTakeProfitPercent = 6.0;
    params.mUseDivergence = true;
    return params;
}

SRSIParams CRSIStrategyFactory::GetConservativeParams() {
    SRSIParams params;
    params.mRSIPeriod = 14;
    params.mOversoldThreshold = 25.0;
    params.mOverboughtThreshold = 75.0;
    params.mExtremeOversold = 15.0;
    params.mExtremeOverbought = 85.0;
    params.mPositionSize = 0.08;
    params.mStopLossPercent = 1.5;
    params.mTakeProfitPercent = 3.0;
    params.mRSIChangeThreshold = 8.0;
    return params;
}

SRSIParams CRSIStrategyFactory::GetAggressiveParams() {
    SRSIParams params;
    params.mRSIPeriod = 10;
    params.mOversoldThreshold = 35.0;
    params.mOverboughtThreshold = 65.0;
    params.mExtremeOversold = 25.0;
    params.mExtremeOverbought = 75.0;
    params.mPositionSize = 0.2;
    params.mStopLossPercent = 3.0;
    params.mTakeProfitPercent = 6.0;
    params.mRSIChangeThreshold = 3.0;
    return params;
}

SRSIParams CRSIStrategyFactory::GetMeanReversionParams() {
    SRSIParams params;
    params.mRSIPeriod = 14;
    params.mOversoldThreshold = 30.0;
    params.mOverboughtThreshold = 70.0;
    params.mExtremeOversold = 20.0;
    params.mExtremeOverbought = 80.0;
    params.mPositionSize = 0.12;
    params.mStopLossPercent = 2.5;
    params.mTakeProfitPercent = 5.0;
    params.mUseDivergence = false;
    return params;
}

SRSIParams CRSIStrategyFactory::GetDivergenceParams() {
    SRSIParams params;
    params.mRSIPeriod = 14;
    params.mOversoldThreshold = 40.0;
    params.mOverboughtThreshold = 60.0;
    params.mExtremeOversold = 30.0;
    params.mExtremeOverbought = 70.0;
    params.mPositionSize = 0.1;
    params.mStopLossPercent = 2.0;
    params.mTakeProfitPercent = 4.0;
    params.mUseDivergence = true;
    params.mRSIChangeThreshold = 10.0;
    return params;
}

SRSIParams CRSIStrategyFactory::GetCryptoParams() {
    SRSIParams params = GetDefaultParams();
    params.mOversoldThreshold = 25.0;
    params.mOverboughtThreshold = 75.0;
    params.mStopLossPercent = 3.0;
    params.mTakeProfitPercent = 6.0;
    return params;
}

SRSIParams CRSIStrategyFactory::GetForexParams() {
    SRSIParams params = GetDefaultParams();
    params.mRSIPeriod = 14;
    params.mStopLossPercent = 1.0;
    params.mTakeProfitPercent = 2.0;
    params.mRSIChangeThreshold = 3.0;
    return params;
}

SRSIParams CRSIStrategyFactory::GetStockParams() {
    SRSIParams params = GetDefaultParams();
    params.mRSIPeriod = 14;
    params.mOversoldThreshold = 30.0;
    params.mOverboughtThreshold = 70.0;
    params.mStopLossPercent = 2.5;
    params.mTakeProfitPercent = 5.0;
    return params;
}

} // namespace Strategy