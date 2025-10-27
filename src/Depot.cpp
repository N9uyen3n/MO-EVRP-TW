#include "../include/Depot.h"

Depot::Depot(int id, double x, double y, double lastTime)
    : Node(id, x, y, 0, 0, 0), lastTime(lastTime), readyTime(0) {}

double Depot::getLastTime() const {
    return lastTime;
}
double Depot::getReadyTime() const {
    return readyTime;
}

