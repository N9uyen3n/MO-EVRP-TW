#pragma once

#include "../core/Instance.h"
#include <iostream>
#include <cmath>

class Utils {
public:
    // Prints basic information about the instance
    static void printInstance(const Instance& instance);

    // Calculates the Euclidean distance between two nodes
    static double euclideanDistance(const Node& a, const Node& b) {
        return std::sqrt(std::pow(a.getX() - b.getX(), 2) + std::pow(a.getY() - b.getY(), 2));
    }
};
