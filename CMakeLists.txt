# Yêu cầu phiên bản CMake
cmake_minimum_required(VERSION 3.17)

# Đặt tên dự án
project(test13 LANGUAGES CXX)

# Yêu cầu tiêu chuẩn C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Cấu hình cờ biên dịch (Release mode tối ưu hóa)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# -------------------------------------------------------------
# 1. Tự động tìm tất cả các file nguồn (.cpp) trong thư mục src
#    (Trừ main.cpp và các file test để tránh xung đột hàm main)
# -------------------------------------------------------------
file(GLOB_RECURSE COMMON_SRCS
        "src/core/*.cpp"
        "src/alns/*.cpp"
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

# -------------------------------------------------------------
# 3. Tạo file thực thi chính (Executable)
# -------------------------------------------------------------
add_executable(test13 src/main.cpp)

# Liên kết thư viện alns_lib vào chương trình chính
target_link_libraries(test13 PRIVATE alns_lib)

# -------------------------------------------------------------
# 4. Tạo file thực thi kiểm thử (Test Executable) - Tùy chọn
# -------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/test/TestALNS.cpp")
    add_executable(TestALNS src/test/TestALNS.cpp)
    target_link_libraries(TestALNS PRIVATE alns_lib)
endif()

# -------------------------------------------------------------
# 5. Tạo file thực thi Tuning (Tuning Executable)
# -------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/tuning/TuningApp.cpp")
    add_executable(TuningApp src/tuning/TuningApp.cpp)
    target_link_libraries(TuningApp PRIVATE alns_lib)
endif()