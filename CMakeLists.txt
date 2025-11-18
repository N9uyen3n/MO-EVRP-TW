# Yêu cầu phiên bản CMake (nâng lên 3.17 để hỗ trợ <filesystem> tốt hơn)
cmake_minimum_required(VERSION 3.17)

# Đặt tên dự án
project(test07)

# Yêu cầu tiêu chuẩn C++17 (Bắt buộc cho Logger)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# --- 1. Tạo biến chứa TẤT CẢ các file .cpp CHUNG ---
# (Đây là tất cả code lõi, KHÔNG BAO GỒM các file main)
set(COMMON_SRCS
        # Module: Core
        src/core/Customer.cpp
        src/core/Depot.cpp
        src/core/Instance.cpp
        src/core/Node.cpp
        src/core/Route.cpp
        src/core/Solution.cpp
        src/core/Station.cpp
        src/core/Vehicle.cpp
        src/core/Solver.cpp

        # Module: IO
        src/io/Parser.cpp

        # Module: Utils
        src/utils/Utils.cpp

        # Module: ALNS (Core)
        src/alns/ALNSSolver.cpp   # <-- Sửa lỗi chính tả từ ALNSSovler.cpp
        src/alns/LocalSearch.cpp
        src/alns/ParetoArchive.cpp
        # (LƯU Ý: IOperator.h là interface, không có IOperator.cpp)

        # Module: ALNS (Operators)
        src/alns/operators/destroy/RandomDestroy.cpp
        src/alns/operators/destroy/ShawDestroy.cpp
        src/alns/operators/destroy/WorstDestroy.cpp
        src/alns/operators/destroy/RouteRemoval.cpp
        src/alns/operators/repair/GreedyRepair.cpp
        src/alns/operators/repair/GreedyStationRepair.cpp
        src/alns/operators/repair/RegretKRepair.cpp

        # Module: Logger
        src/logger/ComprehensiveLogger.cpp
        include/app/AppController.h
        src/app/AppController.cpp
)

# --- 2. Tạo một THƯ VIỆN TĨNH (STATIC) từ code chung ---
# (Thư viện này chứa tất cả logic của bạn)
add_library(alns_lib STATIC ${COMMON_SRCS})

# --- 3. Chỉ định thư mục Include cho thư viện ---
# (Báo cho alns_lib biết nơi tìm các file .h)
target_include_directories(alns_lib PUBLIC include)


# (Nó sẽ dùng src/main.cpp của bạn)
add_executable(test06 src/main.cpp)

# Link test5 với thư viện chung
target_link_libraries(test06 PRIVATE alns_lib)


# (Nó sẽ dùng src/TestALNS.cpp mà bạn muốn thêm vào)
add_executable(TestALNS src/test/TestALNS.cpp)

# Link TestALNS với thư viện chung
target_link_libraries(TestALNS PRIVATE alns_lib)