#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <filesystem> // For path manipulation and directory creation
#include <fstream>   // For file output

#include "../include/Parser.h"
#include "../include/Instance.h"
#include "../include/Utils.h"
#include "../include/ALNS.h"
#include "../include/objectives/ObjectiveFunction.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <instance_file>" << std::endl;
        return 1;
    }

    std::string instance_file = argv[1];

    try {
        std::cout << "Parsing instance file: " << instance_file << std::endl;
        auto instance = Parser::parse(instance_file); // Parser now returns a shared_ptr
        
        Utils::printInstance(*instance);

        // --- Create output directory and file ---
        std::filesystem::path instance_path(instance_file);
        std::string instance_filename = instance_path.filename().string();

        std::string results_dir = "results/" + instance_filename;
        std::filesystem::create_directories(results_dir);

        std::string output_filepath = results_dir + "/results.txt";
        std::ofstream output_file(output_filepath);

        std::cout << "\nSaving detailed results to: " << output_filepath << std::endl;
        // --- End file setup ---

        ObjectiveManager objManager;
        objManager.addObjective(std::make_shared<NumberOfVehiclesObjective>());
        objManager.addObjective(std::make_shared<TotalDistanceObjective>());
        // objManager.addObjective(std::make_shared<TotalEnergyObjective>());
        // objManager.addObjective(std::make_shared<MakespanObjective>());

        ALNS alns_solver(objManager, instance);

        std::vector<Solution> pareto_front = alns_solver.solve();

        std::cout << "Final Pareto Front contains " << pareto_front.size() << " non-dominated solutions." << std::endl;
        output_file << "Final Pareto Front contains " << pareto_front.size() << " non-dominated solutions." << std::endl;

        // --- BEGIN: Modified code to print routes to file ---
        output_file << "\n=============== DETAILED SOLUTIONS ===============\n";
        int sol_count = 1;
        for (const auto& solution : pareto_front) {
            output_file << "\n--- Solution " << sol_count++ << " ---\n";
            output_file << "  Objectives: [";
            for (size_t i = 0; i < solution.objectives.size(); ++i) {
                output_file << solution.objectives[i] << (i == solution.objectives.size() - 1 ? "" : ", ");
            }
            output_file << "]" << std::endl;

            output_file << solution.toString();
        }
        output_file << "\n==================================================\n";
        // --- END: Modified code to print routes to file ---

    } catch (const std::exception& e) {
        std::cerr << "An exception occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
