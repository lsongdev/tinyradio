#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ats-mini-tests.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT
trap 'exit 1' HUP INT TERM
cd "$project_dir"
cc=${CC:-cc}
rds="src/drivers/si473x/rds.c src/drivers/si473x/enhanced_radiotext.c"
radio="src/radio/model.c src/radio/saved_settings.c"
clock="src/services/device_clock.c src/app/rds_clock.c"
run() {
  name=$1; shift
  "$cc" -std=c99 -Wall -Wextra -Werror -Itest/native/stubs -Isrc "$@" -o "$test_dir/$name"
  "$test_dir/$name"
}
run model test/native/model_test.c src/radio/model.c
run persistence test/native/persistence_test.c $radio
run rds test/native/rds_test.c $rds
run ert test/native/ert_test.c $rds
run clock test/native/clock_test.c $rds $clock
run driver test/native/driver_test.c src/drivers/si473x/si473x.c $rds $clock
run si5351 test/native/si5351_test.c src/drivers/si5351/si5351.c
run telemetry test/native/telemetry_test.c src/drivers/pcf8523/pcf8523.c src/drivers/sht3x/sht3x.c
run spectrum test/native/spectrum_test.c src/services/spectrum.c -lm
python3 -m unittest discover -s test -p 'test_*.py'
