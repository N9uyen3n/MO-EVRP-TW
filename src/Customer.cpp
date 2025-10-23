#include "../include/Customer.h"

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