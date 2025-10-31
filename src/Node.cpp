#include "../include/Node.h"
#include <sstream>
#include <string>

Node::Node(int id, std::string stringId, double x, double y)
    : id(id), stringId(stringId), x(x), y(y) {}

Node::Node(int id, std::string stringId, double x, double y, double readyTime, double dueDate, double serviceTime)
    : id(id), stringId(stringId),  x(x), y(y), readyTime(readyTime), dueDate(dueDate), serviceTime(serviceTime) {}

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

std::string Node::getStringId() const {
    return stringId;
}
std::string Node::toString() const {
    std::stringstream ss;
    ss << "Node(id: " << id << ", String ID: " << stringId <<", x: " << x << ", y: " << y << ")";
    return ss.str();
}

