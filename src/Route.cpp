#include "../include/Route.h"
#include "../include/Utils.h"
#include "../include/Customer.h"
#include "../include/Station.h"
#include "../include/Depot.h"
#include <numeric>
#include <algorithm>

// --- Constructor and simple getters (mostly unchanged) ---
Route::Route(int id, std::shared_ptr<Vehicle> vehicle, std::shared_ptr<const Instance> instance)
    : id(id), vehicle(std::move(vehicle)), instance(std::move(instance)), cacheValid(false) {}

int Route::getId() const { return id; }
std::shared_ptr<Vehicle> Route::getVehicle() const { return vehicle; }
const std::vector<RouteInfo>& Route::getInfos() const { return infos; }

int Route::getCustomerCount() const {
    int count = 0;
    for (const auto& info : infos) {
        if (dynamic_cast<const Customer*>(info.node.get())) {
            count++;
        }
    }
    return count;
}

// --- Caching (mostly unchanged) ---
void Route::invalidateCache() {
    cacheValid = false;
}

void Route::rebuildCache() const {
    if (cacheValid) return;

    if (infos.size() < 2) {
        cachedTotalDistance = 0.0;
    } else {
        double totalDistance = 0.0;
        for (size_t i = 0; i < infos.size() - 1; ++i) {
            totalDistance += instance->getDistance(infos[i].node->getId(), infos[i+1].node->getId());
        }
        cachedTotalDistance = totalDistance;
    }

    cachedTotalTime = infos.empty() ? 0.0 : infos.back().arrival_time;

    double totalEnergy = 0.0;
    for (const auto& info : infos) {
        if (dynamic_cast<const Station*>(info.node.get())) {
            totalEnergy += (info.departure_battery - info.arrival_battery);
        }
    }
    cachedTotalEnergyCharged = totalEnergy;

    cacheValid = true;
}

// --- Getters using cache (unchanged) ---
double Route::getTotalDistance() const {
    rebuildCache();
    return cachedTotalDistance;
}

double Route::getTotalTime() const {
    rebuildCache();
    return cachedTotalTime;
}

double Route::getTotalEnergyCharged() const {
    rebuildCache();
    return cachedTotalEnergyCharged;
}

// --- NEW CORE LOGIC ---
bool Route::propogateAndUpdate(std::vector<RouteInfo>& route_infos, size_t start_index) {
    if (route_infos.empty() || !instance) return true; // Empty route is feasible

    // If start_index is 0, it means we are building from scratch.
    // We need to initialize the depot.
    if (start_index == 0) {
        double total_demand = 0;
        for(const auto& info : route_infos) {
            if(auto c = std::dynamic_pointer_cast<Customer>(info.node)) {
                total_demand += c->getDemand();
            }
        }
        auto& depot_info = route_infos[0];
        depot_info.arrival_time = 0;
        depot_info.departure_time = 0;
        depot_info.arrival_load = total_demand;
        depot_info.departure_load = total_demand;
        depot_info.arrival_battery = instance->getVehicleBattery();
        depot_info.departure_battery = instance->getVehicleBattery();
        start_index = 1; // Start propagation from the next node
    }

    // Propagate changes from start_index
    for (size_t i = start_index; i < route_infos.size(); ++i) {
        RouteInfo& prev_info = route_infos[i-1];
        RouteInfo& current_info = route_infos[i];
        
        double travel_time = instance->getTime(prev_info.node->getId(), current_info.node->getId());
        double travel_dist = instance->getDistance(prev_info.node->getId(), current_info.node->getId());
        double energy_consumed = travel_dist * instance->getVehicleEnergyRate();

        current_info.arrival_time = prev_info.departure_time + travel_time;
        current_info.arrival_battery = prev_info.departure_battery - energy_consumed;
        current_info.arrival_load = prev_info.departure_load;

        // CONSTRAINT CHECK: Battery
        if (current_info.arrival_battery < -1e-6) { // Use tolerance
            return false;
        }

        // Get node properties
        double ready_time = 0, due_date = std::numeric_limits<double>::max(), service_time = 0;
        if (auto customer = std::dynamic_pointer_cast<Customer>(current_info.node)) {
            ready_time = customer->getReadyTime();
            due_date = customer->getDueDate();
            service_time = customer->getServiceTime();
        } else if (auto station = std::dynamic_pointer_cast<Station>(current_info.node)) {
            ready_time = station->getReadyTime();
            due_date = station->getDueDate();
        } else if (auto depot = std::dynamic_pointer_cast<Depot>(current_info.node)) {
            ready_time = depot->getReadyTime();
            due_date = depot->getLastTime();
        }

        // CONSTRAINT CHECK: Time Window
        if (current_info.arrival_time > due_date + 1e-6) { // Use tolerance
            return false;
        }

        double wait_time = std::max(0.0, ready_time - current_info.arrival_time);
        
        // Handle logic at the node
        if (auto customer = std::dynamic_pointer_cast<Customer>(current_info.node)) {
            current_info.departure_time = current_info.arrival_time + wait_time + service_time;
            current_info.departure_load = current_info.arrival_load - customer->getDemand();
            current_info.departure_battery = current_info.arrival_battery;
        } else if (auto station = std::dynamic_pointer_cast<Station>(current_info.node)) {
            // CORRECT CHARGING LOGIC
            double battery_to_charge = instance->getVehicleBattery() - current_info.arrival_battery;
            double charging_time = (station->getChargingRate() > 0) ? (battery_to_charge / station->getChargingRate()) : 0;
            
            current_info.departure_time = current_info.arrival_time + wait_time + charging_time;
            current_info.departure_battery = instance->getVehicleBattery(); // Full charge
            current_info.departure_load = current_info.arrival_load;
        } else { // Depot
            current_info.departure_time = current_info.arrival_time + wait_time;
            current_info.departure_battery = current_info.arrival_battery;
            current_info.departure_load = current_info.arrival_load;
        }
    }
    return true; // Feasible
}

// --- REFACTORED Methods ---

void Route::recalculateFrom(size_t start_index) {
    // This function now correctly uses the start_index
    // and relies on the unified propagation logic.
    if (propogateAndUpdate(this->infos, start_index)) {
        invalidateCache();
    } else {
        // This case should ideally not happen if insertions are checked correctly.
        // It indicates an invalid state has been reached.
        // For now, we just invalidate the cache. A more robust solution
        // might throw an exception or log an error.
        invalidateCache();
    }
}

void Route::insert(std::shared_ptr<Node> node, size_t position) {
    if (position > infos.size()) {
        position = infos.size();
    }
    RouteInfo newInfo;
    newInfo.node = node;

    infos.insert(infos.begin() + position, newInfo);
    // OPTIMIZED: Recalculate only from the point of change.
    recalculateFrom(position);
}

void Route::remove(size_t position) {
    // Cannot remove depot
    if (position > 0 && position < infos.size()) {
        infos.erase(infos.begin() + position);
        // OPTIMIZED: Recalculate from the point of change.
        // The node at 'position' is now a new node, so its state needs updating.
        recalculateFrom(position);
    }
}

EvaluationResult Route::evaluateInsertion(std::shared_ptr<Node> node, size_t position) {
    EvaluationResult result;

    // --- Constraint 1: Capacity (quick check) ---
    if (auto customer_to_insert = std::dynamic_pointer_cast<Customer>(node)) {
        double current_total_demand = 0;
        for(const auto& info : infos) {
            if(auto c = std::dynamic_pointer_cast<Customer>(info.node)) {
                current_total_demand += c->getDemand();
            }
        }
        if (current_total_demand + customer_to_insert->getDemand() > vehicle->getCapacity() + 1e-6) {
            return result; // isFeasible = false
        }
    }

    // --- Constraint 2 & 3: Time and Energy (full check) ---
    double currentCost = this->getTotalTime(); // Using total time as cost

    // 1. Create a temporary copy to simulate the insertion
    std::vector<RouteInfo> temp_infos = this->infos;
    
    // 2. Insert the new node
    RouteInfo newInfo;
    newInfo.node = node;
    temp_infos.insert(temp_infos.begin() + position, newInfo);

    // 3. Propagate updates and check feasibility
    if (propogateAndUpdate(temp_infos, position)) {
        // 4. If feasible, calculate the cost delta
        result.isFeasible = true;
        double newCost = temp_infos.back().arrival_time;
        result.costDelta = newCost - currentCost;
    }
    // 5. If not feasible, result.isFeasible remains false

    return result;
}

// --- Other methods (mostly unchanged) ---

bool Route::removeCustomer(std::shared_ptr<Customer> customer) {
    for (size_t i = 0; i < infos.size(); ++i) {
        if (infos[i].node == customer) {
            remove(i);
            return true;
        }
    }
    return false;
}

double Route::getCurrentLoad() const {
    if (infos.empty()) return 0;
    return infos.back().departure_load;
}

double Route::getCurrentBattery() const {
    if (infos.empty()) return 0;
    return infos.back().departure_battery;
}
