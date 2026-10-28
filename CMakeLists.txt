cmake_minimum_required(VERSION 3.17)
project(evrptw_pr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ot /GL /arch:AVX2")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -mtune=native -funroll-loops -flto=auto -DNDEBUG -fomit-frame-pointer")
endif()

# Explicit dependencies: local legacy files cannot enter the build accidentally.
add_library(alns_lib STATIC
    src/alns/ALNSSolver.cpp
    src/alns/ChargingRepair.cpp
    src/alns/LocalSearch.cpp
    src/alns/ParetoArchive.cpp
    src/alns/ScatterSearch.cpp
    src/alns/SolutionPool.cpp
    src/alns/operators/destroy/EnergyCriticalRemoval.cpp
    src/alns/operators/destroy/RandomRemoval.cpp
    src/alns/operators/destroy/RouteMergingDestroy.cpp
    src/alns/operators/destroy/TimeSlackDestroy.cpp
    src/alns/operators/destroy/UnifiedCostDestroy.cpp
    src/alns/operators/destroy/VehicleReductionAwareDestroy.cpp
    src/alns/operators/repair/AdaptiveInsertion.cpp
    src/alns/operators/repair/ChargingAwareRouteBuilder.cpp
    src/alns/operators/repair/GreedyEnergyInsertion.cpp
    src/alns/operators/repair/RegretKRepair.cpp
    src/alns/operators/repair/SmartStationRepair.cpp
    src/alns/operators/repair/VehiclePackingRepair.cpp
    src/core/Customer.cpp
    src/core/Depot.cpp
    src/core/Instance.cpp
    src/core/Node.cpp
    src/core/Route.cpp
    src/core/Solution.cpp
    src/core/Solver.cpp
    src/core/Station.cpp
    src/core/Vehicle.cpp
    src/io/Parser.cpp
    src/logger/ComprehensiveLogger.cpp
)
target_include_directories(alns_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
add_executable(evrptw src/app/ExperimentRunner.cpp)
target_link_libraries(evrptw PRIVATE alns_lib)

# Branch policy; configure again after switching branches.
set(ALNS_NO_EQUITY OFF CACHE BOOL "Branch policy: FP-PALNS-NoEQ (Gini remains reported)" FORCE)
if(ALNS_NO_EQUITY)
    target_compile_definitions(alns_lib PUBLIC ALNS_NO_EQUITY=1)
else()
    target_compile_definitions(alns_lib PUBLIC ALNS_NO_EQUITY=0)
endif()
option(ALNS_INSERTION_TIME_BOUNDS "Use cached relaxed prefix/suffix time bounds" ON)
if(ALNS_INSERTION_TIME_BOUNDS)
    target_compile_definitions(alns_lib PUBLIC ALNS_INSERTION_TIME_BOUNDS=1)
endif()

option(BUILD_TESTING "Build solver regression tests" OFF)
include(CTest)
if(BUILD_TESTING)
    foreach(test_name IN ITEMS
        ChargingRepairTests
        DestroyOperatorRegressionTests
        EjectionUndoTests
        InsertionTimeBoundsTests
        KNNBufferTests
        LocalSearchRegressionTests
        OrOptCacheTests
        ReconstructionCacheTests
        RouteRegressionTests
        SearchContextTests
        SearchObjectivesTests
    )
        add_executable(${test_name} src/test/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE alns_lib)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()
