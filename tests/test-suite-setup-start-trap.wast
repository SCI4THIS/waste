;; A trapping ordinary start is a setup failure, not assert_trap.
(module (func $start unreachable) (start $start))
