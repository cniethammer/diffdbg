// example.cpp -- minimal demo of diffdbg.hpp
//
// Build:
//   g++ -std=c++17 -O2 example.cpp -o example
//
// Reference run:
//   DIFFDBG_MODE=record DIFFDBG_FILE=ref.bin ./example
//
// Comparison run (deliberately perturbed below to show a mismatch):
//   DIFFDBG_MODE=check DIFFDBG_FILE=ref.bin ./example
//   DIFFDBG_MODE=check DIFFDBG_FILE=ref.bin gdb ./example   # to catch the SIGTRAP

#include "diffdbg.hpp"
#include <vector>

#include <mpi.h>

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    diffdbg::init(diffdbg::modeFromEnv(), diffdbg::fileFromEnv(), diffdbg::strategyFromEnv());

    const size_t N = 1000;
    std::vector<double> velocity(N), pressure(N);

    for (int step = 0; step < 5; ++step) {
        for (size_t i = 0; i < N; ++i) {
            velocity[i] = std::sin(0.001 * i + step);
            pressure[i] = std::cos(0.001 * i + step);
        }

        // Simulate an "optimization" bug that only shows up in check mode
        // by uncommenting the next line:
        if (diffdbg::config().mode == diffdbg::Mode::Check && step == 3 && rank == 1) velocity[42] += 1e-3;

        DIFFDBG_CHECK_VEC(velocity, step);
        DIFFDBG_CHECK_VEC(pressure, step);
    }

    diffdbg::finalize();
    MPI_Finalize();
    return 0;
}
