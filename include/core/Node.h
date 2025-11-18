#pragma once
#include <string>

class Node {
    public:
        Node(int id, std::string stringId, double x, double y); // Constructor cho Node cơ bản
        Node(int id, std::string stringId, double x, double y,
            double readyTime, double dueDate, double serviceTime); // Constructor cho Node với thông tin chi tiết
        // Node(int id, std::string stringId, std::string type, double x, double y,
        //     double readyTime, double dueDate, double serviceTime); // Constructor cho Node với thông tin chi tiết
        virtual ~Node() = default;

        int getId() const;
        double getX() const;
        double getY() const;
        virtual double getDemand() const;
        double getReadyTime() const;
        double getDueDate() const;
        double getServiceTime() const;
        // std::string getType() const;
        std::string getStringId() const;
        virtual std::string toString() const;

    private:
        int id; // ID của Node
        std::string stringId;
        // std::string type; // loại node
        double x; // Tọa độ X
        double y; // Tọa độ Y
        double readyTime; // Thời gian sẵn sàng
        double dueDate; // Thời gian kết thúc
        double serviceTime; // Thời gian phục vụ
};