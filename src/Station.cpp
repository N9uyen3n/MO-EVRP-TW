#include "../include/Station.h"
#include <sstream>

Station::Station(int id, double x, double y, double chargingRate)
    : Node(id, x, y, 0, 999999, 0), chargingRate(chargingRate) {}

double Station::getChargingRate() const {
    return chargingRate;
}

std::string Station::toString() const {
    std::stringstream ss;
    ss << "Station(id: " << getId() << ", x: " << getX() << ", y: " << getY() << ", chargingRate: " << chargingRate << ")";
    return ss.str();
}