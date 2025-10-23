cmake_minimum_required(VERSION 3.10)

project(MO_EVRP_TW LANGUAGES CXX)

# Set C++ standard to C++17 for modern features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# No external packages are required anymore
# find_package(nlohmann_json 3.2.0 REQUIRED)
# find_package(spdlog REQUIRED)

# Define sources for the main executable
set(MAIN_SOURCES
    src/main.cpp
    src/Customer.cpp
    src/Depot.cpp
    src/Instance.cpp
    src/Node.cpp
    src/Parser.cpp
    src/Route.cpp
    src/Station.cpp
    src/Utils.cpp
    src/Vehicle.cpp
    src/ALNS.cpp
    src/operators/DestroyOperators.cpp
    src/operators/RepairOperators.cpp
)

# Add the main executable
add_executable(mo_evrp_tw ${MAIN_SOURCES})

# Add the include directory
target_include_directories(mo_evrp_tw PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# No external libraries to link anymore
# target_link_libraries(mo_evrp_tw PRIVATE 
#     nlohmann_json::nlohmann_json
#     spdlog::spdlog
# )