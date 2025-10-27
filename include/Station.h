#pragma once
#include "Node.h"

class Station : public Node {
public:
    Station(int id, double x, double y, double chargingRate);
    ~Station() override = default;

    double getChargingRate() const;

private:
    // Tốc đọ nạp nược (phút / năng lượng)
    double chargingRate;
};