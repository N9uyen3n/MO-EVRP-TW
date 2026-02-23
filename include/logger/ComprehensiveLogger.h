// include/logging/ComprehensiveLogger.h
#pragma once
#include "ILogger.h" // <-- BAO GỒM GIAO DIỆN
#include <fstream>
#include <string>
#include <memory>

// (Không cần forward declarations nữa vì ILogger đã có)

namespace logging {

    // KẾ THỪA TỪ ILOGGER
    class ComprehensiveLogger : public ILogger {
    public:
        ComprehensiveLogger(const std::string& outputDirectory,
                            const std::string& runName);
        ~ComprehensiveLogger() override;

        // Thêm 'override' để chắc chắn chúng ta tuân thủ giao diện
        void logConfig(const alns::ALNSConfig& config) override;

        void logProgress(int iteration, long long time_ms,
                         const ParetoArchive& archive) override;

        void logOperatorSegment(int segment,
                                const alns::OperatorPool& destroyPool,
                                const alns::OperatorPool& repairPool) override;

        void logEvolutionStep(
            int iteration,
            const std::string& destroyOp,
            const std::string& repairOp,
            const std::string& result,
            const Solution& newSolution) override;

        void logFinalFront(const ParetoArchive& archive) override;

        void logSummary(long long total_ms, int total_iter,
                        int final_archive_size) override;

    private:
        // (Toàn bộ 7 file stream và các hàm init... giữ nguyên như
        // file Logger.h đầy đủ mà tôi đã viết cho bạn)

        void initConfigFile();
        void initProgressFile();
        void initOperatorFile();
        void initEvolutionLogFile();
        void initFrontObjectiveFile();
        void initFrontDetailFile();
        void initSummaryFile();

        std::string filePrefix;

        std::ofstream configFile;
        std::ofstream progressFile;
        std::ofstream operatorFile;
        std::ofstream evolutionLogFile;
        std::ofstream frontObjFile;
        std::ofstream frontDetailFile;
        std::ofstream summaryFile;
    };

} // namespace logging