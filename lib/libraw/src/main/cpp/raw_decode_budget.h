// Spektrafilm for Android — device-aware RAW decode admission. GPLv3.
#ifndef SPECTRAFILM_RAW_DECODE_BUDGET_H
#define SPECTRAFILM_RAW_DECODE_BUDGET_H

#include <cstdint>

namespace spectrafilm {

// Peak decode memory includes LibRaw's unpacked image, processed bitmap,
// make_mem_image() copy, and the returned float RGB buffer. The raw-store
// limit alone cannot bound those allocations. Keep a conservative ceiling on
// small devices and use total/available RAM as a heuristic for larger decodes.
// This snapshot is not a memory reservation or a guarantee of export success.
constexpr std::uint64_t decodePixelLimitForMemory(
    bool is64Bit, std::uint64_t totalBytes,
    std::uint64_t availableBytes) noexcept {
    constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
    constexpr std::uint64_t kGiB = 1024ULL * kMiB;
    if (!is64Bit) return 8ULL * kMiB;
    // Missing or inconsistent telemetry must not raise the fixed baseline.
    if (totalBytes == 0 || availableBytes > totalBytes) return 12ULL * kMiB;
    if (totalBytes >= 8ULL * kGiB &&
        availableBytes >= 3ULL * kGiB) {
        return 32ULL * kMiB;
    }
    if (totalBytes >= 6ULL * kGiB &&
        availableBytes >= 2ULL * kGiB) {
        return 24ULL * kMiB;
    }
    return 12ULL * kMiB;
}

}  // namespace spectrafilm

#endif
