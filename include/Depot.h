#pragma once
#include "Node.h"

class Depot : public Node {
    public:
        Depot(int id, double x, double y, double lastTime);
        ~Depot() override = default;

        double getLastTime() const;

    private:
        double lastTime;
};
