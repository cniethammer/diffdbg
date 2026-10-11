// example-2-precision.cpp -- demo of diffdbg.hpp: precision bug detection with different strategies
//
// Build:
//   g++ -std=c++17 -O2 example-2-precision.cpp -o example-2-precision
//
// Reference run:
//   DIFFDBG_MODE=record DIFFDBG_FILE=ref.bin ./example-2-precision
//
// Comparison run:
//   DIFFDBG_MODE=check DIFFDBG_FILE=ref.bin ./example-2-precision

#include "diffdbg.hpp"
#include <vector>

int main() {
    diffdbg::init(diffdbg::modeFromEnv(), diffdbg::fileFromEnv(), diffdbg::strategyFromEnv());

    const size_t N = 100;
    std::vector<double> velocity(N), pressure(N), residual(N);

    for (int step = 0; step < 3; ++step) {
        DIFFDBG_SCOPE(step);

        for (size_t i = 0; i < N; ++i) {
            velocity[i] = std::sin(0.01 * i + step);
            pressure[i] = std::cos(0.01 * i + step);
        }
        DIFFDBG_CHECK_VEC(velocity, step);
        DIFFDBG_CHECK_VEC(pressure, step);

        for (int substep = 0; substep < 2; ++substep) {
            DIFFDBG_SCOPE(substep);

            for (size_t i = 0; i < N; ++i) residual[i] = velocity[i] - pressure[i];

            // Simulate a precision bug that only appears in check mode
            // The bug is at index 7, which is not the first element
            if (diffdbg::config().mode == diffdbg::Mode::Check && step == 1 && substep == 0) {
                residual[7] += 1e-3;
            }

            DIFFDBG_CHECK_VEC(residual);
        }
    }

    diffdbg::finalize();
    return 0;
}
