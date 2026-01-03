#pragma once

class Vehicle {
public:
    Vehicle(int id, double capacity, double batteryCapacity, double energyConsumptionRate);
    ~Vehicle() = default;

    int getId() const;
    double getCapacity() const;          // Tải trọng tối đa (C)
    double getBatteryCapacity() const;   // Dung lượng pin tối đa (Q)
    double getEnergyConsumptionRate() const; // Mức tiêu thụ năng lượng (h)
    
    // double getCurrentLoad() const;      // Tải trọng hiện tại
    // double getCurrentBattery() const;   // Mức pin hiện tại

    // void setCurrentLoad(double load);       // Cập nhật tải trọng hiện tại
    // void setCurrentBattery(double battery); // Cập nhật mức pin hiện tại

private:
    int id;
    double capacity;
    double batteryCapacity;
    double energyConsumptionRate;
    // double currentLoad; // Tải trọng hiện tại
    // double currentBattery; // Mức pin hiện tại
};
