#include "../include/Node.h"

Node::Node(int id, double x, double y) 
    : id(id), x(x), y(y) {}

int Node::getId() const {
    return id;
}

double Node::getX() const {
    return x;
}

double Node::getY() const {
    return y;
}
