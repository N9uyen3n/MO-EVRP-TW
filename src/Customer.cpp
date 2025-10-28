#include "../include/Customer.h"
#include <sstream>

Customer::Customer(int id, double x, double y, double demand, double readyTime, double dueDate, double serviceTime)
    : Node(id, x, y), demand(demand), readyTime(readyTime), dueDate(dueDate), serviceTime(serviceTime) {}

double Customer::getDemand() const {
    return demand;
}

double Customer::getReadyTime() const {
    return readyTime;
}

double Customer::getDueDate() const {
    return dueDate;
}

double Customer::getServiceTime() const {
    return serviceTime;
}

std::string Customer::toString() const {
    std::stringstream ss;
    ss << "Customer(id: " << getId() << ", x: " << getX() << ", y: " << getY() << ", demand: " << demand << ", readyTime: " << readyTime << ", dueDate: " << dueDate << ", serviceTime: " << serviceTime << ")";
    return ss.str();
}   