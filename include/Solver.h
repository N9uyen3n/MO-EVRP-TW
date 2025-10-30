#ifndef SOLVER_H
#define SOLVER_H

#include <vector>
#include "Solution.h"


// Abstract base class for all solver implementations.
class Solver {
public:
    virtual std::vector<Solution> solve();
    virtual ~Solver() = default;

protected:
    Solver() = default;
};

#endif // SOLVER_H
