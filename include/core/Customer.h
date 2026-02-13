#pragma once
#include "Node.h"
#include <string>

class Customer : public Node {
    public:
        Customer(int id, std::string stringId ,double x, double y, double demand,
            double readyTime, double dueDate, double serviceTime);
        ~Customer() override = default;

        NodeType getType() const override {
            return NodeType::CUSTOMER;
        }
        
        double getDemand() const override;
        double getReadyTime() const;
        double getDueDate() const;
        double getServiceTime() const;
        std::string toString() const override;

    private:
        double demand;
        double readyTime;
        double dueDate;
        double serviceTime;
};