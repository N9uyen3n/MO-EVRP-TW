#include "../../include/alns/ObjectiveEvaluator.h"
#include <iostream>

namespace alns {

ObjectiveEvaluator::ObjectiveEvaluator()
    : refVehicles_(1), refDistance_(1.0), refGini_(1.0), lambdaStart_(0.1),
      lambdaEnd_(5.0), tau_(0.3), maxIterations_(1000), w_V_(1000.0),
      w_D_(1.0) {}

void ObjectiveEvaluator::setReferences(int refVehicles, double refDistance,
                                       double refGini) {
  this->refVehicles_ = std::max(1, refVehicles);
  this->refDistance_ = std::max(1.0, refDistance);
  this->refGini_ = std::max(0.0001, refGini); // Avoid division by zero

  // std::cout << "[ObjectiveEvaluator] References set: V=" << refVehicles_
  //           << ", D=" << refDistance_ << ", G=" << refGini_ << std::endl;
}

void ObjectiveEvaluator::setAdaptiveParams(double lambdaStart, double lambdaEnd,
                                           double tau, int maxIterations) {
  this->lambdaStart_ = lambdaStart;
  this->lambdaEnd_ = lambdaEnd;
  this->tau_ = tau;
  this->maxIterations_ = std::max(1, maxIterations);
}

double ObjectiveEvaluator::getCurrentFairnessWeight(int iteration) const {
  double t_ratio = static_cast<double>(iteration) / maxIterations_;

  if (t_ratio <= tau_) {
    return lambdaStart_;
  } else if (t_ratio >= 1.0) {
    return lambdaEnd_;
  } else {
    // Linear interpolation from tau to 1.0
    double progress = (t_ratio - tau_) / (1.0 - tau_);
    return lambdaStart_ + progress * (lambdaEnd_ - lambdaStart_);
  }
}

double ObjectiveEvaluator::computeScalarCost(const Solution &sol,
                                             int iteration) {
  // 1. Normalize Objectives
  double z_veh = static_cast<double>(sol.getTotalVehicles()) / refVehicles_;
  double z_dist = sol.getTotalDistance() / refDistance_;
  double z_gini = sol.getGiniCoefficient() / refGini_;

  // 2. Get Adaptive Weight
  double w_F = getCurrentFairnessWeight(iteration);

  // 3. Compute Scalar Cost
  return (w_V_ * z_veh) + (w_D_ * z_dist) + (w_F * z_gini);
}

} // namespace alns
