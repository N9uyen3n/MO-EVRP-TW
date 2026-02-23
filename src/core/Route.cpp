#include "../../include/core/Route.h"
#include "../../include/core/Instance.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Station.h"
#include "../../include/core/Depot.h"
#include <stdexcept>
#include <algorithm> // Cho std::max
#include <cmath>     // Cho std::abs (kiểm tra số thực)
#include <iostream>
#include <ostream>
#include <sstream>   // <-- Cần include thư viện này
#include <iomanip>

Route::Route(int id, std::shared_ptr<Vehicle> vehicle, const std::shared_ptr<Instance>& instance)
    : id(id), vehicle(vehicle), instance(instance), evalResult() {

    nodeSequence = std::make_shared<std::vector<int>>();
    nodeSequence->push_back(0);
    nodeSequence->push_back(0);

    states = std::make_shared<std::vector<NodeState>>();
    minBatteryReq = std::make_shared<std::vector<double>>();

    evaluate(); 
}

void Route::detach() const{
    if (nodeSequence.use_count() > 1) {
        nodeSequence = std::make_shared<std::vector<int>>(*nodeSequence);
    }
    // States are always detached in evaluate() before modification
}

void Route::detachStates() const{
    if (states.use_count() > 1) {
        states = std::make_shared<std::vector<NodeState>>(*states);
    }
}


void Route::addNode(int nodeId, size_t position) {
    if (position < 1 || position > nodeSequence->size() - 1) {
        throw std::out_of_range("Vi tri them node khong hop le. Phai nam giua 2 depot.");
    }
    detach();
    nodeSequence->insert(nodeSequence->begin() + position, nodeId);
    isDirty = true;
}

void Route::addNode(int nodeId) {
    detach();
    nodeSequence->insert(nodeSequence->end() - 1, nodeId);
    isDirty = true;
}

void Route::removeNode(size_t position) {
    if (position < 1 || position > nodeSequence->size() - 2) {
        throw std::out_of_range("Vi tri xoa node khong hợp lệ. Khong the xoa depot.");
    }
    detach();
    nodeSequence->erase(nodeSequence->begin() + position);
    isDirty = true;
}

void Route::clear() {
    detach();
    nodeSequence->clear();
    nodeSequence->push_back(0);
    nodeSequence->push_back(0);
    isDirty = true;
    evaluate(); 
}

void Route::reverseNodes(size_t i, size_t j) {
    if (i < 1 || j >= nodeSequence->size() - 1 || i >= j) {
        throw std::out_of_range("Chi so 2-Opt không hợp lệ.");
    }
    detach();
    std::reverse(nodeSequence->begin() + i, nodeSequence->begin() + j + 1);
    isDirty = true;
}

void Route::evaluate() const {
    if (!isDirty) {
        return;
    }
    detachStates(); // COW for states vector

    int n = nodeSequence->size();
    if (n <= 1) {
        evalResult = EvaluationResult();
        isDirty = false;
        return;
    }

    std::vector<double> dp(n, 0.0);
    const double EPSILON = 1e-9;

    dp[n - 1] = 0.0;

    for (int i = n - 2; i >= 0; --i) {
        double energy_to_next = instance->getDistance((*nodeSequence)[i], (*nodeSequence)[i+1])
                              * vehicle->getEnergyConsumptionRate();
        auto next_node = instance->getNodeById((*nodeSequence)[i+1]);
        if (next_node->getType() == NodeType::STATION) {
            dp[i] = energy_to_next + std::max(0.0, dp[i+1] - vehicle->getBatteryCapacity());
        } else {
            dp[i] = energy_to_next + dp[i+1];
        }
        if (dp[i] > vehicle->getBatteryCapacity() + EPSILON) {
            evalResult = EvaluationResult();
            evalResult.feasible = false;
            isDirty = false;
            return;
        }
    }

    // Store Backward Pass result for station optimization operators
    if (minBatteryReq.use_count() > 1) {
        minBatteryReq = std::make_shared<std::vector<double>>(dp);
    } else {
        *minBatteryReq = dp;
    }

    states->clear();
    states->resize(n);
    evalResult = EvaluationResult();

    auto start_depot = std::static_pointer_cast<Depot>(instance->getNodeById(0));
    (*states)[0].arrivalTime = start_depot->getReadyTime();
    (*states)[0].departureTime = start_depot->getReadyTime();
    (*states)[0].remainingBattery = vehicle->getBatteryCapacity();
    (*states)[0].remainingLoad = vehicle->getCapacity();
    (*states)[0].chargeAmount = 0.0;

    for (int i = 0; i < n - 1; ++i) {
        int from_id = (*nodeSequence)[i];
        int to_id = (*nodeSequence)[i + 1];
        auto to_node = instance->getNodeById(to_id);

        NodeState& from_state = (*states)[i];
        NodeState& to_state = (*states)[i + 1];

        double distance = instance->getDistance(from_id, to_id);
        double travel_time = instance->getTime(from_id, to_id);
        double energy_consumed = distance * vehicle->getEnergyConsumptionRate();

        evalResult.totalDistance += distance;
        evalResult.totalEnergyConsumption += energy_consumed;

        to_state.arrivalTime = from_state.departureTime + travel_time;
        to_state.remainingBattery = from_state.remainingBattery - energy_consumed;
        to_state.remainingLoad = from_state.remainingLoad;
        to_state.chargeAmount = 0.0;

        if (to_state.remainingBattery < -EPSILON) {
            evalResult.feasible = false;
            isDirty = false;
            return;
        }

        switch(to_node->getType()) {
            case NodeType::CUSTOMER: {
                auto customer = std::static_pointer_cast<Customer>(to_node);
                to_state.remainingLoad -= customer->getDemand();
                if (to_state.remainingLoad < -EPSILON) {
                    evalResult.feasible = false;
                    isDirty = false;
                    return;
                }
                double wait_time = std::max(0.0, customer->getReadyTime() - to_state.arrivalTime);
                evalResult.totalWaitTime += wait_time;
                double service_start_time = to_state.arrivalTime + wait_time;
                if (service_start_time > customer->getDueDate() + EPSILON) {
                    evalResult.feasible = false;
                    isDirty = false;
                    return;
                }
                (*states)[i+1].departureTime = service_start_time + customer->getServiceTime();
                break;
            }
            case NodeType::STATION: {
                auto station = std::static_pointer_cast<Station>(to_node);
                double wait_time = std::max(0.0, station->getReadyTime() - to_state.arrivalTime);
                evalResult.totalWaitTime += wait_time;
                double charge_start_time = to_state.arrivalTime + wait_time;
                if (charge_start_time > station->getDueDate() + EPSILON) {
                    evalResult.feasible = false;
                    isDirty = false;
                    return;
                }
                double charge_needed = std::max(0.0, dp[i+1] - to_state.remainingBattery);
                double charge_possible = vehicle->getBatteryCapacity() - to_state.remainingBattery;
                double charge_amount = std::min(charge_needed, charge_possible);
                double charge_time = charge_amount * station->getChargingRate();
                (*states)[i+1].chargeAmount = charge_amount;
                evalResult.totalChargeTime += charge_time;
                evalResult.totalChargeAmount += charge_amount;
                (*states)[i+1].departureTime = charge_start_time + charge_time;
                (*states)[i+1].remainingBattery += charge_amount;
                if ((*states)[i+1].departureTime > station->getDueDate() + EPSILON) {
                    evalResult.feasible = false;
                    isDirty = false;
                    return;
                }
                break;
            }
            case NodeType::DEPOT: {
                auto depot = std::static_pointer_cast<Depot>(to_node);
                if (to_state.arrivalTime > depot->getDueDate() + EPSILON) {
                    evalResult.feasible = false;
                    isDirty = false;
                    return;
                }
                (*states)[i+1].departureTime = to_state.arrivalTime;
                depot->setLastTime(to_state.arrivalTime);
                break;
            }
        }
    }

    for (int i = 1; i < n; ++i) {
        auto node_prev = instance->getNodeById((*nodeSequence)[i-1]);
        if (node_prev->getType() == NodeType::STATION) {
            if ((*nodeSequence)[i] == (*nodeSequence)[i-1]) {
                evalResult.feasible = false;
                isDirty = false;
                return;
            }
        }
    }

    evalResult.feasible = true;
    evalResult.totalTime = states->back().arrivalTime - states->front().departureTime;
    isDirty = false;
}

// Tier 1 - Exact Check
InsertionResult Route::checkInsertionCost(int nodeId, size_t position) const {
    std::vector<int> simNodeSequence = *nodeSequence;
    simNodeSequence.insert(simNodeSequence.begin() + position, nodeId);
    int n = simNodeSequence.size();
    const double EPSILON = 1e-9;
    std::vector<double> dp(n, 0.0);
    dp[n - 1] = 0.0;
    for (int i = n - 2; i >= 0; --i) {
        double energy_to_next = instance->getDistance(simNodeSequence[i], simNodeSequence[i+1]) * vehicle->getEnergyConsumptionRate();
        auto next_node = instance->getNodeById(simNodeSequence[i+1]);
        if (next_node->getType() == NodeType::STATION) {
            dp[i] = energy_to_next + std::max(0.0, dp[i+1] - vehicle->getBatteryCapacity());
        } else {
            dp[i] = energy_to_next + dp[i+1];
        }
        if (dp[i] > vehicle->getBatteryCapacity() + EPSILON) {
            return { false };
        }
    }
    std::vector<NodeState> simStates(n);
    double simTotalWaitTime = 0.0;
    double simTotalChargeAmount = 0.0;
    double simTotalEnergyConsumption = 0.0;
    double simTotalDistance = 0.0;
    auto start_depot = std::static_pointer_cast<Depot>(instance->getNodeById(0));
    simStates[0].arrivalTime = start_depot->getReadyTime();
    simStates[0].departureTime = start_depot->getReadyTime();
    simStates[0].remainingBattery = vehicle->getBatteryCapacity();
    simStates[0].remainingLoad = vehicle->getCapacity();
    for (int i = 0; i < n - 1; ++i) {
        int from_id = simNodeSequence[i];
        int to_id = simNodeSequence[i + 1];
        auto to_node = instance->getNodeById(to_id);
        NodeState& from_state = simStates[i];
        NodeState& to_state = simStates[i + 1];
        double distance = instance->getDistance(from_id, to_id);
        double travel_time = instance->getTime(from_id, to_id);
        double energy_consumed = distance * vehicle->getEnergyConsumptionRate();
        simTotalDistance += distance;
        simTotalEnergyConsumption += energy_consumed;
        to_state.arrivalTime = from_state.departureTime + travel_time;
        to_state.remainingBattery = from_state.remainingBattery - energy_consumed;
        to_state.remainingLoad = from_state.remainingLoad;
        to_state.chargeAmount = 0.0;
        if (to_state.remainingBattery < -EPSILON) return { false };
        switch(to_node->getType()) {
            case NodeType::CUSTOMER: {
                auto cust = std::static_pointer_cast<Customer>(to_node);
                to_state.remainingLoad -= cust->getDemand();
                if (to_state.remainingLoad < -EPSILON) return { false };
                double wait_time = std::max(0.0, cust->getReadyTime() - to_state.arrivalTime);
                simTotalWaitTime += wait_time;
                double service_start_time = to_state.arrivalTime + wait_time;
                if (service_start_time > cust->getDueDate() + EPSILON) return { false };
                to_state.departureTime = service_start_time + cust->getServiceTime();
                break;
            }
            case NodeType::STATION: {
                auto station = std::static_pointer_cast<Station>(to_node);
                double wait_time = std::max(0.0, station->getReadyTime() - to_state.arrivalTime);
                simTotalWaitTime += wait_time;
                double charge_start_time = to_state.arrivalTime + wait_time;
                if (charge_start_time > station->getDueDate() + EPSILON) return { false };
                double charge_needed = std::max(0.0, dp[i+1] - to_state.remainingBattery);
                double charge_possible = vehicle->getBatteryCapacity() - to_state.remainingBattery;
                double charge_amount = std::min(charge_needed, charge_possible);
                double charge_time = charge_amount * station->getChargingRate();
                to_state.chargeAmount = charge_amount;
                simTotalChargeAmount += charge_amount;
                to_state.departureTime = charge_start_time + charge_time;
                to_state.remainingBattery += charge_amount;
                if (to_state.departureTime > station->getDueDate() + EPSILON) return { false };
                break;
            }
            case NodeType::DEPOT: {
                auto depot = std::static_pointer_cast<Depot>(to_node);
                if (to_state.arrivalTime > depot->getDueDate() + EPSILON) return { false };
                to_state.departureTime = to_state.arrivalTime;
                depot->setLastTime(to_state.arrivalTime);
                break;
            }
        }
    }
    InsertionResult res;
    res.isFeasible = true;
    evaluate(); 
    res.deltaDistance = simTotalDistance - evalResult.totalDistance;
    res.deltaChargeAmount = simTotalChargeAmount - evalResult.totalChargeAmount;
    res.deltaWaitTime = simTotalWaitTime - evalResult.totalWaitTime;
    res.deltaEnergyConsumption = simTotalEnergyConsumption - evalResult.totalEnergyConsumption;
    return res;
}

// --- GETTERS ---
int Route::getId() const{
    return id;
}

std::shared_ptr<Vehicle> Route::getVehicle() const {
    return vehicle;
}

const std::vector<int>& Route::getNodes() const{
    return *nodeSequence;
}

std::vector<int> Route::getCustomers() const {
    std::vector<int> customers;
    for (int nodeId : *nodeSequence) {
        auto node = instance->getNodeById(nodeId);
        if (node->getType() == NodeType::CUSTOMER) {
            customers.push_back(nodeId);
        }
    }
    return customers;
}

bool Route::isFeasible() const{
    evaluate();
    return evalResult.feasible;
}

double Route::getTotalDistance() const{
    evaluate();
    return evalResult.totalDistance;
}

double Route::getTotalWaitTime() const{
    evaluate();
    return evalResult.totalWaitTime;
}

double Route::getTotalChargeTime() const{
    evaluate();
    return evalResult.totalChargeTime;
}

double Route::getTotalChargeAmount() const{
    evaluate();
    return evalResult.totalChargeAmount;
}

double Route::getTotalEnergyConsumption() const {
    evaluate();
    return evalResult.totalEnergyConsumption;
}

double Route::getTotalTime() const{
    evaluate();
    return evalResult.totalTime;
}

double Route::getActiveTime() const{
    evaluate();
    return evalResult.totalTime - evalResult.totalWaitTime;
}

double Route::getTotalDemand() const {
    double totalDemand = 0.0;
    // No need to call evaluate() here, as nodeSequence is independent of evaluation results
    for (int nodeId : *nodeSequence) {
        if (instance->getNodeById(nodeId)->getType() == NodeType::CUSTOMER) {
            totalDemand += instance->getNodeById(nodeId)->getDemand();
        }
    }
    return totalDemand;
}

const std::vector<NodeState>& Route::getStates() const {
    evaluate();
    return *states;
}

int Route::getNodeAt(size_t pos) const {
    if (pos >= nodeSequence->size()) return -1;
    return (*nodeSequence)[pos];
}

int Route::getLastNodeId() const {
    if (nodeSequence->size() <= 2) {
        // Route is empty or has only depots, return the starting depot
        return 0;
    }
    // Return the node at the second to last position (before the final depot)
    return (*nodeSequence)[nodeSequence->size() - 2];
}

size_t Route::size() const {
    return nodeSequence->size();
}

void Route::print() const {
    evaluate(); 
    std::cout << "Route Id:" << id << " - Total Distance: " << evalResult.totalDistance <<
        " - Total Time: " << evalResult.totalTime <<
        " - Total Charge Amount: " << evalResult.totalChargeAmount << std::endl;
    for (size_t i = 0; i < nodeSequence->size() - 1; ++i) {
        std::cout << (*nodeSequence)[i] << "->";
    }
    std::cout << (*nodeSequence)[nodeSequence->size() - 1] << std::endl;
}


std::string Route::toString() const {
    evaluate(); 
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "--- Route ID: " << this->id << " ---" << " Feasible: " << (this->evalResult.feasible ? "YES" : "NO") << "\n"
       << "   Total Distance:    " << std::setw(8) << this->evalResult.totalDistance << "\n"
       << "   Total Time:        " << std::setw(8) << this->evalResult.totalTime << "\n"
       << "   Total Energy Cons: " << std::setw(8) << this->evalResult.totalEnergyConsumption << "\n";

    if (nodeSequence->empty()) {
        ss << "   [Route is Empty]\n";
        return ss.str();
    }
    if (nodeSequence->size() != states->size()) {
        ss << "   [ERROR: Node sequence and states vectors have different sizes!]\n"
           << "   NodeSequence.size() = " << nodeSequence->size() << "\n"
           << "   States.size() = " << states->size() << "\n";
        return ss.str();
    }

    ss << "--- Node Sequence (Count: " << nodeSequence->size() << ") ---\n";
    ss << std::setw(5) << "Idx" << " | "
       << std::setw(5) << "StrID" << " | "
       << std::setw(10) << "Type" << " | "
       << std::setw(8) << "ArrTime" << " | "
       << std::setw(8) << "DepTime" << " | "
       << std::setw(8) << "RemBat" << " | "
       << std::setw(8) << "RemLoad" << " | "
       << std::setw(8) << "Charge" << "\n";
    ss << "-----------------------------------------------------------------------------------\n";

    for (size_t i = 0; i < nodeSequence->size(); ++i) {
        int nodeId = (*nodeSequence)[i];
        const auto& state = (*states)[i];
        const std::shared_ptr<Node>& node = this->instance->getNodeById(nodeId);

        std::string nodeTypeStr = "???";
        switch(node->getType()) {
            case NodeType::CUSTOMER: nodeTypeStr = "Customer"; break;
            case NodeType::STATION:  nodeTypeStr = "Station"; break;
            case NodeType::DEPOT:    nodeTypeStr = "Depot"; break;
        }

        ss << std::setw(5) << node->getId() << " | "
           << std::setw(5) << node->getStringId() << " | "
           << std::setw(10) << nodeTypeStr << " | "
           << std::setw(8) << state.arrivalTime << " | "
           << std::setw(8) << state.departureTime << " | "
           << std::setw(8) << state.remainingBattery << " | "
           << std::setw(8) << state.remainingLoad << " | "
           << std::setw(8) << state.chargeAmount << "\n";
    }
    ss << "\n";

    return ss.str();
}

long long Route::getHash() const {
    evaluate(); 
    long long hash = 0;
    for (int nodeId : *nodeSequence) {
        hash = hash * 31 + nodeId;
    }
    return hash;
}

// Tier 3 - Bounding Check
bool Route::canPossiblyInsert(int nodeIdToInsert, size_t position, int nodeIdToRemove) const {
    evaluate();

    if (position < 1 || position > nodeSequence->size() - 1) {
        return false;
    }
    if (!evalResult.feasible) {
        return false;
    }

    const auto& prev_state = states->at(position - 1);
    int prev_node_id = (*nodeSequence)[position - 1];
    auto nodeToInsert = instance->getNodeById(nodeIdToInsert);
    const double EPSILON = 1e-9;

    double availableLoad = prev_state.remainingLoad;
    if (nodeIdToRemove != -1) {
        auto nodeToRemove = instance->getNodeById(nodeIdToRemove);
        if (nodeToRemove->getType() == NodeType::CUSTOMER) {
            availableLoad += nodeToRemove->getDemand();
        }
    }

    if (nodeToInsert->getDemand() > availableLoad + EPSILON) {
        return false;
    }

    double energy_to_node = instance->getDistance(prev_node_id, nodeIdToInsert) * vehicle->getEnergyConsumptionRate();
    if (prev_state.remainingBattery < energy_to_node - EPSILON) {
        return false;
    }

    double travel_time_to_node = instance->getTime(prev_node_id, nodeIdToInsert);
    double arrival_at_node = prev_state.departureTime + travel_time_to_node;
    if (arrival_at_node > nodeToInsert->getDueDate() + EPSILON) {
        return false;
    }

    return true;
}


// Tier 2 - Fast, Approximate Check
InsertionResult Route::fastForwardCheck(int nodeId, size_t position) const {
    evaluate(); 

    const double EPSILON = 1e-9;

    int prevNodeId = (*nodeSequence)[position - 1];
    int nextNodeId = (*nodeSequence)[position];
    auto customerNode = std::static_pointer_cast<Customer>(instance->getNodeById(nodeId));

    double oldEdgeDistance = instance->getDistance(prevNodeId, nextNodeId);
    double newEdgeDistance = instance->getDistance(prevNodeId, nodeId) + instance->getDistance(nodeId, nextNodeId);
    double deltaDistance = newEdgeDistance - oldEdgeDistance;

    NodeState currentState = states->at(position - 1);
    double currentLoad = currentState.remainingLoad;
    double simTotalWaitTime = 0;

    // prev -> customer
    currentState.departureTime += instance->getTime(prevNodeId, nodeId);
    currentState.remainingBattery -= instance->getDistance(prevNodeId, nodeId) * vehicle->getEnergyConsumptionRate();
    currentLoad -= customerNode->getDemand();

    if (currentState.remainingBattery < -EPSILON || currentLoad < -EPSILON || currentState.departureTime > customerNode->getDueDate() + EPSILON) {
        return {false};
    }
    double waitAtCust = std::max(0.0, customerNode->getReadyTime() - currentState.departureTime);
    simTotalWaitTime += waitAtCust;
    currentState.departureTime = currentState.departureTime + waitAtCust + customerNode->getServiceTime();

    // customer -> next
    currentState.departureTime += instance->getTime(nodeId, nextNodeId);
    currentState.remainingBattery -= instance->getDistance(nodeId, nextNodeId) * vehicle->getEnergyConsumptionRate();
    if (currentState.remainingBattery < -EPSILON) return {false};

    // Ripple simulation
    for (size_t i = position; i < nodeSequence->size() - 1; ++i) {
        int current_node_id = (*nodeSequence)[i];
        int next_node_id = (*nodeSequence)[i + 1];
        auto current_node_obj = instance->getNodeById(current_node_id);

        if (currentState.departureTime > current_node_obj->getDueDate() + EPSILON) return {false};
        
        double wait_time = std::max(0.0, current_node_obj->getReadyTime() - currentState.departureTime);
        simTotalWaitTime += wait_time;
        currentState.departureTime += wait_time;

        if (current_node_obj->getType() == NodeType::STATION) {
            double energyToDepot = instance->getDistance(current_node_id, 0) * vehicle->getEnergyConsumptionRate();
            double requiredBattery = energyToDepot * 1.1; 
            
            if (currentState.remainingBattery < requiredBattery) {
                double chargeAmount = requiredBattery - currentState.remainingBattery;
                chargeAmount = std::min(chargeAmount, vehicle->getBatteryCapacity() - currentState.remainingBattery);
                
                auto station = std::static_pointer_cast<Station>(current_node_obj);
                double chargeTime = chargeAmount * station->getChargingRate();
                currentState.departureTime += chargeTime;
                currentState.remainingBattery += chargeAmount;
            }
        }
        
        currentState.departureTime += current_node_obj->getServiceTime();
        
        currentState.departureTime += instance->getTime(current_node_id, next_node_id);
        currentState.remainingBattery -= instance->getDistance(current_node_id, next_node_id) * vehicle->getEnergyConsumptionRate();
        if (currentState.remainingBattery < -EPSILON) return {false};
    }

    auto final_depot = instance->getNodeById(nodeSequence->back());
    if (currentState.departureTime > final_depot->getDueDate() + EPSILON) return {false};

    InsertionResult res;
    res.isFeasible = true;
    res.deltaDistance = deltaDistance;
    
    // This is an approximation
    double oldWaitTime = 0;
    for(size_t i = position -1; i < states->size(); ++i) {
        oldWaitTime += (*states)[i].timeWait;
    }
    res.deltaWaitTime = simTotalWaitTime - oldWaitTime;

    return res;
}

// O(1) pre-filter
bool Route::quickCapacityCheck(double demand) const {
    evaluate(); 
    if (states->empty()) {
        return true; 
    }
    // This is a heuristic. It checks against the final remaining load.
    return demand <= states->back().remainingLoad;
}

const std::vector<double>& Route::getMinBatteryReq() const {
    evaluate();
    return *minBatteryReq;
}