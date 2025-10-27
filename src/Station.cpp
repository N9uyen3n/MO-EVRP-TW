#include "../include/Station.h"

Station::Station(int id, double x, double y, double chargingRate)
    : Node(id, x, y, 0, 0, 0), chargingRate(chargingRate) {}

double Station::getChargingRate() const {
    return chargingRate;
}