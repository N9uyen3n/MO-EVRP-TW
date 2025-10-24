#pragma once
#include "Node.h"

class Depot : public Node {
    public:
        Depot(int id, double x, double y, double lastTime);
        Depot(int id, double x, double y,double readyTime, double lastTime);
        ~Depot() override = default;

        double getLastTime() const;
        double getReadyTime() const;

    private:
        double lastTime;
        double readyTime;
};
