#include "../include/Depot.h"

Depot::Depot(int id, double x, double y, double lastTime)
    : Node(id, x, y), lastTime(lastTime) {}

double Depot::getLastTime() const {
    return lastTime;
}
