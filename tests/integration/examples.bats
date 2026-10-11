#!/usr/bin/env bats

bats_load_library 'bats-support'
bats_load_library 'bats-assert'
bats_load_library 'bats-file'

setup() {
    cd "$BATS_TEST_DIRNAME/.."
    rm -f ref*.bin
}

@test "example runs" {
    run $EXAMPLE_DIR/example
    assert_success
}

@test "example creates reference file" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref.bin $EXAMPLE_DIR/example
    assert_exists ref.bin
}

@test "example detects mismatch in check mode" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref.bin $EXAMPLE_DIR/example
    # The example intentionally introduces a mismatch in check mode
    run env DIFFDBG_MODE=check DIFFDBG_FILE=ref.bin $EXAMPLE_DIR/example
    # Expected to fail with exit code 133 (SIGTRAP from diffdbg)
    assert_output --partial "MISMATCH"
    assert_output --partial "label='velocity'"
    assert_output --partial "idx=(3)"
    assert_output --partial "index=42"
    assert_failure 133
}

@test "scoped runs" {
    run $EXAMPLE_DIR/scoped
    assert_success
}

@test "scoped creates reference file" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref-scoped.bin $EXAMPLE_DIR/scoped
    assert_exists ref-scoped.bin
}

@test "scoped detects mismatch in check mode with strategy=Full" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref-scoped.bin DIFFDBG_STRATEGY=Full $EXAMPLE_DIR/scoped
    assert_exists ref-scoped.bin
    run env DIFFDBG_MODE=check DIFFDBG_FILE=ref-scoped.bin DIFFDBG_STRATEGY=Full $EXAMPLE_DIR/scoped
    assert_output --partial "MISMATCH"
    assert_output --partial "label='residual'"
    assert_output --partial "idx=(2,1)"
    assert_output --partial "index=7"
    # Expected to fail with exit code 133 (SIGTRAP from diffdbg)
    assert_failure 133
}

@test "scoped does not detect mismatch in check mode with strategy=First" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref-scoped.bin DIFFDBG_STRATEGY=First $EXAMPLE_DIR/scoped
    assert_exists ref-scoped.bin
    run env DIFFDBG_MODE=check DIFFDBG_FILE=ref-scoped.bin DIFFDBG_STRATEGY=First $EXAMPLE_DIR/scoped

    # Faulty value at index 7 is not detected with strategy=First, so the test should pass
    assert_success
}

@test "precision with Full strategy detects mismatch" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref-prec-full.bin DIFFDBG_STRATEGY=Full $EXAMPLE_DIR/precision
    assert_exists ref-prec-full.bin
    run env DIFFDBG_MODE=check DIFFDBG_FILE=ref-prec-full.bin DIFFDBG_STRATEGY=Full $EXAMPLE_DIR/precision
    # Expected to fail with exit code 133 (SIGTRAP from diffdbg)
    assert_failure 133
}

@test "precision with Hash strategy detects mismatch" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref-prec-hash.bin DIFFDBG_STRATEGY=Hash $EXAMPLE_DIR/precision
    assert_exists ref-prec-hash.bin
    run env DIFFDBG_MODE=check DIFFDBG_FILE=ref-prec-hash.bin DIFFDBG_STRATEGY=Hash $EXAMPLE_DIR/precision
    # Expected to fail with exit code 133 (SIGTRAP from diffdbg)
    assert_failure 133
}

@test "precision with Stats strategy detects mismatch" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref-prec-stats.bin DIFFDBG_STRATEGY=Stats $EXAMPLE_DIR/precision
    assert_exists ref-prec-stats.bin
    run env DIFFDBG_MODE=check DIFFDBG_FILE=ref-prec-stats.bin DIFFDBG_STRATEGY=Stats $EXAMPLE_DIR/precision
    # Expected to fail with exit code 133 (SIGTRAP from diffdbg)
    assert_failure 133
}

@test "example with unknown strategy reports warning" {
    run env DIFFDBG_MODE=record DIFFDBG_FILE=ref-warn.bin DIFFDBG_STRATEGY=UnknownStrategy $EXAMPLE_DIR/example
    assert_success
    assert_output --partial "WARNING: unknown DIFFDBG_STRATEGY='UnknownStrategy', using 'full'"
}

@test "strategy mismatch between record and check warns" {
    # Record with Full strategy
    DIFFDBG_MODE=record DIFFDBG_FILE=ref-mismatch.bin DIFFDBG_STRATEGY=Full $EXAMPLE_DIR/example
    assert_exists ref-mismatch.bin

    # Check with First strategy - should warn about mismatch
    run env DIFFDBG_MODE=check DIFFDBG_FILE=ref-mismatch.bin DIFFDBG_STRATEGY=First $EXAMPLE_DIR/example
    assert_output --partial "WARNING: reference was recorded with strategy='full' overriding DIFFDBG_STRATEGY='first' with 'full'"
    # Should still run and detect mismatch
    assert_output --partial "MISMATCH"
    assert_failure 133
}

@test "example-mpi runs with 2 processes" {
    run mpirun -n 2 $EXAMPLE_DIR/example-mpi
    assert_success
}

@test "example-mpi creates reference files with 2 processes" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref-mpi.bin mpirun -n 2 $EXAMPLE_DIR/example-mpi
    # MPI creates separate files per rank
    assert_exists ref-mpi.rank0.bin
    assert_exists ref-mpi.rank1.bin
}

@test "example-mpi detects mismatch in check mode with 2 processes" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref-mpi.bin mpirun -n 2 $EXAMPLE_DIR/example-mpi
    assert_exists ref-mpi.rank0.bin
    assert_exists ref-mpi.rank1.bin
    # The bug only appears on rank 1, step 3
    run env DIFFDBG_MODE=check DIFFDBG_FILE=ref-mpi.bin mpirun -n 2 $EXAMPLE_DIR/example-mpi
    assert_output --partial "[diffdbg r1] MISMATCH"
    assert_output --partial "label='velocity'"
    assert_output --partial "idx=(3)"
    assert_output --partial "index=42"
    assert_failure 133
}

teardown() {
    rm -f ref*.bin
}
