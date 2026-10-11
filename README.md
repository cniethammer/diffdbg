# diffdbg

A header-only differential debugging framework for C++ that helps detect numerical differences between runs (e.g., after code optimizations or compiler changes).

## Features

- **Zero-dependency**: Single header file, C++17 compatible
- **Multiple validation strategies**: Full, First, Hash, and Stats
- **MPI support**: Works with any launcher (no MPI headers required)
- **Flexible context tracking**: Thread indices through loops or use RAII scopes
- **Debugger integration**: Traps on mismatch for interactive debugging
- **Tolerance-based comparison**: Configurable relative/absolute tolerances
- **Comprehensive tests**: Unit tests with Catch2 for validation strategies

## Quick Start

### 1. Integrate into your code

```cpp
#include "diffdbg.hpp"

int main(int argc, char** argv) {
    diffdbg::init(diffdbg::modeFromEnv(), diffdbg::fileFromEnv(), diffdbg::strategyFromEnv());
    
    // Your code here
    for (int step = 0; step < nSteps; ++step) {
        // ... compute something ...
        DIFFDBG_CHECK_VEC(velocity, step);
        DIFFDBG_CHECK_VEC(pressure, step);
    }
    
    diffdbg::finalize();
    return 0;
}
```

### 2. Record a reference run

```bash
DIFFDBG_MODE=record DIFFDBG_STRATEGY=full DIFFDBG_FILE=ref.bin ./your_app
```

This creates `ref.bin` containing the reference data.

### 3. Check against reference

```bash
DIFFDBG_MODE=check DIFFDBG_FILE=ref.bin ./your_app
```

If any value differs, the program prints the location and traps into the debugger (SIGTRAP).

## Validation Strategies

| Strategy | Description | Best For |
|----------|-------------|----------|
| `full` (default) | Element-wise comparison with tolerance | Floating-point data, pinpointing bad elements |
| `first` | Only compare first element | Quick checks, tiny reference files |
| `hash` | 64-bit FNV-1a hash of raw bytes | Integer data, bitwise reproducibility |
| `stats` | min/max/mean comparison | Noisy floating-point, robust to rounding |

## Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `DIFFDBG_MODE` | `off\|record\|check` | `off` |
| `DIFFDBG_FILE` | Path to reference file | `diffdbg_reference.bin` |
| `DIFFDBG_STRATEGY` | `full\|first\|hash\|stats` | `full` |
| `DIFFDBG_RANK` | Override process rank | Auto-detected |
| `DIFFDBG_ONLY_RANK` | Only this rank runs diffdbg | All ranks |

## Multi-dimensional Context

For nested loops or multi-dimensional indices:

```cpp
DIFFDBG_CHECK_VEC(velocity, step, substep);  // Multi-dimensional key
```

Or using scoped context:

```cpp
for (int step = 0; step < nSteps; ++step) {
    DIFFDBG_SCOPE(step);
    for (int substep = 0; substep < nSub; ++substep) {
        DIFFDBG_SCOPE(substep);
        solveInnerKernel(...);  // Checks automatically tagged
    }
}
```

## MPI / Multi-Process

Works with any launcher without MPI dependencies:

```bash
# Auto-detects rank from environment
DIFFDBG_MODE=record DIFFDBG_FILE=ref.bin mpirun -n 4 ./app
DIFFDBG_MODE=check  DIFFDBG_FILE=ref.bin mpirun -n 4 ./app
```

Or explicitly set rank:

```cpp
int rank;
MPI_Comm_rank(MPI_COMM_WORLD, &rank);
diffdbg::setRank(rank);  // Call BEFORE init()
diffdbg::init(...);
```

## Building Examples

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make

# Run example
DIFFDBG_MODE=record DIFFDBG_FILE=ref.bin ./examples/example
DIFFDBG_MODE=check DIFFDBG_FILE=ref.bin ./examples/example
```

### CMake Options

| Option | Description | Default |
|--------|-------------|---------|
| `DIFFDBG_BUILD_EXAMPLES` | Build example programs | ON |

To disable examples:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DDIFFDBG_BUILD_EXAMPLES=OFF
```

## API Reference

### Initialization

```cpp
void diffdbg::init(Mode mode, const std::string& file, Strategy strategy);
void diffdbg::finalize();
```

### Checking Values

```cpp
DIFFDBG_CHECK_VEC(vector)           // Check std::vector
DIFFDBG_CHECK_ARR(ptr, count)       // Check raw array
DIFFDBG_CHECK_SCALAR(value)         // Check scalar value

// With explicit indices
DIFFDBG_CHECK_VEC(vec, i, j, k)
```

### Scoped Context

```cpp
DIFFDBG_SCOPE(value);  // RAII context marker
```

### Configuration

```cpp
diffdbg::Config& diffdbg::config();  // Access configuration
```

Configuration fields:
- `mode`: `Mode::Off`, `Mode::Record`, or `Mode::Check`
- `strategy`: Validation strategy
- `rank`: Process rank (-1 for single process)
- `referenceFile`: Path to reference file
- `relTol`: Relative tolerance (default: 1e-9)
- `absTol`: Absolute tolerance (default: 1e-12)
- `breakOnMismatch`: Trap into debugger (default: true)
- `stopAtFirstElement`: Stop scanning on first mismatch (default: true)
- `verboseOk`: Log successful checks (default: false)

## License

See [LICENSE](LICENSE) file.

## Acknowledgements

Portions of this software were developed with the assistance of AI language models.
