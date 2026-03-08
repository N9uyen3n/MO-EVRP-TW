#pragma once

#include "alns/IOperator.h"
#include "core/Instance.h"
#include <memory>
#include <random>
#include <string>
#include <vector>

/**
 * @brief Toán tử phá hủy toàn bộ một tuyến ngẫu nhiên (Random Route Removal).
 *
 * Chọn một tuyến (route) dựa trên xác suất có trọng số (tuyến càng ít khách,
 * xác suất chọn càng cao). Sau đó, xóa toàn bộ tuyến này và trả khách hàng về
 * danh sách unserved.
 */
class RandomRouteRemoval : public IDestroyOperator {
public:
  explicit RandomRouteRemoval(std::shared_ptr<Instance> instance);

  std::string getName() const override;
  std::vector<int> execute(Solution &solution, int nodesToRemove,
                           std::mt19937 &rng) override;

private:
  std::shared_ptr<Instance> instance;
};
