// include/logging/NullLogger.h
#pragma once
#include "logger/ILogger.h"

namespace logging {

    class NullLogger : public ILogger {
    public:
        ~NullLogger() override = default;

        // Triển khai TẤT CẢ các hàm trong giao diện,
        // nhưng để chúng RỖNG (không làm gì cả).

        void logConfig(const alns::ALNSConfig& config) override {}

        void logProgress(int iteration, long long time_ms,
                         const ParetoArchive& archive) override {}

        void logOperatorSegment(int segment,
                                const alns::OperatorPool& destroyPool,
                                const alns::OperatorPool& repairPool) override {}

        void logEvolutionStep(
            int iteration,
            const std::string& destroyOp,
            const std::string& repairOp,
            const std::string& result,
            const Solution& newSolution) override {}

        void logFinalFront(const ParetoArchive& archive) override {}

        void logSummary(long long total_ms, int total_iter,
                        int final_archive_size) override {}
    };

} // namespace logging