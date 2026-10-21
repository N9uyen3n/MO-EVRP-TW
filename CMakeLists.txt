# Yêu cầu phiên bản CMake
cmake_minimum_required(VERSION 3.17)

# Đặt tên dự án
project(untitled LANGUAGES CXX)

add_executable(RunJD src/test/RunJD.cpp)
target_link_libraries(RunJD PRIVATE alns_lib)


# Yêu cầu tiêu chuẩn C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Cấu hình cờ biên dịch (Release mode tối ưu hóa)
# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ot /GL /arch:AVX2")

else()

    set(CMAKE_CXX_FLAGS_RELEASE
            "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -mtune=native -funroll-loops -flto=auto -DNDEBUG -fomit-frame-pointer")

endif()

# -------------------------------------------------------------
# 1. Tự động tìm tất cả các file nguồn (.cpp) trong thư mục src
#    (Trừ main.cpp và các file test để tránh xung đột hàm main)
# -------------------------------------------------------------
file(GLOB_RECURSE COMMON_SRCS
        "src/core/*.cpp"
        "src/alns/*.cpp"
        "src/alns/operators/*.cpp"
        "src/alns/operators/destroy/*.cpp"
        "src/alns/operators/repair/*.cpp"
        "src/alns/operators/local_search/*.cpp"
        "src/io/*.cpp"
        "src/utils/*.cpp"
        "src/logger/*.cpp"
        "src/app/*.cpp"
)

# Loại bỏ các file chứa hàm main() khỏi danh sách thư viện chung
list(REMOVE_ITEM COMMON_SRCS
        "${CMAKE_SOURCE_DIR}/src/main.cpp"
        "${CMAKE_SOURCE_DIR}/src/test/TestALNS.cpp"
)

# -------------------------------------------------------------
# 2. Tạo thư viện tĩnh (Static Library) từ các file nguồn chung
# -------------------------------------------------------------
add_library(alns_lib STATIC ${COMMON_SRCS})

# Thêm thư mục include cho thư viện và cho bất kỳ target nào liên kết với nó
target_include_directories(alns_lib PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
)

# P6: conservative O(1) insertion time-window rejection; OFF keeps the reference path.
option(ALNS_INSERTION_TIME_BOUNDS "Use cached relaxed prefix/suffix time bounds" ON)
if(ALNS_INSERTION_TIME_BOUNDS)
    target_compile_definitions(alns_lib PUBLIC ALNS_INSERTION_TIME_BOUNDS=1)
endif()

# -------------------------------------------------------------
# 3. Tạo file thực thi chính (Executable)
# -------------------------------------------------------------
add_executable(untitled src/main.cpp)

# Liên kết thư viện alns_lib vào chương trình chính
target_link_libraries(untitled PRIVATE alns_lib)

# -------------------------------------------------------------
# 4. Tạo file thực thi kiểm thử (Test Executable) - Tùy chọn
# -------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/test/TestALNS.cpp")
    add_executable(TestALNS src/test/TestALNS.cpp)
    target_link_libraries(TestALNS PRIVATE alns_lib)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(RouteRegressionTests src/test/RouteRegressionTests.cpp)
    target_link_libraries(RouteRegressionTests PRIVATE alns_lib)
    add_test(NAME RouteRegressionTests COMMAND RouteRegressionTests)
    add_executable(LocalSearchRegressionTests src/test/LocalSearchRegressionTests.cpp)
    target_link_libraries(LocalSearchRegressionTests PRIVATE alns_lib)
    add_test(NAME LocalSearchRegressionTests COMMAND LocalSearchRegressionTests)
    add_executable(ChargingRepairTests src/test/ChargingRepairTests.cpp)
    target_link_libraries(ChargingRepairTests PRIVATE alns_lib)
    add_test(NAME ChargingRepairTests COMMAND ChargingRepairTests)
    add_executable(ReconstructionCacheTests src/test/ReconstructionCacheTests.cpp)
    target_link_libraries(ReconstructionCacheTests PRIVATE alns_lib)
    add_test(NAME ReconstructionCacheTests COMMAND ReconstructionCacheTests)
    add_executable(EjectionUndoTests src/test/EjectionUndoTests.cpp)
    target_link_libraries(EjectionUndoTests PRIVATE alns_lib)
    add_test(NAME EjectionUndoTests COMMAND EjectionUndoTests)
    add_executable(InsertionTimeBoundsTests src/test/InsertionTimeBoundsTests.cpp)
    target_link_libraries(InsertionTimeBoundsTests PRIVATE alns_lib)
    add_test(NAME InsertionTimeBoundsTests COMMAND InsertionTimeBoundsTests)
    add_executable(SearchContextTests src/test/SearchContextTests.cpp)
    target_link_libraries(SearchContextTests PRIVATE alns_lib)
    add_test(NAME SearchContextTests COMMAND SearchContextTests)
    add_executable(KNNBufferTests src/test/KNNBufferTests.cpp)
    target_link_libraries(KNNBufferTests PRIVATE alns_lib)
    add_test(NAME KNNBufferTests COMMAND KNNBufferTests)
    add_executable(OrOptCacheTests src/test/OrOptCacheTests.cpp)
    target_link_libraries(OrOptCacheTests PRIVATE alns_lib)
    add_test(NAME OrOptCacheTests COMMAND OrOptCacheTests)
    add_executable(ALNSAuditRunner src/test/ALNSAuditRunner.cpp)
    target_link_libraries(ALNSAuditRunner PRIVATE alns_lib)
endif()

# -------------------------------------------------------------
# 5. Tạo file thực thi Tuning (Tuning Executable)
# -------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/tuning/TuningApp.cpp")
    add_executable(TuningApp src/tuning/TuningApp.cpp)
    target_link_libraries(TuningApp PRIVATE alns_lib)
endif()

# -------------------------------------------------------------
# 6. Tạo file thực thi Benchmark Test2 (Benchmark Executable)
# -------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/test/BenchmarkTest2.cpp")
    add_executable(BenchmarkTest2 src/test/BenchmarkTest2.cpp)
    target_link_libraries(BenchmarkTest2 PRIVATE alns_lib)
endif()

# -------------------------------------------------------------
# 7. Tạo file thực thi Pareto Demo (Demo Executable)
# -------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/test/ParetoDemo.cpp")
    add_executable(ParetoDemo src/test/ParetoDemo.cpp)
    target_link_libraries(ParetoDemo PRIVATE alns_lib)
endif()

# -------------------------------------------------------------
# 8. Tạo file thực thi Benchmark EVRPTWMF
#    Chạy bộ dữ liệu data/instances_EVRPTWMF
# -------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/test/BenchmarkEVRPTWMF.cpp")
    add_executable(BenchmarkEVRPTWMF src/test/BenchmarkEVRPTWMF.cpp)
    target_link_libraries(BenchmarkEVRPTWMF PRIVATE alns_lib)
endif()
