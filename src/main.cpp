#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <filesystem>
#include <fstream>
#include <random>     // <-- THÊM VÀO
#include <chrono>     // <-- THÊM VÀO (để seed RNG)

#include "../include/io/Parser.h"
#include "../include/core/Instance.h"
#include "../include/utils/Utils.h"

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


    } catch (const std::exception& e) {
        std::cerr << "An exception occurred: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}