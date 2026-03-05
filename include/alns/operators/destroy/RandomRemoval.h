#pragma once

#include "alns/IOperator.h"
#include "core/Instance.h"
#include <memory>
#include <random>
#include <string>
#include <vector>

/**
 * @brief Toán tử phá hủy ngẫu nhiên (Random Removal).
 *
 * Chọn ngẫu nhiên một số khách hàng từ tất cả các tuyến và gỡ bỏ.
 * Mục đích chính: đa dạng hóa (diversification) không gian tìm kiếm
 * bằng cách phá vỡ pattern mà các toán tử heuristic có thể bị mắc kẹt.
 */
class RandomRemoval : public IDestroyOperator {
public:
  explicit RandomRemoval(std::shared_ptr<Instance> instance);

  std::string getName() const override;
  std::vector<int> execute(Solution &solution, int nodesToRemove,
                           std::mt19937 &rng) override;

private:
  std::shared_ptr<Instance> instance;
};
