# Tests

This directory contains the test suite for diffdbg.

## Unit Tests (Catch2)

Unit tests are located in `unit/` and use Catch2 for testing the core functionality.

### Building and running unit tests

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make diffdbg_tests

# Run tests from build directory
ctest

# Run tests from main directory
ctest --test-dir build

# Run with filters
ctest --test-dir build -R diffdbg_tests
ctest --test-dir build -R "[computeStats]"
```

### Prerequisites

Catch2 must be installed on your system. On Ubuntu:

```bash
sudo apt install libcatch2-dev
```

## Integration Tests (bats)

Integration tests are located in `integration/` and use bats (Bash Automated Testing System) to test the example programs.

### Building and running integration tests

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Run integration tests from build directory
ctest

# Run integration tests from main directory
ctest --test-dir build -R integration_tests
```

### Prerequisites

bats must be installed on your system. On Ubuntu:

```bash
sudo apt install bats
```

## CMake Options

| Option | Description | Default |
|--------|-------------|---------|
| `BUILD_UNIT_TESTS` | Build unit tests | ON |
| `BUILD_INTEGRATION_TESTS` | Build integration tests | ON |

To disable tests:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_UNIT_TESTS=OFF -DBUILD_INTEGRATION_TESTS=OFF
```
