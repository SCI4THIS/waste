# sleep

This package selects upstream GNU Coreutils' `src/sleep.c` through
`sources.mk`. Build with `make -C src/aux sleep`, install explicitly with
`make -C src/aux install-sleep`, and run package WAST checks with
`make -C src/aux test-sleep`.

The command calls a local timed-wait bridge through the versioned
`waste_kernel.pselect_v1` ABI, avoiding a nested shared-libc call for its main
wait. Native sessions wait against the host monotonic clock; browser workers
arm a deadline timer and resume the engine when it expires.
