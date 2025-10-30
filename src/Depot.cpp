#include "../include/Depot.h"

#include <limits>

Depot::Depot(int id, double x, double y)
    : Node(id, x, y, 0, std::numeric_limits<double>::max(), 0), lastTime(0) {}

void Depot::setLastTime(double lastTime) {
    this->lastTime = lastTime;
}

double Depot::getLastTime() const {
    return lastTime;
}

