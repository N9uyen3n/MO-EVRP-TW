#pragma once
#include "Node.h"

class Customer : public Node {
    public:
        Customer(int id, double x, double y, double demand, double readyTime, double dueDate, double serviceTime);
        ~Customer() override = default;
        double getDemand() const;
        double getReadyTime() const;
        double getDueDate() const;
        double getServiceTime() const;

    private:
        double demand;
        double readyTime;
        double dueDate;
        double serviceTime;
};