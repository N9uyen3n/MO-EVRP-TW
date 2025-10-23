#include "Route.h"
#include "Utils.h"
#include "Customer.h"
#include "Station.h"
#include "Depot.h"
#include <numeric>      // For std::accumulate
#include <algorithm>    // For std::max

Route::Route(int id, std::shared_ptr<Vehicle> vehicle, std::shared_ptr<const Instance> instance)
    : id(id), vehicle(std::move(vehicle)), instance(std::move(instance)), cacheValid(false) {}

int Route::getId() const {
    return id;
}

std::shared_ptr<Vehicle> Route::getVehicle() const {
    return vehicle;
}

const std::vector<RouteInfo>& Route::getInfos() const {
    return infos;
}

int Route::getCustomerCount() const {
    int count = 0;
    for (const auto& info : infos) {
        if (dynamic_cast<const Customer*>(info.node.get())) {
            count++;
        }
    }
    return count;
}

// --- Caching Implementation ---

void Route::invalidateCache() {
    cacheValid = false;
}

void Route::rebuildCache() const {
    if (cacheValid) return;

    if (infos.size() < 2) {
        cachedTotalDistance = 0.0;
    } else {
        if (instance) {
            double totalDistance = 0.0;
            for (size_t i = 0; i < infos.size() - 1; ++i) {
                totalDistance += instance->getDistance(infos[i].node->getId(), infos[i+1].node->getId());
            }
            cachedTotalDistance = totalDistance;
        }
    }

    if (infos.empty()) {
        cachedTotalTime = 0.0;
    } else {
        cachedTotalTime = infos.back().arrival_time;
    }

    double totalEnergy = 0.0;
    for (const auto& info : infos) {
        if (dynamic_cast<const Station*>(info.node.get())) {
            totalEnergy += (info.departure_battery - info.arrival_battery);
        }
    }
    cachedTotalEnergyCharged = totalEnergy;

    cacheValid = true;
}

// --- Getters that use the cache ---

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

// --- Modification methods ---

bool Route::canInsert(std::shared_ptr<Node> node, size_t position) {
    if (!instance || position <= 0 || position >= infos.size()) {
        return false;
    }

    auto customer_to_insert = std::dynamic_pointer_cast<Customer>(node);
    if (!customer_to_insert) {
        // For now, we only handle inserting customers. Stations could be handled differently.
        return false;
    }

    const RouteInfo& prev_info = infos[position - 1];
    const RouteInfo& next_info_original = infos[position];

    // --- Check 1: Capacity --- 
    // The new total load must not exceed vehicle capacity.
    // This check is implicitly handled by recalculateFrom logic, but a preliminary check is good.
    double current_total_demand = 0;
    for(const auto& info : infos) {
        if(auto c = std::dynamic_pointer_cast<Customer>(info.node)) {
            current_total_demand += c->getDemand();
        }
    }
    if (current_total_demand + customer_to_insert->getDemand() > vehicle->getCapacity()) {
        return false;
    }

    // --- Check 2 & 3: Time Windows and Battery --- 
    // Simulate the state at the new node and the node after it.

    // State at the point of insertion
    double travel_time_to_new = instance->getTime(prev_info.node->getId(), node->getId());
    double energy_to_new = instance->getDistance(prev_info.node->getId(), node->getId()) * instance->getVehicleEnergyRate();

    double arrival_time_at_new = prev_info.departure_time + travel_time_to_new;
    double arrival_battery_at_new = prev_info.departure_battery - energy_to_new;

    if (arrival_battery_at_new < 0) return false; // Can't reach the new node

    double wait_time_at_new = std::max(0.0, customer_to_insert->getReadyTime() - arrival_time_at_new);
    if (arrival_time_at_new + wait_time_at_new > customer_to_insert->getDueDate()) return false; // Violates new node's due date

    double departure_time_from_new = arrival_time_at_new + wait_time_at_new + customer_to_insert->getServiceTime();

    // State at the next node in the original route
    double travel_time_to_next = instance->getTime(node->getId(), next_info_original.node->getId());
    double energy_to_next = instance->getDistance(node->getId(), next_info_original.node->getId()) * instance->getVehicleEnergyRate();

    double arrival_time_at_next = departure_time_from_new + travel_time_to_next;
    double arrival_battery_at_next = arrival_battery_at_new - energy_to_next; // Battery doesn't change at customer

    if (arrival_battery_at_next < 0) return false; // Can't reach the next node

    if (auto next_customer = std::dynamic_pointer_cast<const Customer>(next_info_original.node)) {
        if (arrival_time_at_next > next_customer->getDueDate()) return false; // Insertion makes next node late
    }
    
    // A full check would require propagating the time changes down the whole route.
    // This simplified check is a good heuristic.
    return true;
}

void Route::insert(std::shared_ptr<Node> node, size_t position) {
    if (position > infos.size()) {
        position = infos.size();
    }
    RouteInfo newInfo;
    newInfo.node = node;

    infos.insert(infos.begin() + position, newInfo);
    recalculateFrom(0); // Recalculate the whole route from the depot
}

void Route::remove(size_t position) {
    if (position > 0 && position < infos.size()) { // Cannot remove depots
        infos.erase(infos.begin() + position);
        recalculateFrom(0); // Recalculate the whole route from the depot
    }
}

void Route::recalculateFrom(size_t position) {
    if (infos.empty() || !instance) return;

    // --- Delivery Logic --- 
    // 1. First, calculate the total demand for this specific route.
    double total_demand = 0;
    for(const auto& info : infos) {
        if(auto c = std::dynamic_pointer_cast<Customer>(info.node)) {
            total_demand += c->getDemand();
        }
    }

    // 2. Initialize the starting depot (always at index 0)
    auto& depot_info = infos[0];
    depot_info.arrival_time = 0;
    depot_info.departure_time = 0;
    depot_info.arrival_load = total_demand; // Arrives with the goods for the route
    depot_info.departure_load = total_demand; // Leaves with the goods
    depot_info.arrival_battery = instance->getVehicleBattery();
    depot_info.departure_battery = instance->getVehicleBattery();

    // 3. Recalculate all subsequent nodes
    for (size_t i = 1; i < infos.size(); ++i) {
        RouteInfo& prev_info = infos[i-1];
        RouteInfo& current_info = infos[i];

        double travel_time = instance->getTime(prev_info.node->getId(), current_info.node->getId());
        double travel_dist = instance->getDistance(prev_info.node->getId(), current_info.node->getId());
        double energy_consumed = travel_dist * instance->getVehicleEnergyRate();

        current_info.arrival_time = prev_info.departure_time + travel_time;
        current_info.arrival_battery = prev_info.departure_battery - energy_consumed;
        current_info.arrival_load = prev_info.departure_load;

        if (auto customer = std::dynamic_pointer_cast<Customer>(current_info.node)) {
            double wait_time = std::max(0.0, customer->getReadyTime() - current_info.arrival_time);
            current_info.departure_time = current_info.arrival_time + wait_time + customer->getServiceTime();
            current_info.departure_load = current_info.arrival_load - customer->getDemand(); // Delivery: load decreases
            current_info.departure_battery = current_info.arrival_battery;
        } else if (auto station = std::dynamic_pointer_cast<Station>(current_info.node)) {
            // Simple strategy: charge to full. This can be improved.
            double battery_to_charge = instance->getVehicleBattery() - current_info.arrival_battery;
            double charging_time = (station->getChargingRate() > 0) ? (battery_to_charge / station->getChargingRate()) : 0;
            
            current_info.departure_time = current_info.arrival_time + charging_time;
            current_info.departure_battery = instance->getVehicleBattery();
            current_info.departure_load = current_info.arrival_load;
        } else if (auto depot = std::dynamic_pointer_cast<Depot>(current_info.node)) {
            // This is the ending depot
            current_info.departure_time = current_info.arrival_time;
            current_info.departure_battery = current_info.arrival_battery;
            current_info.departure_load = current_info.arrival_load;
        }
    }

    invalidateCache();
}

// --- Methods for ALNS operators ---
double Route::getInsertionCost(std::shared_ptr<Node> node, int position) const {
    if (!instance || infos.size() < 2 || position <= 0 || position >= infos.size()) {
        return std::numeric_limits<double>::max();
    }

    auto prev_node = infos[position - 1].node;
    auto next_node = infos[position].node;

    double old_dist = instance->getDistance(prev_node->getId(), next_node->getId());
    double new_dist = instance->getDistance(prev_node->getId(), node->getId()) + instance->getDistance(node->getId(), next_node->getId());

    return new_dist - old_dist;
}

bool Route::removeCustomer(std::shared_ptr<Customer> customer) {
    for (size_t i = 0; i < infos.size(); ++i) {
        if (infos[i].node == customer) {
            remove(i);
            return true;
        }
    }
    return false;
}