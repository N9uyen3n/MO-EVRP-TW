#pragma once
#include "Route.h"
#include <memory>
#include <set>
#include <unordered_set>
#include <vector>

// [OPT-11] Solution hint structure for vehicle reduction guidance
struct SolutionHint {
  int numVehicles = 0;
  double totalDistance = 0.0;
  std::vector<int> customerOrder;  // Preferred insertion order
  std::unordered_set<int> hardToServeCustomers;  // Customers that need special attention
  bool isValid = false;

  void clear() {
    numVehicles = 0;
    totalDistance = 0.0;
    customerOrder.clear();
    hardToServeCustomers.clear();
    isValid = false;
  }
};

class Solution {
public:
    explicit Solution(const std::shared_ptr<Instance> &instance);
    Solution(const Solution &other); // Copy constructor
    Solution &operator=(const Solution &other); // Copy assignment

    // --- Modifications ---
    void addRoute(const Route &route);
    void removeRoute(size_t index);
    void clear();
    bool removeCustomer(int customerId); // Remove a customer from any route by ID
    void removeEmptyRoutes(); // Remove routes with no customers

    // --- Getters ---
    const std::vector<Route> &getRoutes() const;
    std::vector<Route> &getRoutes();
    double getTotalDistance() const;
    double getWorkloadGini() const;
    double getTotalTime() const;
    double getMaxTime() const;
    int getTotalVehicles() const;
    double getHV() const;
    int getNodesCount() const;
    bool isFeasible() const;

    void setHV(double hv);

    // --- Utilities ---
    size_t getNumRoutes() const;
    void evaluateRoutes();
    double
    getAverageRouteTime() const; // ⭐ NEW: For workload-conscious insertion

    bool dominates(const Solution &other) const;
    void print() const;
    std::string toString() const;

    long long getHash() const;

    void markDirty() { feasibleDirty_ = true; }

  // [OPT-11] Solution hint methods
  void setHint(const SolutionHint &hint);
  const SolutionHint &getHint() const;
  void clearHint();
  bool hasValidHint() const;

private:
    void detach(); // Private method for Copy-on-Write
    std::shared_ptr<Instance> instance;
    std::shared_ptr<std::vector<Route>> routes; // Changed to shared_ptr

    int totalVehicles;
    int totalNodes;
    double totalDistance;
    double workloadGini;
    double totalTime;
    double maxTime;
    double HV;

    bool feasible;

    // ⭐ Performance: lazy cache cho isFeasible() — invalidate khi routes thay đổi
    mutable bool feasibleCached_ = false;
    mutable bool feasibleDirty_ = true;

    bool checkGlobalFeasibility() const;

    // ⭐ Performance: customerIdSet_ built once in constructor, never changes
    std::unordered_set<int> customerIdSet_;
  SolutionHint hint_;
    void buildCustomerIdSet(); // Called in constructor
};
