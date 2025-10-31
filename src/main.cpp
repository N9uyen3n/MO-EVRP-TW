#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <filesystem>
#include <fstream>
#include <random>     // <-- THÊM VÀO
#include <chrono>     // <-- THÊM VÀO (để seed RNG)

#include "../include/Parser.h"
#include "../include/Instance.h"
#include "../include/Utils.h"
#include "../include/ALNS.h"       // <-- THÊM VÀO

int main(int argc, char* argv[]) {
    std::string instance_file;

    if (argc == 2) {
        instance_file = argv[1];
        std::cout << "Info: Using provided instance file: " << instance_file << std::endl;
    }
    else {
        // c101C5.txt là file 5 khách hàng, RẤT TỐT để test
        instance_file = "data/solomon/c101C5.txt";
        std::cout << "Info: No instance file provided. Using default: " << instance_file << std::endl;
    }

    try {
        // --- (Code debug của bạn) ---
        auto current_dir = std::filesystem::current_path();
        std::cout << "[DEBUG] Current Working Directory: " << current_dir.string() << std::endl;
        auto absolute_path = std::filesystem::absolute(instance_file);
        std::cout << "[DEBUG] Attempting to open absolute path: " << absolute_path.string() << std::endl;
        // --- (Kết thúc code debug) ---

        std::cout << "Parsing instance file: " << instance_file << std::endl;
        auto instance = Parser::parse(instance_file);

        Utils::printInstance(*instance);

        // ====================================================================
        // ⬇️ BẮT ĐẦU: CODE TEST ALNS ⬇️
        // ====================================================================

        // 1. Khởi tạo Bộ sinh số ngẫu nhiên (RNG)
        unsigned int seed = static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count());
        std::mt19937 rng(seed);
        std::cout << "\nInfo: RNG seeded with " << seed << std::endl;

        // 2. Khởi tạo ALNS bằng constructor ĐẦY ĐỦ
        //    (Các tham số đã được giảm xuống cho phù hợp với test nhanh)
        std::cout << "Initializing ALNS for small test..." << std::endl;
        auto alns_solver = std::make_shared<ALNS>(
            instance, rng,
            1000,
            0.95,
            0.05,
            33.0,
            20.0,
            13.0,
            0.1,
            50,
            100,
            100,
            50,
            20
        );

        // 3. Chạy Solver
        std::cout << "\n--- Starting ALNS Solver ---" << std::endl;
        std::vector<Solution> solutions = alns_solver->solve();
        std::cout << "--- ALNS Solver Finished ---" << std::endl;

        // 4. In kết quả
        if (solutions.empty()) {
            std::cout << "\n[RESULT] No solution found." << std::endl;
        } else {
            Solution best_solution = solutions[0];
            std::cout << "\n=== Best Solution Found ===" << std::endl;
            std::cout << "Feasible: " << (best_solution.isFeasible() ? "Yes" : "No") << std::endl;
            std::cout << "Total Vehicles: " << best_solution.getNumRoutes() << std::endl;
            std::cout << "Total Distance: " << best_solution.getTotalDistance() << std::endl;

            std::cout << "\nRoutes:" << std::endl;
            for (const auto& route : best_solution.getRoutes()) {
                if (route.getNodes().size() <= 2) continue; // Bỏ qua tuyến rỗng (D->D)

                std::cout << "  [Route " << route.getId() << "] ";
                const auto& nodes = route.getNodes();
                for (size_t i = 0; i < nodes.size(); ++i) {
                    std::cout << nodes[i];
                    if (i < nodes.size() - 1) std::cout << " -> ";
                }
                std::cout << " (Dist: " << route.getTotalDistance() << ")" << std::endl;
            }
        }

        // ====================================================================
        // ⬆️ KẾT THÚC: CODE TEST ALNS ⬆️
        // ====================================================================


    } catch (const std::exception& e) {
        std::cerr << "An exception occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}