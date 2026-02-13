// app/AppController.h
#pragma once
#include "alns/ALNSSolver.h" // <-- Phải include
#include "core/Instance.h"   // <-- Phải include
#include <string>
#include <memory>

class AppController {
public:
    /**
     * @brief Constructor, nhận các đường dẫn chính.
     * Nó cũng sẽ tự động tải (parse) instance.
     */
    AppController(const std::string& instancePath,
                  const std::string& outputDir,
                  const std::string& runName);

    /**
     * @brief Hàm chính để chạy toàn bộ quá trình giải.
     */
    void run();

private:
    /**
     * @brief Hàm helper để tạo ALNSConfig.
     * Toàn bộ cấu hình hard-code nằm ở đây.
     */
    ALNSConfig createConfig();

    /**
     * @brief Hàm helper để đăng ký tất cả các toán tử.
     */
    void registerOperators(ALNSSolver& solver, const ALNSConfig& config);

    // Biến thành viên
    std::string instancePath;
    std::string outputDir;
    std::string runName;

    // Instance được tải và lưu trữ ở đây
    std::shared_ptr<Instance> instance;
};