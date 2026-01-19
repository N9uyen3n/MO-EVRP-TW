#include "../../include/alns/ParetoArchive.h"
#include <algorithm>
#include <limits>
#include <numeric>   // For std::iota
#include <stdexcept>
#include <cmath>     // For std::abs
#include <iostream>

ParetoArchive::ParetoArchive(int maxSize) : maxSize(maxSize) {
    if (maxSize <= 0) {
        throw std::invalid_argument("ParetoArchive max size must be positive.");
    }
}

size_t ParetoArchive::getSize() const {
    return archive.size();
}

const std::vector<Solution>& ParetoArchive::getFront() const {
    return archive;
}

Solution ParetoArchive::getRandomSolution(std::mt19937& rng) const {
    if (archive.empty()) {
        throw std::runtime_error("Cannot get random solution: Pareto archive is empty.");
    }
    std::uniform_int_distribution<> dist(0, static_cast<int>(archive.size() - 1));
    return archive[dist(rng)];
}

AddResult ParetoArchive::tryAdd(const Solution& newSolution) {
    bool isDominated = false;
    std::vector<int> dominatedIndices;
    const double EPSILON = 1e-6;

    for (size_t i = 0; i < archive.size(); ++i) {
        const auto& existingSolution = archive[i];

        // 1. Check for duplicates based on the actual objectives from Solution::dominates
        bool sameVeh = existingSolution.getTotalVehicles() == newSolution.getTotalVehicles();
        bool sameDist = std::abs(existingSolution.getTotalDistance() - newSolution.getTotalDistance()) < EPSILON;
        bool sameWorkload = std::abs(existingSolution.getWorkloadVariance() - newSolution.getWorkloadVariance()) < EPSILON;
        bool sameMaxTime = std::abs(existingSolution.getMaxTime() - newSolution.getMaxTime()) < EPSILON;

        if (sameVeh && sameDist && sameWorkload && sameMaxTime) {
            return AddResult::IDENTICAL;
        }

        // 2. Check if the new solution is dominated
        if (existingSolution.dominates(newSolution)) {
            isDominated = true;
            break;
        }

        // 3. Check if the new solution dominates any existing solution
        if (newSolution.dominates(existingSolution)) {
            dominatedIndices.push_back(static_cast<int>(i));
        }
    }

    if (isDominated) {
        return AddResult::DOMINATED;
    }

    // Remove dominated existing solutions (iterate backwards to preserve indices)
    std::sort(dominatedIndices.rbegin(), dominatedIndices.rend());
    for (int index : dominatedIndices) {
        archive.erase(archive.begin() + index);
    }

    // Add the new solution
    archive.push_back(newSolution);

    // Prune if the archive exceeds its maximum size
    if (archive.size() > static_cast<size_t>(maxSize)) {
        prune();
    }

    if (!dominatedIndices.empty()) {
        return AddResult::DOMINATING;
    } else {
        return AddResult::NON_DOMINATED;
    }
}

Solution ParetoArchive::getBestSolutionForObjective(int objectiveIndex) const {
    if (archive.empty()) throw std::runtime_error("Archive is empty");

    // Comparator lambda based on the corrected objectives
    auto compareFn = [&](const Solution& a, const Solution& b) {
        switch(objectiveIndex) {
            case 0: return a.getTotalDistance() < b.getTotalDistance();      // Objective: Distance
            case 1: return a.getWorkloadVariance() < b.getWorkloadVariance(); // Objective: Workload Variance
            case 2: return a.getMaxTime() < b.getMaxTime();                 // Objective: Max Time
            case 3: return a.getTotalVehicles() < b.getTotalVehicles();       // Objective: Vehicles
            default: return a.getTotalDistance() < b.getTotalDistance();
        }
    };

    return *std::min_element(archive.begin(), archive.end(), compareFn);
}

// Pruning function using Crowding Distance with the corrected objectives
void ParetoArchive::prune() {
    size_t n = archive.size();
    if (n <= static_cast<size_t>(maxSize)) return;

    // Objectives for crowding distance: Vehicles, Distance, Workload Variance, Max Time
    const int numObjectives = 4;
    std::vector<double> crowdingDistances(n, 0.0);
    std::vector<int> indices(n);
    std::iota(indices.begin(), indices.end(), 0);

    for (int m = 0; m < numObjectives; ++m) {
        // Sort by the current objective
        auto sortFn = [&](int a, int b) {
            switch (m) {
                case 0: return archive[a].getTotalVehicles() < archive[b].getTotalVehicles();
                case 1: return archive[a].getTotalDistance() < archive[b].getTotalDistance();
                case 2: return archive[a].getWorkloadVariance() < archive[b].getWorkloadVariance();
                case 3: return archive[a].getMaxTime() < archive[b].getMaxTime();
                default: return false;
            }
        };
        std::sort(indices.begin(), indices.end(), sortFn);

        // Assign infinite distance to the boundary points to always keep them
        crowdingDistances[indices[0]] = std::numeric_limits<double>::infinity();
        crowdingDistances[indices[n - 1]] = std::numeric_limits<double>::infinity();

        // Get min/max values for the current objective
        double val_min, val_max;
        switch (m) {
            case 0: val_min = archive[indices[0]].getTotalVehicles(); val_max = archive[indices[n-1]].getTotalVehicles(); break;
            case 1: val_min = archive[indices[0]].getTotalDistance(); val_max = archive[indices[n-1]].getTotalDistance(); break;
            case 2: val_min = archive[indices[0]].getWorkloadVariance(); val_max = archive[indices[n-1]].getWorkloadVariance(); break;
            case 3: val_min = archive[indices[0]].getMaxTime(); val_max = archive[indices[n-1]].getMaxTime(); break;
            default: val_min = 0; val_max = 1; break; // Should not happen
        }

        double range = val_max - val_min;
        if (range < 1e-9) range = 1.0; // Avoid division by zero

        // Add to the crowding distance
        for (size_t i = 1; i < n - 1; ++i) {
            if (crowdingDistances[indices[i]] == std::numeric_limits<double>::infinity()) continue;

            double prevVal, nextVal;
            switch (m) {
                case 0: prevVal = archive[indices[i-1]].getTotalVehicles(); nextVal = archive[indices[i+1]].getTotalVehicles(); break;
                case 1: prevVal = archive[indices[i-1]].getTotalDistance(); nextVal = archive[indices[i+1]].getTotalDistance(); break;
                case 2: prevVal = archive[indices[i-1]].getWorkloadVariance(); nextVal = archive[indices[i+1]].getWorkloadVariance(); break;
                case 3: prevVal = archive[indices[i-1]].getMaxTime(); nextVal = archive[indices[i+1]].getMaxTime(); break;
                default: prevVal = 0; nextVal = 0; break; // Should not happen
            }
            crowdingDistances[indices[i]] += (nextVal - prevVal) / range;
        }
    }

    // Find the element with the smallest crowding distance (densest region) to remove
    int worstIndex = static_cast<int>(std::min_element(crowdingDistances.begin(), crowdingDistances.end()) - crowdingDistances.begin());
    archive.erase(archive.begin() + worstIndex);
}

void ParetoArchive::clear() {
    archive.clear();
}
