#include "../../include/core/Solution.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Instance.h"
#include <algorithm> // Required for std::sort
#include <cmath>     // Required for std::pow
#include <iomanip>
#include <iostream>
#include <numeric> // Required for std::accumulate
#include <ostream>
#include <set>
#include <sstream>

// Private detach function for Copy-on-Write.
// If the data is shared (ref count > 1), it creates a deep copy and points to
// it, ensuring that modifications do not affect other Solution objects.
void Solution::detach() {
  if (routes.use_count() > 1) {
    routes = std::make_shared<std::vector<Route>>(*routes);
  }
}

Solution::Solution(const std::shared_ptr<Instance> &instance)
    : instance(instance),
      routes(std::make_shared<std::vector<Route>>()), // Initialize with a new
      // vector
      totalVehicles(0), totalDistance(0.0), workloadGini(0.0), totalTime(0.0),
      maxTime(0.0), feasible(true) {
  buildCustomerIdSet(); // Build once, never changes for this instance
}

void Solution::buildCustomerIdSet() {
  customerIdSet_.clear();
  for (const auto &node : instance->getNodes()) {
    if (node->getType() == NodeType::CUSTOMER) {
      customerIdSet_.insert(node->getId());
    }
  }
}

// Copy constructor: Just copy the shared_ptr. This is a fast, shallow copy.
Solution::Solution(const Solution &other) = default;

// Copy assignment operator: Default behavior is correct for shared_ptr.
Solution &Solution::operator=(const Solution &other) = default;

void Solution::addRoute(const Route &route) {
  detach(); // Ensure unique copy before modification
  routes->push_back(route);
  feasibleDirty_ = true; // Invalidate cache
}

void Solution::removeRoute(size_t index) {
  detach(); // Ensure unique copy before modification
  if (index >= routes->size()) {
    throw std::out_of_range("Chỉ số tuyến đường không hợp lệ.");
  }
  routes->erase(routes->begin() + index);
  feasibleDirty_ = true; // Invalidate cache
}

void Solution::clear() {
  detach(); // Ensure unique copy before modification
  routes->clear();
  feasibleDirty_ = true;
}

bool Solution::removeCustomer(int customerId) {
  detach();
  for (auto &route : *routes) {
    const auto &nodes = route.getNodes();
    for (size_t pos = 0; pos < nodes.size(); ++pos) {
      if (nodes[pos] == customerId) {
        route.removeNode(pos);
        feasibleDirty_ = true;
        return true;
      }
    }
  }
  return false; // Not found
}

void Solution::removeEmptyRoutes() {
  detach();
  routes->erase(
      std::remove_if(routes->begin(), routes->end(),
                     [](const Route &r) { return r.getCustomers().empty(); }),
      routes->end());
  feasibleDirty_ = true;
}

const std::vector<Route> &Solution::getRoutes() const {
  return *routes; // Return const reference to the vector
}

std::vector<Route> &Solution::getRoutes() {
  detach(); // Ensure unique copy before returning a mutable reference
  return *routes;
}

size_t Solution::getNumRoutes() const {
  return routes->size(); // Use pointer
}

double Solution::getTotalDistance() const { return this->totalDistance; }

double Solution::getWorkloadGini() const { return this->workloadGini; }

double Solution::getTotalTime() const {
  double total = 0.0;
  for (const auto &route : *routes) {
    // Use pointer
    total += route.getTotalTime();
  }
  return total;
}

double Solution::getMaxTime() const {
  double total = 0.0;
  for (const auto &route : *routes) {
    // Use pointer
    if (total < route.getTotalTime())
      total = route.getTotalTime();
  }
  return total;
}

int Solution::getTotalVehicles() const { return this->totalVehicles; }

int Solution::getNodesCount() const {
  int totalNodes = 0;
  for (const Route &route : *routes) { // Nhớ deferencing pointer *routes
    totalNodes += route.getNodes().size() - 2; // Exclude depot start/end
  }
  return totalNodes;
}

bool Solution::isFeasible() const {
  if (!feasibleDirty_)
    return feasibleCached_;
  feasibleCached_ = checkGlobalFeasibility();
  feasibleDirty_ = false;
  return feasibleCached_;
}

void Solution::evaluateRoutes() {
  detach(); // We are about to modify the routes vector itself (erase)

  for (auto &route : *this->routes) {
    // Chỉ evaluate nếu route bị đánh dấu dirty (xử lý nội bộ trong Route)
    route.evaluate();
  }

  routes->erase(std::remove_if(routes->begin(), routes->end(),
                               [](const Route &route) {
                                 return route.getNodes().size() <= 2;
                               }),
                routes->end());

  this->totalVehicles = routes->size(); // Use pointer
  this->totalDistance = 0.0;
  this->workloadGini = 0.0;
  this->totalTime = 0.0;
  this->maxTime = 0.0;
  this->feasible = true;

  std::vector<double> route_durations;
  route_durations.reserve(routes->size());

  for (const auto &route : *this->routes) {
    // Use pointer
    this->totalDistance += route.getTotalDistance();
    this->totalTime += route.getTotalTime();
    // User Request: Use Active Time (Travel + Service + Charge) for Gini
    // This accurately reflects the true workload (effort) of the driver
    // rather than the bloated total time which includes idle waiting time.
    route_durations.push_back(route.getActiveTime());

    if (route.getTotalTime() > this->maxTime) {
      this->maxTime = route.getTotalTime();
    }

    if (!route.isFeasible()) {
      this->feasible = false;
    }
  }

  // Calculate Workload Gini Coefficient
  if (route_durations.size() > 1) {
    // Gini = (Sum of absolute differences) / (2 * n * Sum of values)
    // Requires sorted values for efficient calculation, or O(n^2) for pairwise.
    // Sorted formula: G = (2 / n) * (Sum(i * x_i) / Sum(x_i)) - (n + 1) / n
    // where x_i are sorted in non-decreasing order (x_1 <= x_2 <= ... <= x_n)

    std::sort(route_durations.begin(), route_durations.end());
    double sum_val =
        std::accumulate(route_durations.begin(), route_durations.end(), 0.0);

    if (sum_val > 1e-6) {
      double num = 0.0;
      int n = route_durations.size();
      for (int i = 0; i < n; ++i) {
        // Formula uses 1-based index i+1 for x_i
        num += (i + 1) * route_durations[i];
      }
      this->workloadGini = (2.0 * num) / (n * sum_val) - (double)(n + 1) / n;
    } else {
      this->workloadGini = 0.0;
    }
  } else {
    this->workloadGini = 0.0; // Gini is 0 for single vehicle (perfect equality
                              // with itself? or undefined. Let's say 0)
  }

  // Cache the inline calculated feasibility to avoid redundant re-evaluation.
  if (this->feasible) {
    this->feasibleCached_ = checkGlobalFeasibility();
  } else {
    this->feasibleCached_ = false;
  }
  this->feasibleDirty_ = false;
}

// ⭐ NEW: Get average route time for workload-conscious insertion
double Solution::getAverageRouteTime() const {
  if (routes->empty())
    return 0.0;

  double totalTime = 0.0;
  int nonEmptyRoutes = 0;

  for (const auto &route : *routes) {
    if (route.size() > 2) {
      // Not empty (has customers)
      totalTime += route.getTotalTime();
      nonEmptyRoutes++;
    }
  }

  return nonEmptyRoutes > 0 ? totalTime / nonEmptyRoutes : 0.0;
}

bool Solution::checkGlobalFeasibility() const {
  // customerIdSet_ được build 1 lần trong constructor — không rebuild
  std::unordered_set<int> customers_served;
  customers_served.reserve(customerIdSet_.size());

  for (const Route &route : *this->routes) {
    if (!route.isFeasible()) {
      return false;
    }

    for (int node_id : route.getNodes()) {
      if (customerIdSet_.count(node_id)) {
        if (customers_served.count(node_id)) {
          return false; // Duplicate customer
        }
        customers_served.insert(node_id);
      }
    }
  }

  return customers_served.size() == customerIdSet_.size();
}

// bool Solution::dominates(const Solution& other) const {
//     // Ưu tiên 1 (Tuyệt đối): Số lượng xe (totalVehicles).
//     if (this->totalVehicles < other.totalVehicles) {
//         return true; // Thắng ngay lập tức nếu ít xe hơn.
//     }
//     if (this->totalVehicles > other.totalVehicles) {
//         return false; // Thua ngay lập tức nếu nhiều xe hơn.
//     }

//     // Ưu tiên 2: Nếu số xe bằng nhau, xét Pareto trên các mục tiêu còn lại.
//     // A dominates B <=> A không tệ hơn B ở mọi mặt VÀ A tốt hơn B ở ít nhất
//     1 mặt. bool betterInAtLeastOne = false;

//     // CHECK 2: Distance
//     if (this->totalDistance > other.totalDistance + 1e-4) return false; // Tệ
//     hơn if (this->totalDistance < other.totalDistance - 1e-4)
//     betterInAtLeastOne = true;

//     // CHECK 3: Workload Variance
//     if (this->workloadVariance > other.workloadVariance + 1e-4) return false;
//     // Tệ hơn if (this->workloadVariance < other.workloadVariance - 1e-4)
//     betterInAtLeastOne = true;

//     // CHECK 4: Max Time
//     if (this->maxTime > other.maxTime + 1e-4) return false; // Tệ hơn
//     if (this->maxTime < other.maxTime - 1e-4) betterInAtLeastOne = true;

//     return betterInAtLeastOne;
// }

// bool Solution::dominates(const Solution& other) const {
//     // Ưu tiên 1 (Tuyệt đối): Số lượng xe (totalVehicles).
//     if (this->totalVehicles < other.totalVehicles) {
//         return true; // Thắng ngay lập tức nếu ít xe hơn.
//     }
//     if (this->totalVehicles > other.totalVehicles) {
//         return false; // Thua ngay lập tức nếu nhiều xe hơn.
//     }

//     // Ưu tiên 2: Nếu số xe bằng nhau, xét Pareto trên các mục tiêu còn lại.
//     // A dominates B <=> A không tệ hơn B ở mọi mặt VÀ A tốt hơn B ở ít nhất
//     1 mặt. bool betterInAtLeastOne = false;

//     // CHECK 2: Distance
//     if (this->totalDistance > other.totalDistance + 1e-4) return false; // Tệ
//     hơn if (this->totalDistance < other.totalDistance - 1e-4)
//     betterInAtLeastOne = true;

//     return betterInAtLeastOne;
// }

bool Solution::dominates(const Solution &other) const {
  // A dominates B <=> A không tệ hơn B ở mọi mặt VÀ A tốt hơn B ở ít nhất 1
  // mặt.
  bool betterInAtLeastOne = false;

  if (this->totalVehicles < other.totalVehicles) {
    return true; // Thắng ngay lập tức nếu ít xe hơn.
  }
  if (this->totalVehicles > other.totalVehicles) {
    return false; // Thua ngay lập tức nếu nhiều xe hơn.
  }
  // CHECK 2: Distance
  if (this->totalDistance > other.totalDistance + 1e-4)
    return false; // Tệ hơn
  if (this->totalDistance < other.totalDistance - 1e-4)
    betterInAtLeastOne = true;

  // 4. Workload Balance (Gini Coefficient)
  // Lower Gini is better (0 = perfect equality, 1 = max inequality)
  if (this->workloadGini > other.workloadGini + 1e-3)
    return false;
  if (this->workloadGini < other.workloadGini - 1e-3)
    betterInAtLeastOne = true;

  // CHECK 4: Max Time
  if (this->maxTime > other.maxTime + 1e-4)
    return false; // Tệ hơn
  if (this->maxTime < other.maxTime - 1e-4)
    betterInAtLeastOne = true;

  return betterInAtLeastOne;
}

// bool Solution::dominates(const Solution &other) const {
//   // A dominates B <=> A không tệ hơn B ở mọi mặt VÀ A tốt hơn B ở ít nhất 1
//   // mặt.
//   bool betterInAtLeastOne = false;
//
//   if (this->totalVehicles > other.totalVehicles) {
//     return false; // Thua ngay lập tức nếu nhiều xe hơn.
//   }
//   if (this->totalVehicles < other.totalVehicles) {
//     betterInAtLeastOne = true;
//   }
//
//   // CHECK 2: Distance
//   if (this->totalDistance > other.totalDistance + 1e-4)
//     return false; // Tệ hơn
//   if (this->totalDistance < other.totalDistance - 1e-4)
//     betterInAtLeastOne = true;
//
//   // 4. Workload Balance (Gini Coefficient)
//   // Lower Gini is better (0 = perfect equality, 1 = max inequality)
//   if (this->workloadGini > other.workloadGini + 1e-3)
//     return false;
//   if (this->workloadGini < other.workloadGini - 1e-3)
//     betterInAtLeastOne = true;
//
//   // CHECK 4: Max Time
//   if (this->maxTime > other.maxTime + 1e-4)
//     return false; // Tệ hơn
//   if (this->maxTime < other.maxTime - 1e-4)
//     betterInAtLeastOne = true;
//
//   return betterInAtLeastOne;
// }

void Solution::print() const {
  std::cout << "Total Vehicles: " << getTotalVehicles()
            << " Total Distance: " << getTotalDistance()
            << " Workload Gini: " << getWorkloadGini()
            << " Total Time: " << getTotalTime()
            << " Max Time: " << getMaxTime() << std::endl;
  for (const auto &route : *routes) {
    // Use pointer
    route.print();
  }
}

std::string Solution::toString() const {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(2);

  ss << "Solution (Vehicles: " << getTotalVehicles()
     << ", Distance: " << getTotalDistance()
     << ", WorkloadGini: " << getWorkloadGini() << ", MaxTime: " << getMaxTime()
     << ", Feasible: " << (isFeasible() ? "true" : "false") << ")\n";

  for (const auto &route : *routes) {
    // Use pointer
    ss << route.toString();
  }

  return ss.str();
}

long long Solution::getHash() const {
  long long hash = 0;
  for (const auto &route : *routes) {
    // Use pointer
    hash ^= route.getHash();
  }
  return hash;
}
