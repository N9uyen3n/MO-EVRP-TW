// include/logging/ILogger.h
#pragma once
#include <string>

// Forward declarations (để không cần include)
class ParetoArchive;
class Solution;
struct OperatorPool;
struct ALNSConfig;

namespace logging {

    class ILogger {
    public:
        virtual ~ILogger() = default; // Destructor ảo

        // --- Các hàm bắt buộc ---
        virtual void logConfig(const ALNSConfig& config) = 0;

        virtual void logProgress(int iteration, long long time_ms,
                                 const ParetoArchive& archive) = 0;

        virtual void logOperatorSegment(int segment,
                                        const OperatorPool& destroyPool,
                                        const OperatorPool& repairPool) = 0;

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