#!/usr/bin/env bash
# Runs the parser on every sample and compares its output with the recorded expectation.
#   tests/run_tests.sh [path-to-cpp_parser]
set -u
cd "$(dirname "$0")/.."
BIN="${1:-build/cpp_parser}"
pass=0
fail=0

check() {  # check <source-file> <expected-file>
    local actual
    actual="$("$BIN" "$1")"
    if [ "$actual" == "$(cat "$2")" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: $1"
        diff <(echo "$actual") "$2" | sed 's/^/    /'
    fi
}

check samples/valid.cpp tests/expected/valid.txt
check samples/errors.cpp tests/expected/errors.txt
for f in tests/cases/*.cpp; do
    check "$f" "tests/expected/$(basename "$f" .cpp).txt"
done

echo "passed: $pass  failed: $fail"
[ "$fail" -eq 0 ]
