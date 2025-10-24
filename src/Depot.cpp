#include "../include/Depot.h"

Depot::Depot(int id, double x, double y, double lastTime)
    : Node(id, x, y), lastTime(lastTime), readyTime(0) {}

double Depot::getLastTime() const {
    return lastTime;
}
Depot::Depot(int id, double x, double y,double readyTime, double lastTime)
    : Node(id, x, y), lastTime(lastTime), readyTime(readyTime) {}

double Depot::getReadyTime() const {
    return readyTime;
}

