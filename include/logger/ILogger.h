// include/logging/ILogger.h
#pragma once
#include <string>

// Forward declarations (để không cần include)
class ParetoArchive;
class Solution;

// Khai báo trước các struct trong namespace 'alns'
namespace alns {
    struct OperatorPool;
    struct ALNSConfig;
}

namespace logging {

    class ILogger {
    public:
        virtual ~ILogger() = default; // Destructor ảo

        // --- Các hàm bắt buộc ---
        // Sử dụng tên đầy đủ với namespace
        virtual void logConfig(const alns::ALNSConfig& config) = 0;

        virtual void logProgress(int iteration, long long time_ms,
                                 const ParetoArchive& archive) = 0;

        virtual void logOperatorSegment(int segment,
                                        const alns::OperatorPool& destroyPool,
                                        const alns::OperatorPool& repairPool) = 0;

        virtual void logEvolutionStep(
            int iteration,
            const std::string& destroyOp,
            const std::string& repairOp,
            const std::string& result,
            const Solution& newSolution) = 0;

        virtual void logFinalFront(const ParetoArchive& archive) = 0;

        virtual void logSummary(long long total_ms, int total_iter,
                                int final_archive_size) = 0;
    };

} // namespace logging