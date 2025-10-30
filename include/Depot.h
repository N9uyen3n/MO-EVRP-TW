#pragma once
#include "Node.h"

class Depot : public Node {
public:
    Depot(int id, double x, double y);
    ~Depot() override = default;

    // Xóa 'const' ở đây vì hàm này CẦN thay đổi giá trị
    void setLastTime(double lastTime);

    // Thêm 'const' ở đây vì hàm này CHỈ đọc giá trị
    double getLastTime() const;

private:
    double lastTime;
};