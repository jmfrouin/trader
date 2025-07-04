
//
// Created by Jean-Michel Frouin on 04/07/2025.
//

#include "StrategyEngine.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace Strategy {

CStrategyEngine::CStrategyEngine()
    : mTotalExposure(0.0)
    , mExchangeAPI(nullptr)
    , mRiskManager(nullptr)
    , mSignalCallback(nullptr)
    , mPositionCallback(nullptr)
    , mErrorCallback(nullptr) {
}

CStrategyEngine::~CStrategyEngine() {
    // Arrêter toutes les stratégies avant la destruction
    std::lock_guard<std::mutex> guard(mStrategiesMutex);
    for (auto& pair : mStrategies) {
        try {
            if (pair.second) {
                pair.second->Stop();
            }
        } catch (const std::exception& e) {
            std::cerr << "Error stopping strategy " << pair.first << ": " << e.what() << std::endl;
        }
    }
}

void CStrategyEngine::RegisterStrategy(std::shared_ptr<IStrategy> strategy) {
    if (!strategy) {
        throw std::invalid_argument("Cannot register null strategy");
    }

    std::lock_guard<std::mutex> guard(mStrategiesMutex);
    std::string name = strategy->GetName();

    if (mStrategies.find(name) != mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + name + "' already exists");
    }

    // Configurer la stratégie avec l'API d'échange si disponible
    if (mExchangeAPI) {
        strategy->SetExchangeAPI(mExchangeAPI);
    }

    // Initialiser la stratégie
    try {
        strategy->Initialize();
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to initialize strategy '" + name + "': " + e.what());
    }

    // Enregistrer la stratégie
    mStrategies[name] = strategy;
    mStrategyStates[name] = EStrategyState::INACTIVE;

    // Initialiser les statistiques
    SStrategyStatistics stats;
    stats.mStrategyName = name;
    stats.mStartTime = std::chrono::system_clock::now();
    mStrategyStats[name] = stats;

    // Initialiser les positions pour cette stratégie
    mStrategyPositions[name] = std::vector<std::string>();
}

void CStrategyEngine::RemoveStrategy(const std::string& strategyName) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    // Arrêter la stratégie avant de la supprimer
    try {
        if (it->second) {
            it->second->Stop();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error stopping strategy " << strategyName << ": " << e.what() << std::endl;
    }

    // Supprimer toutes les données associées
    mStrategies.erase(it);
    mStrategyStates.erase(strategyName);
    mStrategyStats.erase(strategyName);
    mStrategyPositions.erase(strategyName);

    // Supprimer les paramètres si ils existent
    auto paramIt = mStrategyParams.find(strategyName);
    if (paramIt != mStrategyParams.end()) {
        mStrategyParams.erase(paramIt);
    }
}

std::shared_ptr<IStrategy> CStrategyEngine::GetStrategy(const std::string& strategyName) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    return it->second;
}

std::vector<std::string> CStrategyEngine::GetAvailableStrategies() const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    std::vector<std::string> result;
    result.reserve(mStrategies.size());

    for (const auto& pair : mStrategies) {
        result.push_back(pair.first);
    }

    return result;
}

std::vector<std::string> CStrategyEngine::GetActiveStrategies() const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    std::vector<std::string> result;

    for (const auto& pair : mStrategyStates) {
        if (pair.second == EStrategyState::ACTIVE) {
            result.push_back(pair.first);
        }
    }

    return result;
}

void CStrategyEngine::StartStrategy(const std::string& strategyName) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    try {
        it->second->Start();
        mStrategyStates[strategyName] = EStrategyState::ACTIVE;
    } catch (const std::exception& e) {
        mStrategyStates[strategyName] = EStrategyState::ERROR;
        NotifyError(strategyName, "Failed to start strategy: " + std::string(e.what()));
        throw;
    }
}

void CStrategyEngine::StopStrategy(const std::string& strategyName) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    try {
        it->second->Stop();
        mStrategyStates[strategyName] = EStrategyState::INACTIVE;
    } catch (const std::exception& e) {
        mStrategyStates[strategyName] = EStrategyState::ERROR;
        NotifyError(strategyName, "Failed to stop strategy: " + std::string(e.what()));
        throw;
    }
}

void CStrategyEngine::PauseStrategy(const std::string& strategyName) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    try {
        it->second->Pause();
        mStrategyStates[strategyName] = EStrategyState::PAUSED;
    } catch (const std::exception& e) {
        mStrategyStates[strategyName] = EStrategyState::ERROR;
        NotifyError(strategyName, "Failed to pause strategy: " + std::string(e.what()));
        throw;
    }
}

void CStrategyEngine::ResumeStrategy(const std::string& strategyName) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    try {
        it->second->Resume();
        mStrategyStates[strategyName] = EStrategyState::ACTIVE;
    } catch (const std::exception& e) {
        mStrategyStates[strategyName] = EStrategyState::ERROR;
        NotifyError(strategyName, "Failed to resume strategy: " + std::string(e.what()));
        throw;
    }
}

EStrategyState CStrategyEngine::GetStrategyState(const std::string& strategyName) const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategyStates.find(strategyName);
    if (it == mStrategyStates.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    return it->second;
}

SSignal CStrategyEngine::ExecuteStrategy(const std::string& strategyName,
                                        const std::vector<API::SKline>& klines,
                                        const API::STicker& ticker) {
    std::shared_ptr<IStrategy> strategy;
    EStrategyState state;

    {
        std::lock_guard<std::mutex> guard(mStrategiesMutex);
        auto it = mStrategies.find(strategyName);
        if (it == mStrategies.end()) {
            throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
        }
        strategy = it->second;

        auto stateIt = mStrategyStates.find(strategyName);
        state = (stateIt != mStrategyStates.end()) ? stateIt->second : EStrategyState::INACTIVE;
    }

    if (state != EStrategyState::ACTIVE) {
        SSignal signal;
        signal.mType = ESignalType::HOLD;
        signal.mStrategyName = strategyName;
        signal.mMessage = "Strategy is not active";
        signal.mTimestamp = std::chrono::system_clock::now();
        return signal;
    }

    try {
        auto startTime = std::chrono::high_resolution_clock::now();
        SSignal signal = strategy->Update(klines, ticker);
        auto endTime = std::chrono::high_resolution_clock::now();

        // Mettre à jour le temps d'exécution
        auto executionTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        // Valider le signal
        if (!ValidateStrategy(strategyName) || !strategy->ValidateSignal(signal)) {
            signal.mType = ESignalType::HOLD;
            signal.mMessage = "Signal validation failed";
        }

        // Ajouter des métadonnées au signal
        signal.mStrategyName = strategyName;
        signal.mTimestamp = std::chrono::system_clock::now();

        // Notifier le signal
        NotifyStrategySignal(strategyName, signal);

        return signal;
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> guard(mStrategiesMutex);
        mStrategyStates[strategyName] = EStrategyState::ERROR;

        NotifyError(strategyName, "Strategy execution failed: " + std::string(e.what()));

        SSignal errorSignal;
        errorSignal.mType = ESignalType::HOLD;
        errorSignal.mStrategyName = strategyName;
        errorSignal.mMessage = "Execution error: " + std::string(e.what());
        errorSignal.mTimestamp = std::chrono::system_clock::now();

        return errorSignal;
    }
}

void CStrategyEngine::ExecuteAllStrategies(const std::vector<API::SKline>& klines,
                                          const API::STicker& ticker) {
    std::vector<std::string> activeStrategies = GetActiveStrategies();

    for (const auto& strategyName : activeStrategies) {
        try {
            ExecuteStrategy(strategyName, klines, ticker);
        } catch (const std::exception& e) {
            std::cerr << "Error executing strategy " << strategyName << ": " << e.what() << std::endl;
        }
    }
}

void CStrategyEngine::ConfigureStrategy(const std::string& strategyName, const nlohmann::json& config) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    try {
        it->second->Configure(config);
    } catch (const std::exception& e) {
        NotifyError(strategyName, "Configuration failed: " + std::string(e.what()));
        throw;
    }
}

nlohmann::json CStrategyEngine::GetStrategyConfig(const std::string& strategyName) const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    return it->second->GetCurrentConfig();
}

void CStrategyEngine::SetStrategyParams(const std::string& strategyName, const SStrategyParams& params) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    if (mStrategies.find(strategyName) == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    mStrategyParams[strategyName] = params;
}

SStrategyParams CStrategyEngine::GetStrategyParams(const std::string& strategyName) const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategyParams.find(strategyName);
    if (it == mStrategyParams.end()) {
        throw std::runtime_error("Strategy parameters for '" + strategyName + "' not found");
    }

    return it->second;
}

void CStrategyEngine::RegisterPosition(const SPosition& position) {
    std::lock_guard<std::mutex> posGuard(mPositionsMutex);
    std::lock_guard<std::mutex> stratGuard(mStrategiesMutex);

    if (position.mId.empty()) {
        throw std::invalid_argument("Position ID cannot be empty");
    }

    if (position.mStrategyName.empty()) {
        throw std::invalid_argument("Position strategy name cannot be empty");
    }

    // Vérifier que la stratégie existe
    if (mStrategies.find(position.mStrategyName) == mStrategies.end()) {
        throw std::runtime_error("Strategy '" + position.mStrategyName + "' not found");
    }

    // Stocker la position
    mOpenPositions[position.mId] = position;
    mPositionToStrategy[position.mId] = position.mStrategyName;
    mStrategyPositions[position.mStrategyName].push_back(position.mId);

    // Notifier la stratégie
    auto stratIt = mStrategies.find(position.mStrategyName);
    if (stratIt != mStrategies.end()) {
        try {
            stratIt->second->OnPositionOpened(position);
        } catch (const std::exception& e) {
            std::cerr << "Error notifying strategy " << position.mStrategyName
                      << " of position opened: " << e.what() << std::endl;
        }
    }

    // Notifier via callback
    NotifyPositionUpdate(position.mStrategyName, position);
}

void CStrategyEngine::ClosePosition(const std::string& positionId, double exitPrice, double pnl) {
    SPosition position;
    std::string strategyName;

    {
        std::lock_guard<std::mutex> guard(mPositionsMutex);

        auto posIt = mOpenPositions.find(positionId);
        if (posIt == mOpenPositions.end()) {
            throw std::runtime_error("Position with ID '" + positionId + "' not found");
        }

        position = posIt->second;
        mOpenPositions.erase(posIt);

        auto stratIt = mPositionToStrategy.find(positionId);
        if (stratIt != mPositionToStrategy.end()) {
            strategyName = stratIt->second;
            mPositionToStrategy.erase(stratIt);

            // Supprimer de la liste des positions de la stratégie
            auto& strategyPositions = mStrategyPositions[strategyName];
            strategyPositions.erase(
                std::remove(strategyPositions.begin(), strategyPositions.end(), positionId),
                strategyPositions.end()
            );
        }
    }

    // Mettre à jour les statistiques
    if (!strategyName.empty()) {
        UpdateStrategyStatistics(strategyName, position, pnl);
    }

    // Notifier la stratégie
    if (!strategyName.empty()) {
        std::lock_guard<std::mutex> guard(mStrategiesMutex);
        auto stratIt = mStrategies.find(strategyName);
        if (stratIt != mStrategies.end()) {
            try {
                stratIt->second->OnPositionClosed(position, exitPrice, pnl);
            } catch (const std::exception& e) {
                std::cerr << "Error notifying strategy " << strategyName
                          << " of position closed: " << e.what() << std::endl;
            }
        }
    }

    // Notifier via callback
    NotifyPositionUpdate(strategyName, position);
}

void CStrategyEngine::UpdatePosition(const std::string& positionId, double currentPrice) {
    std::lock_guard<std::mutex> guard(mPositionsMutex);

    auto it = mOpenPositions.find(positionId);
    if (it == mOpenPositions.end()) {
        throw std::runtime_error("Position with ID '" + positionId + "' not found");
    }

    it->second.mCurrentPrice = currentPrice;

    // Calculer le PnL non réalisé
    double priceDiff = currentPrice - it->second.mEntryPrice;
    if (it->second.mSide == API::EOrderSide::SELL) {
        priceDiff = -priceDiff;
    }
    it->second.mUnrealizedPnL = priceDiff * it->second.mQuantity - it->second.mCommission;

    // Notifier la stratégie
    std::string strategyName = mPositionToStrategy[positionId];
    if (!strategyName.empty()) {
        std::lock_guard<std::mutex> stratGuard(mStrategiesMutex);
        auto stratIt = mStrategies.find(strategyName);
        if (stratIt != mStrategies.end()) {
            try {
                stratIt->second->OnPositionUpdated(it->second);
            } catch (const std::exception& e) {
                std::cerr << "Error notifying strategy " << strategyName
                          << " of position updated: " << e.what() << std::endl;
            }
        }
    }

    // Notifier via callback
    NotifyPositionUpdate(strategyName, it->second);
}

std::vector<SPosition> CStrategyEngine::GetOpenPositions() const {
    std::lock_guard<std::mutex> guard(mPositionsMutex);

    std::vector<SPosition> result;
    result.reserve(mOpenPositions.size());

    for (const auto& pair : mOpenPositions) {
        result.push_back(pair.second);
    }

    return result;
}

std::vector<SPosition> CStrategyEngine::GetOpenPositionsByStrategy(const std::string& strategyName) const {
    std::lock_guard<std::mutex> guard(mPositionsMutex);

    std::vector<SPosition> result;

    auto stratIt = mStrategyPositions.find(strategyName);
    if (stratIt != mStrategyPositions.end()) {
        result.reserve(stratIt->second.size());

        for (const auto& positionId : stratIt->second) {
            auto posIt = mOpenPositions.find(positionId);
            if (posIt != mOpenPositions.end()) {
                result.push_back(posIt->second);
            }
        }
    }

    return result;
}

std::vector<SPosition> CStrategyEngine::GetPositionsBySymbol(const std::string& symbol) const {
    std::lock_guard<std::mutex> guard(mPositionsMutex);

    std::vector<SPosition> result;

    for (const auto& pair : mOpenPositions) {
        if (pair.second.mSymbol == symbol) {
            result.push_back(pair.second);
        }
    }

    return result;
}

SStrategyStatistics CStrategyEngine::GetStrategyStatistics(const std::string& strategyName) const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategyStats.find(strategyName);
    if (it == mStrategyStats.end()) {
        throw std::runtime_error("Strategy statistics for '" + strategyName + "' not found");
    }

    return it->second;
}

std::map<std::string, SStrategyStatistics> CStrategyEngine::GetAllStrategyStatistics() const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);
    return mStrategyStats;
}

double CStrategyEngine::GetTotalPnL() const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    double totalPnL = 0.0;
    for (const auto& pair : mStrategyStats) {
        totalPnL += pair.second.mTotalPnL;
    }

    return totalPnL;
}

double CStrategyEngine::GetTotalPnLByStrategy(const std::string& strategyName) const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategyStats.find(strategyName);
    if (it == mStrategyStats.end()) {
        return 0.0;
    }

    return it->second.mTotalPnL;
}

void CStrategyEngine::SetExchangeAPI(std::shared_ptr<API::IExchangeAPI> api) {
    mExchangeAPI = api;

    // Mettre à jour toutes les stratégies avec la nouvelle API
    std::lock_guard<std::mutex> guard(mStrategiesMutex);
    for (auto& pair : mStrategies) {
        pair.second->SetExchangeAPI(api);
    }
}

std::shared_ptr<API::IExchangeAPI> CStrategyEngine::GetExchangeAPI() const {
    return mExchangeAPI;
}

void CStrategyEngine::SetRiskManager(std::shared_ptr<CRiskManager> riskManager) {
    mRiskManager = riskManager;
}

std::shared_ptr<CRiskManager> CStrategyEngine::GetRiskManager() const {
    return mRiskManager;
}

void CStrategyEngine::SetStrategyCallback(StrategyCallback callback) {
    std::lock_guard<std::mutex> guard(mCallbacksMutex);
    mStrategyCallback = callback;
}

void CStrategyEngine::SetPositionCallback(PositionCallback callback) {
    std::lock_guard<std::mutex> guard(mCallbacksMutex);
    mPositionCallback = callback;
}

void CStrategyEngine::SetErrorCallback(ErrorCallback callback) {
    std::lock_guard<std::mutex> guard(mCallbacksMutex);
    mErrorCallback = callback;
}

void CStrategyEngine::ResetStrategy(const std::string& strategyName) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    try {
        it->second->Reset();
        mStrategyStates[strategyName] = EStrategyState::INACTIVE;

        // Réinitialiser les statistiques
        SStrategyStatistics stats;
        stats.mStrategyName = strategyName;
        stats.mStartTime = std::chrono::system_clock::now();
        mStrategyStats[strategyName] = stats;
    } catch (const std::exception& e) {
        NotifyError(strategyName, "Reset failed: " + std::string(e.what()));
        throw;
    }
}

void CStrategyEngine::ResetAllStrategies() {
    std::vector<std::string> strategyNames = GetAvailableStrategies();

    for (const auto& name : strategyNames) {
        try {
            ResetStrategy(name);
        } catch (const std::exception& e) {
            std::cerr << "Error resetting strategy " << name << ": " << e.what() << std::endl;
        }
    }
}

bool CStrategyEngine::IsStrategyActive(const std::string& strategyName) const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategyStates.find(strategyName);
    if (it == mStrategyStates.end()) {
        return false;
    }

    return it->second == EStrategyState::ACTIVE;
}

size_t CStrategyEngine::GetActiveStrategyCount() const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    size_t count = 0;
    for (const auto& pair : mStrategyStates) {
        if (pair.second == EStrategyState::ACTIVE) {
            count++;
        }
    }

    return count;
}

void CStrategyEngine::SaveStrategyState(const std::string& strategyName, const std::string& filename) const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    try {
        nlohmann::json data = it->second->Serialize();
        std::ofstream file(filename);
        file << std::setw(4) << data << std::endl;
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to save strategy state: " + std::string(e.what()));
    }
}

void CStrategyEngine::LoadStrategyState(const std::string& strategyName, const std::string& filename) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        throw std::runtime_error("Strategy with name '" + strategyName + "' not found");
    }

    try {
        std::ifstream file(filename);
        nlohmann::json data;
        file >> data;
        it->second->Deserialize(data);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to load strategy state: " + std::string(e.what()));
    }
}

nlohmann::json CStrategyEngine::ExportAllStrategies() const {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    nlohmann::json result;

    for (const auto& pair : mStrategies) {
        try {
            result[pair.first] = pair.second->Serialize();
        } catch (const std::exception& e) {
            std::cerr << "Error exporting strategy " << pair.first << ": " << e.what() << std::endl;
        }
    }

    return result;
}

void CStrategyEngine::ImportStrategies(const nlohmann::json& strategiesData) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    for (const auto& item : strategiesData.items()) {
        const std::string& strategyName = item.key();
        const auto& data = item.value();

        auto it = mStrategies.find(strategyName);
        if (it != mStrategies.end()) {
            try {
                it->second->Deserialize(data);
            } catch (const std::exception& e) {
                std::cerr << "Error importing strategy " << strategyName << ": " << e.what() << std::endl;
            }
        }
    }
}

// Méthodes privées

void CStrategyEngine::UpdateStrategyStatistics(const std::string& strategyName, const SPosition& position, double pnl) {
    std::lock_guard<std::mutex> guard(mStrategiesMutex);

    auto it = mStrategyStats.find(strategyName);
    if (it == mStrategyStats.end()) {
        return;
    }

    SStrategyStatistics& stats = it->second;
    stats.mTotalTrades++;
    stats.mTotalPnL += pnl;
    stats.mLastTradeTime = std::chrono::system_clock::now();

    if (pnl > 0) {
        stats.mWinningTrades++;
    } else {
        stats.mLosingTrades++;
    }

    // Calculer le taux de réussite
    if (stats.mTotalTrades > 0) {
        stats.mWinRate = static_cast<double>(stats.mWinningTrades) / stats.mTotalTrades * 100.0;
    }

    // Calculer le drawdown
    if (pnl < 0) {
        stats.mCurrentDrawdown += std::abs(pnl);
        stats.mMaxDrawdown = std::max(stats.mMaxDrawdown, stats.mCurrentDrawdown);
    } else {
        stats.mCurrentDrawdown = std::max(0.0, stats.mCurrentDrawdown - pnl);
    }

    // Mettre à jour les métriques de la stratégie
    auto stratIt = mStrategies.find(strategyName);
    if (stratIt != mStrategies.end()) {
        try {
            stratIt->second->UpdateMetrics(position, pnl);
        } catch (const std::exception& e) {
            std::cerr << "Error updating strategy metrics: " << e.what() << std::endl;
        }
    }
}

void CStrategyEngine::NotifyStrategySignal(const std::string& strategyName, const SSignal& signal) {
    std::lock_guard<std::mutex> guard(mCallbacksMutex);

    if (mStrategyCallback) {
        try {
            mStrategyCallback(strategyName, signal);
        } catch (const std::exception& e) {
            std::cerr << "Error in strategy callback: " << e.what() << std::endl;
        }
    }
}

void CStrategyEngine::NotifyPositionUpdate(const std::string& strategyName, const SPosition& position) {
    std::lock_guard<std::mutex> guard(mCallbacksMutex);

    if (mPositionCallback) {
        try {
            mPositionCallback(strategyName, position);
        } catch (const std::exception& e) {
            std::cerr << "Error in position callback: " << e.what() << std::endl;
        }
    }
}

void CStrategyEngine::NotifyError(const std::string& strategyName, const std::string& error) {
    std::lock_guard<std::mutex> guard(mCallbacksMutex);

    if (mErrorCallback) {
        try {
            mErrorCallback(strategyName, error);
        } catch (const std::exception& e) {
            std::cerr << "Error in error callback: " << e.what() << std::endl;
        }
    }
}

bool CStrategyEngine::ValidateStrategy(const std::string& strategyName) const {
    auto it = mStrategies.find(strategyName);
    if (it == mStrategies.end()) {
        return false;
    }

    auto stateIt = mStrategyStates.find(strategyName);
    if (stateIt == mStrategyStates.end()) {
        return false;
    }

    return stateIt->second == EStrategyState::ACTIVE;
}

void CStrategyEngine::CleanupClosedPositions() {
    std::lock_guard<std::mutex> guard(mPositionsMutex);

    // Cette méthode pourrait être appelée périodiquement pour nettoyer
    // les références vers des positions fermées qui pourraient traîner
    // Pour l'instant, le nettoyage se fait lors de la fermeture des positions
}

std::string CStrategyEngine::GeneratePositionId() const {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::stringstream ss;
    ss << "pos_" << timestamp << "_" << counter.fetch_add(1);
    return ss.str();
}

} // namespace Strategy