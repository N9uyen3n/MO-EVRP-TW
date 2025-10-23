#pragma once
#include "Node.h"

class Station : public Node {
public:
    Station(int id, double x, double y, double chargingRate);
    ~Station() override = default;

    double getChargingRate() const;

private:
    // The 'g' parameter from the file (inverse refueling rate)
    double chargingRate;
};