#include "../../include/alns/SolutionPool.h"

namespace alns {

SolutionPool::SolutionPool(size_t poolSize, std::shared_ptr<Instance> instance)
    : instance(instance) {
    if (poolSize == 0) {
        throw std::invalid_argument("Pool size must be greater than 0.");
    }
    pool.reserve(poolSize);
    for (size_t i = 0; i < poolSize; ++i) {
        pool.emplace_back(this->instance);
    }
    inUse.assign(poolSize, false);
}

Solution& SolutionPool::acquire() {
    for (size_t i = 0; i < inUse.size(); ++i) {
        if (!inUse[i]) {
            inUse[i] = true;
            return pool[i];
        }
    }
    throw std::runtime_error("SolutionPool: No available solutions in the pool.");
}

void SolutionPool::release(const Solution& solution) {
    // Tính toán địa chỉ offset để tìm index
    const Solution* solution_ptr = &solution;
    if (solution_ptr >= &pool[0] && solution_ptr <= &pool.back()) {
        size_t index = solution_ptr - &pool[0];
        if (inUse[index]) {
            inUse[index] = false;
        } else {
            // Lỗi: cố gắng trả lại một solution không được đánh dấu là đang sử dụng
            throw std::logic_error("SolutionPool: Releasing a solution that was not in use.");
        }
    } else {
        // Lỗi: cố gắng trả lại một solution không thuộc về pool này
        throw std::logic_error("SolutionPool: Releasing a solution that does not belong to this pool.");
    }
}

} // namespace alns
