#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
compiler=${CXX:-c++}
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/digifant-audio-core.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT

for test_source in "$root/tests/audio_block_pool_test.cpp" "$root/tests/audio_sampler_test.cpp"; do
    test_name=$(basename "$test_source" .cpp)
    "$compiler" -std=c++20 -Wall -Wextra -Wpedantic -Werror -pthread \
        "$test_source" -o "$build_dir/$test_name"
    "$build_dir/$test_name"
done

echo "Audio core checks: PASS"
