#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ats-mini-tests.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT
trap 'exit 1' HUP INT TERM
cd "$project_dir"
cxx=${CXX:-c++}
"$cxx" -std=c++11 -Wall -Wextra -Werror -Isrc test/native/model_test.cpp src/radio/model.cpp -o "$test_dir/model"
"$test_dir/model"
"$cxx" -std=c++11 -Wall -Wextra -Werror -Isrc test/native/rds_test.cpp -o "$test_dir/rds"
"$cxx" -std=c++11 -Wall -Wextra -Werror -Isrc test/native/ert_test.cpp -o "$test_dir/ert"
"$test_dir/rds"
"$test_dir/ert"
"$cxx" -std=c++11 -Wall -Wextra -Werror -Itest/native/stubs -Isrc test/native/driver_test.cpp src/drivers/si473x/si473x.cpp -o "$test_dir/driver"
"$test_dir/driver"
"$cxx" -std=c++11 -Wall -Wextra -Werror -Isrc test/native/persistence_test.cpp src/radio/model.cpp src/radio/saved_settings.cpp -o "$test_dir/persistence"
"$test_dir/persistence"
python3 -m unittest discover -s test -p 'test_*.py'
