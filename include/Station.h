#pragma once
#include "Node.h"
#include <string>

class Station : public Node {
public:
    Station(int id, double x, double y, double chargingRate);
    ~Station() override = default;

    double getChargingRate() const;
    std::string toString() const override;

private:
    // Tốc đọ nạp nược (phút / năng lượng)
    double chargingRate;
};