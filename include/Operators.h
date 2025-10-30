#pragma once // <-- SỬA: Đảm bảo gõ đúng
#include "Solution.h"
#include "Instance.h"
#include <random>
#include <vector>
#include <string>
#include <memory>
#include <set>
#include <limits> // Cho std::numeric_limits

// Cấu trúc để lưu kết quả của một lần chèn
struct InsertionResult {
    int customerId = -1;
    int routeIndex = -1;
    size_t position = 0;
    double cost = std::numeric_limits<double>::infinity();
    bool feasible = false;

    bool operator<(const InsertionResult& other) const;
};

// ==================================================================
// LOẠI 1: PHÁ HỦY KHÁCH HÀNG (CUSTOMER DESTROY)
// ==================================================================

class ICustomerDestroy {
public:
    ICustomerDestroy(std::shared_ptr<Instance> instance, std::mt19937& rng);
    virtual ~ICustomerDestroy() = default;
    virtual void destroy(Solution& solution, int numToRemove, std::vector<int>& unservedCustomers) = 0;

protected:
    std::shared_ptr<Instance> instance;
    std::mt19937& rng;
    // Helper để các lớp con sử dụng
    std::set<int> getAllCustomerIds() const;
};

class RandomRemoval : public ICustomerDestroy {
public:
    using ICustomerDestroy::ICustomerDestroy;
    void destroy(Solution& solution, int numToRemove, std::vector<int>& unservedCustomers) override;
};

class ShawRemoval : public ICustomerDestroy {
public:
    ShawRemoval(std::shared_ptr<Instance> inst, std::mt19937& r,
                double p1, double p2, double p3, double p4, double eta);
    void destroy(Solution& solution, int numToRemove, std::vector<int>& unservedCustomers) override;
private:
    double phi1, phi2, phi3, phi4, determinism;
    double calculateRelatedness(int custId1, int custId2, const Solution& sol);
};

class WorstDistanceRemoval : public ICustomerDestroy {
public:
    WorstDistanceRemoval(std::shared_ptr<Instance> inst, std::mt19937& r, double kappa);
    void destroy(Solution& solution, int numToRemove, std::vector<int>& unservedCustomers) override;
private:
    double determinism_kappa;
};

class GreedyRouteRemoval : public ICustomerDestroy {
public:
    using ICustomerDestroy::ICustomerDestroy;
    void destroy(Solution& solution, int numToRemove, std::vector<int>& unservedCustomers) override;
protected:
    void removeCustomersFromRoute(Solution& solution, size_t routeIndex, std::vector<int>& unservedCustomers);
};

class RandomRouteRemoval : public GreedyRouteRemoval {
public:
    using GreedyRouteRemoval::GreedyRouteRemoval;
    void destroy(Solution& solution, int numToRemove, std::vector<int>& unservedCustomers) override;
};

// ==================================================================
// LOẠI 2: SỬA CHỮA KHÁCH HÀNG (CUSTOMER REPAIR)
// ==================================================================

class ICustomerRepair {
public:
    ICustomerRepair(std::shared_ptr<Instance> instance, std::mt19937& rng);
    virtual ~ICustomerRepair() = default;
    virtual void repair(Solution& solution, std::vector<int>& unservedCustomers) = 0;

protected:
    std::shared_ptr<Instance> instance;
    std::mt19937& rng;
    InsertionResult findBestInsertionForCustomer(int customerId, const Solution& solution);
    std::vector<InsertionResult> findKBestInsertionsForCustomer(int customerId, const Solution& solution, int k);
    void createNewRouteForCustomer(Solution& solution, int custId);
};

class GreedyInsertion : public ICustomerRepair {
public:
    using ICustomerRepair::ICustomerRepair;
    void repair(Solution& solution, std::vector<int>& unservedCustomers) override;
};

class RegretKInsertion : public ICustomerRepair {
public:
    RegretKInsertion(std::shared_ptr<Instance> inst, std::mt19937& r, int k_val);
    void repair(Solution& solution, std::vector<int>& unservedCustomers) override;
private:
    int k;
};

// ==================================================================
// LOẠI 3: PHÁ HỦY TRẠM SẠC (STATION DESTROY)
// ==================================================================

class IStationDestroy {
public:
    IStationDestroy(std::shared_ptr<Instance> instance, std::mt19937& rng);
    virtual ~IStationDestroy() = default;
    virtual void destroy(Solution& solution, int numToRemove) = 0;

protected:
    std::shared_ptr<Instance> instance;
    std::mt19937& rng;
};

class RandomStationRemoval : public IStationDestroy {
public:
    using IStationDestroy::IStationDestroy;
    void destroy(Solution& solution, int numToRemove) override;
};

// ==================================================================
// LOẠI 4: SỬA CHỮA TRẠM SẠC (STATION REPAIR)
// ==================================================================

class IStationRepair {
public:
    IStationRepair(std::shared_ptr<Instance> instance, std::mt19937& rng);
    virtual ~IStationRepair() = default;
    virtual void repair(Solution& solution) = 0;

protected:
    std::shared_ptr<Instance> instance;
    std::mt19937& rng;
};

class GreedyStationInsertion : public IStationRepair {
public:
    using IStationRepair::IStationRepair;
    void repair(Solution& solution) override;
};