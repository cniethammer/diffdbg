#!/usr/bin/env bats

bats_load_library 'bats-support'
bats_load_library 'bats-assert'
bats_load_library 'bats-file'

setup() {
    cd "$BATS_TEST_DIRNAME/.."
    rm -f ref.bin ref-2.bin
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
    assert_failure 133
}

@test "example-2 runs" {
    run $EXAMPLE_DIR/example-2
    assert_success
}

@test "example-2 creates reference file" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref-2.bin $EXAMPLE_DIR/example-2
    assert_exists ref-2.bin
}

@test "example-2 detects mismatch in check mode" {
    DIFFDBG_MODE=record DIFFDBG_FILE=ref-2.bin $EXAMPLE_DIR/example-2
    # The example intentionally introduces a mismatch in check mode
    run env DIFFDBG_MODE=check DIFFDBG_FILE=ref-2.bin $EXAMPLE_DIR/example-2
    # Expected to fail with exit code 133 (SIGTRAP from diffdbg)
    assert_failure 133
}

teardown() {
    rm -f ref.bin ref-2.bin
}
