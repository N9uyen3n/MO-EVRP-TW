#pragma once
#include "Node.h"
#include <memory>

struct RouteInfo {
    std::shared_ptr<Node> node; // Điểm dừng (khách hàng, depot, hoặc trạm sạc)

    // Trạng thái của xe khi đến điểm dừng này
    double arrival_time;
    double arrival_load;
    double arrival_battery;

    double wait_time; // Thời gian chờ tại điểm dừng

    // Trạng thái của xe khi rời điểm dừng này (sau khi phục vụ/sạc)
    double departure_time;
    double departure_load;
    double departure_battery;
};
