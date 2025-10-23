#pragma once

class Node {
    public:
        Node(int id, double x, double y);
        virtual ~Node() = default;
        int getId() const;
        double getX() const;
        double getY() const;
        
    private:
        int id;
        double x;
        double y;
};