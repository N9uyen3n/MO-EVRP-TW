# Yêu cầu phiên bản CMake (3.10 là đủ)
cmake_minimum_required(VERSION 3.10)

# Đặt tên dự án
project(test5)

# Yêu cầu tiêu chuẩn C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Đây là phần bạn quan tâm ---

# 1. Dùng 'set' để tạo một biến tên là SOURCES
#    Biến này chứa TẤT CẢ các tệp .cpp cần thiết để xây dựng dự án
set(SOURCES src/main.cpp
        src/Customer.cpp
        src/Depot.cpp
        src/Instance.cpp
        src/Node.cpp
        src/Parser.cpp
        src/Route.cpp
        src/Solution.cpp
        src/Station.cpp
        src/Utils.cpp
        src/Vehicle.cpp
        src/ALNS.cpp
        src/RepairOperator.cpp
        src/DestroyOperator.cpp
        src/Solver.cpp
)

# 2. Dùng 'add_executable' để tạo tệp thực thi tên là 'test5'
#    Chúng ta dùng biến ${SOURCES} đã định nghĩa ở trên
add_executable(test5 ${SOURCES})

# 3. Báo cho trình biên dịch biết thư mục 'include' là nơi chứa các tệp .h
#    Đây là bước QUAN TRỌNG NHẤT
target_include_directories(test5 PUBLIC include)