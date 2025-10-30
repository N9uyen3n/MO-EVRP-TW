#include "../include/Vehicle.h"

Vehicle::Vehicle(int id, double capacity, double batteryCapacity, double energyConsumptionRate)
    : id(id), 
      capacity(capacity), 
      batteryCapacity(batteryCapacity), 
      energyConsumptionRate(energyConsumptionRate) {}

int Vehicle::getId() const {
    return id;
}

double Vehicle::getCapacity() const {
    return capacity;
}

double Vehicle::getBatteryCapacity() const {
    return batteryCapacity;
}

double Vehicle::getEnergyConsumptionRate() const {
    return energyConsumptionRate;
}
