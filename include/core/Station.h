#pragma once
#include "Node.h"
#include <string>

class Station : public Node {
public:
    Station(int id, std::string stringId, double x, double y, double chargingRate);
    Station(int id, std::string stringId, double x, double y, double readyTime, double dueDate, double chargingRate);
    ~Station() override = default;
    void setChargingRate(double chargingRate);
    double getChargingRate() const;
    std::string toString() const override;

private:
    // Tốc đọ nạp nược (phút / năng lượng)
    double chargingRate;
};