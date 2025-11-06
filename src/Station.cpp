#include "../include/Station.h"
#include <sstream>

Station::Station(int id, std::string stringId, double x, double y, double chargingRate)
    : Node(id, stringId, x, y, 0, 999999, 0), chargingRate(chargingRate) {}
Station::Station(int id, std::string stringId, double x, double y, double readyTime, double dueDate, double chargingRate)
    : Node(id, stringId, x, y, readyTime, dueDate, 0), chargingRate(chargingRate) {}


void Station::setChargingRate(double chargingRate) {
    this->chargingRate = chargingRate;
}

double Station::getChargingRate() const {
    return chargingRate;
}

std::string Station::toString() const {
    std::stringstream ss;
    // ss << "Station(id: " << getId() << " , stringId: " << getStringId() <<  ", x: " << getX() << ", y: "
    // << getY() << ", chargingRate: " << chargingRate << ")";
    ss << "Station(id: " << getId() << " , stringId: " << getStringId() << " , x: " << getX() << ", y: " <<
       getY() << ", readyTime: " << getReadyTime() << ", dueDate: " << getDueDate() << ")";
    return ss.str();
}