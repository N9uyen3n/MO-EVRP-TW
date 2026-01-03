#ifndef SOLVER_H
#define SOLVER_H

#include <vector>
#include <random>
#include "Instance.h"
#include "Route.h"
#include "Customer.h"
#include "Solution.h"


// Abstract base class for all solver implementations.
class Solver {
public:
    virtual std::vector<Solution> solve() = 0;
    virtual ~Solver();

protected:
    Solver() = default;
};

#endif // SOLVER_H
