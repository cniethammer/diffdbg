// example.cpp -- demo of diffdbg.hpp: flat loop, multi-index, and scoped context
//
// Build:
//   g++ -std=c++17 -O2 example.cpp -o example
//
// Reference run:
//   DIFFDBG_MODE=record DIFFDBG_FILE=ref.bin ./example
//
// Comparison run:
//   DIFFDBG_MODE=check DIFFDBG_FILE=ref.bin ./example

#include "diffdbg.hpp"
#include <vector>

// Pretend this is a solver kernel buried several calls deep -- it has no
// idea what step/substep it's being called from, yet the check below is
// still tagged with the full (step, substep) context automatically.
void solveInnerKernel(std::vector<double>& residual) {
    for (size_t i = 0; i < residual.size(); ++i) residual[i] *= 0.999;

    // Simulate a bug that only appears in the "check" run, at one specific
    // (step, substep) pair, to demonstrate scoped-context tagging:
    if (diffdbg::config().mode == diffdbg::Mode::Check) residual[7] += 1e-3;

    DIFFDBG_CHECK_VEC(residual); // tagged with whatever's on the scope stack
}

int main() {
    diffdbg::init(diffdbg::modeFromEnv(), diffdbg::fileFromEnv(), diffdbg::strategyFromEnv());

    const size_t N = 100;
    std::vector<double> velocity(N), pressure(N), residual(N);

    for (int step = 0; step < 3; ++step) {
        DIFFDBG_SCOPE(step); // pushes `step` onto the scoped-context stack

        for (size_t i = 0; i < N; ++i) {
            velocity[i] = std::sin(0.01 * i + step);
            pressure[i] = std::cos(0.01 * i + step);
        }
        // flat-loop style: explicit single index
        DIFFDBG_CHECK_VEC(velocity, step);
        DIFFDBG_CHECK_VEC(pressure, step);

        for (int substep = 0; substep < 2; ++substep) {
            DIFFDBG_SCOPE(substep); // pushes `substep` too -> context is (step, substep)

            for (size_t i = 0; i < N; ++i) residual[i] = velocity[i] - pressure[i];
            solveInnerKernel(residual); // check happens inside, tagged (step, substep)
        }
        // back here, only `step` is on the stack again (substep scope ended)
    }

    diffdbg::finalize();
    return 0;
}
