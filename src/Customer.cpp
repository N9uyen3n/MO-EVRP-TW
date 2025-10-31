#include "../include/Customer.h"
#include <sstream>
#include <string>
Customer::Customer(int id, std::string stringId, double x, double y,
    double demand, double readyTime, double dueDate, double serviceTime)
    : Node(id, stringId, x, y), demand(demand), readyTime(readyTime), dueDate(dueDate), serviceTime(serviceTime) {}

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
    ss << "Customer(id: " << getId() << " , stringId: " << getStringId() << " , x: " << getX() << ", y: " <<
        getY() << ", demand: " << demand << ", readyTime: " << readyTime << ", dueDate: " << dueDate << ", serviceTime: " << serviceTime << ")";
    return ss.str();
}   