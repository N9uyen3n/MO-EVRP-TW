#include "../../include/core/Depot.h"
#include <sstream>
#include <string>
#include <limits>

Depot::Depot(int id, std::string stringId,double x, double y)
    : Node(id, stringId,x, y, 0, std::numeric_limits<double>::max(), 0), lastTime(0) {}

Depot::Depot(int id, std::string stringId, double x, double y, double readyTime, double dueDate)
    : Node(id, stringId,x, y, readyTime, dueDate, 0), lastTime(0){}


void Depot::setLastTime(double lastTime) {
    this->lastTime = lastTime;
}

double Depot::getLastTime() const {
    return lastTime;
}

std::string Depot::toString() const {
    std::stringstream ss;
    ss << "Depot(id: " << getId() << " , stringId: " << getStringId() << " , x: " << getX() << ", y: " <<
        getY() <<  ", readyTime: " << getReadyTime() << ", dueDate: " << getDueDate() << ")";
    return ss.str();
}