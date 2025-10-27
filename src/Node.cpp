#include "../include/Node.h"

Node::Node(int id, double x, double y) 
    : id(id), x(x), y(y) {}

Node::Node(int id, double x, double y, double readyTime, double dueDate, double serviceTime)
    : id(id), x(x), y(y), readyTime(readyTime), dueDate(dueDate), serviceTime(serviceTime) {}

int Node::getId() const {
    return id;
}

double Node::getReadyTime() const {
    return readyTime;
}

double Node::getDueDate() const {
    return dueDate;
}

double Node::getServiceTime() const {
    return serviceTime;
}

double Node::getX() const {
    return x;
}

double Node::getY() const {
    return y;
}
