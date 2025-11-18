#include "../../include/io/Parser.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Depot.h"
#include "../../include/core/Station.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>

// Helper function to extract value from /value/
static double getValueFromLine(const std::string& line) {
    size_t first = line.find('/');
    size_t second = line.find('/', first + 1);
    if (first == std::string::npos || second == std::string::npos) {
        return 0.0;
    }
    return std::stod(line.substr(first + 1, second - first - 1));
}

std::shared_ptr<Instance> Parser::parse(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open instance file: " + filename);
    }

    std::string line;
    std::vector<std::shared_ptr<Node>> nodes;
    double capacity = 0, battery = 0, energyRate = 0, velocity = 0, chargingRate = 0;

    // Skip header line
    std::getline(file, line);
    int id = 0;
    while (std::getline(file, line)) {
        // Trim whitespace
        line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), line.end());

        if (line.empty()) continue;

        std::stringstream ss(line);
        // Differentiate between parameter lines (containing '/') and node lines.
        if (line.find('/') != std::string::npos) {
            // This is a parameter line
            char firstChar = line[0];
            switch (firstChar) {
                case 'Q': battery = getValueFromLine(line); break;
                // case 'Q': battery = 1000; break;
                case 'C': capacity = getValueFromLine(line); break;
                case 'r': energyRate = getValueFromLine(line); break;
                case 'g': chargingRate = getValueFromLine(line); break;
                case 'v': velocity = getValueFromLine(line); break;
            }
        } else {
            // This is a node line
            std::string stringId, typeStr;
            double x, y, demand, readyTime, dueDate, serviceTime;
            ss >> stringId >> typeStr >> x >> y >> demand >> readyTime >> dueDate >> serviceTime;

            typeStr.erase(typeStr.begin(), std::find_if(typeStr.begin(), typeStr.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
            typeStr.erase(std::find_if(typeStr.rbegin(), typeStr.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(), typeStr.end());
            
            // Node lines should not be empty or malformed
            if (stringId.empty() || typeStr.empty()) continue;

            char firstChar = stringId[0];

            if (firstChar == 'D' || firstChar == 'S' || firstChar == 'C') {
                if (typeStr == "d") {
                    nodes.push_back(std::make_shared<Depot>(id, stringId, x, y,
                        readyTime, dueDate));
                } else if (typeStr == "c") {
                    nodes.push_back(std::make_shared<Customer>(id, stringId, x, y, demand,
                        readyTime, dueDate, serviceTime));
                } else if (typeStr == "f") {
                    nodes.push_back(std::make_shared<Station>(id, stringId, x, y,
                        readyTime, dueDate, chargingRate));
                }

            }
        }
        id++;
    }

    // Post-process stations to add the correct charging rate
    // for (auto& node : nodes) {
    //     // Use dynamic_cast to check if a Node is actually a Station
    //     if (auto station = std::dynamic_pointer_cast<Station>(node)) {
    //         // This is inefficient, we are creating new stations, it's better to modify them.
    //         // For now, let's find and replace.
    //         // A better design would be to store chargingRate in the parser and pass it to constructor.
    //         // But let's stick to this for simplicity of demonstration.
    //         station = Station(station->getId(), station->getX(), station->getY(), chargingRate);
    //     }
    // }

    file.close();

    return std::make_shared<Instance>(nodes, capacity, battery, energyRate, velocity);
}
