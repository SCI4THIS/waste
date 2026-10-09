#!/bin/bash
# Run every browser-context WAST fixture with the selected WAST executable.
# In bash.html these paths resolve in the mounted guest VFS. From the project
# checkout, use the installed snapshots under src/vfs instead.

: "${WAST:=wast}"

if [[ -d /root/test/html-rt ]]; then
  test_dir=/root/test/html-rt
else
  script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
  test_dir="$script_dir/../src/vfs/root/test/html-rt"
fi

shopt -s nullglob
tests=("$test_dir"/*.wast)
if ((${#tests[@]} == 0)); then
  printf 'html-rt.sh: no WAST tests found in %s\n' "$test_dir" >&2
  return 2 2>/dev/null || exit 2
fi

status=0
passed=0
expected_failures=0
skipped=0
failed=0

# report-files.wast verifies redirected verbose and quiet reports produced by
# pass.wast, while input.wast expects one byte from its stdin descriptor.
if ! "$WAST" --verbose "$test_dir/pass.wast" > /tmp/wast-report.txt; then
  printf 'FAIL setup: could not create /tmp/wast-report.txt\n'
  status=1
fi
if ! "$WAST" "$test_dir/pass.wast" > /tmp/wast-quiet.txt; then
  printf 'FAIL setup: could not create /tmp/wast-quiet.txt\n'
  status=1
fi
printf 'x' > /tmp/html-rt-input.txt

for test in "${tests[@]}"; do
  name=${test##*/}
  printf '\n=== %s ===\n' "$name"
  case "$name" in
    posix-kernel.wast)
      printf 'SKIP %s: its fork assertions require a top-level process context.\n' "$name"
      skipped=$((skipped + 1))
      continue
      ;;
    report.fail.wast)
      report=/tmp/html-rt-report-fail.txt
      "$WAST" --verbose "$test" > "$report" 2>&1
      result=$?
      report_output=$(<"$report")
      printf '%s\n' "$report_output"
      if ((result != 0)) &&
          [[ "$report_output" == *"WAST: 2 PASS, 1 FAIL, 3 total"* &&
             "$report_output" == *"one: result mismatch"* ]]; then
        printf 'XFAIL %s: expected negative-control failure observed.\n' "$name"
        expected_failures=$((expected_failures + 1))
      else
        printf 'FAIL %s: negative-control behavior differed from expectation.\n' "$name"
        failed=$((failed + 1))
        status=1
      fi
      continue
      ;;
    input.wast)
      "$WAST" --verbose "$test" < /tmp/html-rt-input.txt
      ;;
    *)
      "$WAST" --verbose "$test"
      ;;
  esac
  result=$?
  if ((result == 0)); then
    printf 'PASS %s\n' "$name"
    passed=$((passed + 1))
  else
    printf 'FAIL %s (exit %s)\n' "$name" "$result"
    failed=$((failed + 1))
    status=1
  fi
done
printf '\nHTML-RT: %s PASS, %s XFAIL, %s SKIP, %s FAIL\n' \
  "$passed" "$expected_failures" "$skipped" "$failed"
return "$status" 2>/dev/null || exit "$status"
