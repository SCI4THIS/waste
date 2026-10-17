# Run the actual installed compiler; WAST owns every functional assertion.
printf 'layout-seed\nignored\n' > /tmp/unsafe-buffer-seed.txt
/usr/bin/clang -std=c++20 -fsyntax-only -Wunsafe-buffer-usage \
  -Xclang -fsafe-buffer-usage-suggestions -fdiagnostics-parseable-fixits \
  -fno-color-diagnostics -frandomize-layout-seed-file=/tmp/unsafe-buffer-seed.txt \
  /tmp/unsafe-buffer.cpp > /tmp/unsafe-buffer.txt 2>&1
printf '%s\n' "$?" > /tmp/unsafe-buffer-status.txt
printf 'parent-alive\n' > /tmp/unsafe-buffer-parent.txt
