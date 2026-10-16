// SPDX-FileCopyrightText: 2026 Spektrafilm Android contributors
// SPDX-License-Identifier: GPL-3.0-only
//
// Device gate for halation's three 128 MiB storage-descriptor windows. This is
// deliberately a separate, opt-in large test: it requires about 3 GiB peak
// process memory and a Vulkan device with enough available GPU memory.
// A no-device or allocation fallback is a failure, never a vacuous pass.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

#include "gpu/vulkan_compute.h"
#include "kernels/exponential_filter.h"
#include "runtime/memory_budget.h"

int main() {
    constexpr int w = 6064, h = 4040;
    constexpr size_t pixels = static_cast<size_t>(w) * h;
    constexpr size_t values = pixels * 3u;
    constexpr size_t segmentFloats = (128u << 20) / sizeof(float);
    constexpr double sigma = 5.0;  // IIR on both axes, across descriptor seams.
    constexpr double maxLimit = 1e-4, rmsLimit = 1e-5;
    try {
        std::vector<float> input(values);
        std::vector<double> cpu(values);
        std::vector<double> gpu(values, -1.0);

        // Smooth, nonconstant RGB field with structured high-frequency detail.
        // Its strictly positive floor makes log10(raw) comparison meaningful.
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t p = static_cast<size_t>(y) * w + x;
                for (int c = 0; c < 3; ++c) {
                    const float v = 0.06f + 0.000012f * static_cast<float>(x) +
                                    0.000017f * static_cast<float>(y) + 0.035f * c +
                                    0.001f * static_cast<float>((x * 7 + y * 13 + c * 19) % 17);
                    input[p * 3u + c] = v;
                }
            }
        }
        // Both 128 MiB boundaries split an RGB triple. Place distinct, colored
        // impulses on either side so a wrong descriptor offset is conspicuous.
        for (size_t boundary : {segmentFloats, 2u * segmentFloats}) {
            for (int delta = -18; delta <= 18; delta += 9) {
                const size_t p = boundary / 3u + static_cast<size_t>(delta);
                for (int c = 0; c < 3; ++c)
                    input[p * 3u + c] += static_cast<float>(4.0 + 2.0 * c + 0.1 * delta);
            }
        }
        // Real image edges exercise the filter's reflect initialization too.
        for (int y = 0; y < h; ++y) {
            const size_t left = static_cast<size_t>(y) * w * 3u;
            const size_t right = left + static_cast<size_t>(w - 1) * 3u;
            input[left] += 2.0f;
            input[right + 2u] += 3.0f;
        }
        for (size_t i = 0; i < values; ++i) cpu[i] = static_cast<double>(input[i]);

        const double sigmas[3] = {sigma, sigma, sigma};
        const double weights[3] = {1.0, 1.0, 1.0};
        spk::gpu::HalationScatterRequest request{};
        request.raw_rgb_f32 = input.data();
        request.width = w;
        request.height = h;
        request.pixel_size_um = 1.0;
        request.scatter_amount = 1.0;
        request.scatter_spatial_scale = 1.0;
        request.mixture_count = 1;
        request.mixture_sigma_px = sigmas;
        request.mixture_weight = weights;
        request.halation_n_bounces = 0;

        spk::gpu::HalationScatterDiagnostics diagnostic{};
        const bool ok = spk::gpu::halation_scatter(request, gpu.data(), &diagnostic);
        std::printf("large halation: ok=%d engaged=%d reason=%s bands=%u dispatches=%u "
                    "upload=%.1fms gpu=%.1fms readback=%.1fms\n",
                    ok ? 1 : 0, diagnostic.engaged ? 1 : 0, diagnostic.reason,
                    diagnostic.bands, diagnostic.dispatches, diagnostic.upload_ms,
                    diagnostic.gpu_ms, diagnostic.readback_ms);
        if (!ok || !diagnostic.engaged) {
            std::puts("test_large_frame_gpu: FAIL (GPU did not engage)");
            return 1;
        }
        const auto memory = spk::memory::process_memory_budget().snapshot();
        const auto device = memory.domains[static_cast<size_t>(spk::memory::MemoryDomain::GpuDevice)];
        std::printf("large scratch: GPU device current=%llu peak=%llu bytes\n",
                    static_cast<unsigned long long>(device.current_bytes),
                    static_cast<unsigned long long>(device.peak_bytes));
        if (device.peak_bytes < values * sizeof(float) * 4u ||
            device.current_bytes >= (128u << 20)) {
            std::puts("test_large_frame_gpu: FAIL (large scratch retained or counter unavailable)");
            return 1;
        }
        std::vector<float>().swap(input);  // release the 280 MiB upload plane.
        spk::gaussian_blur_per_channel_d(cpu.data(), w, h, 3, sigmas);

        double maxAbs = 0.0, sumSq = 0.0;
        double seamMax[2] = {0.0, 0.0};
        size_t seamSamples[2] = {0u, 0u};
        for (size_t i = 0; i < values; ++i) {
            if (!std::isfinite(gpu[i]) || !std::isfinite(cpu[i])) {
                std::printf("nonfinite value at float index %zu\n", i);
                return 1;
            }
            const double a = std::log10(std::fmax(cpu[i], 0.0) + 1e-10);
            const double b = std::log10(std::fmax(gpu[i], 0.0) + 1e-10);
            const double error = std::fabs(a - b);
            maxAbs = std::max(maxAbs, error);
            sumSq += error * error;
            for (int seam = 0; seam < 2; ++seam) {
                const size_t boundary = static_cast<size_t>(seam + 1) * segmentFloats;
                if (i >= boundary - 3u * w && i < boundary + 3u * w) {
                    seamMax[seam] = std::max(seamMax[seam], error);
                    ++seamSamples[seam];
                }
            }
        }
        const double rms = std::sqrt(sumSq / static_cast<double>(values));
        std::printf("large halation vs CPU: max_abs=%.3e rms=%.3e; "
                    "segment seams max_abs=%.3e/%zu %.3e/%zu\n",
                    maxAbs, rms, seamMax[0], seamSamples[0], seamMax[1], seamSamples[1]);
        const bool pass = maxAbs <= maxLimit && rms <= rmsLimit &&
                          seamSamples[0] > 0 && seamSamples[1] > 0 &&
                          seamMax[0] <= maxLimit && seamMax[1] <= maxLimit;
        std::puts(pass ? "test_large_frame_gpu: ALL OK" : "test_large_frame_gpu: FAIL");
        return pass ? 0 : 1;
    } catch (const std::exception& e) {
        std::printf("test_large_frame_gpu: FAIL (%s)\n", e.what());
        return 1;
    }
}
