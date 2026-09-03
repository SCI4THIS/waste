# Two-Iteration Tail-Call Trace

This trace evaluates the `$count` function from `return_call.wast` with an
initial argument of `2`. Stack displays are bottom-to-top. Two tail transfers
are followed by the terminating evaluation at zero.

```wat
(func $count (param i64) (result i64)
  (if (result i64) (i64.eqz (local.get 0))
    (then (local.get 0))
    (else
      (return_call $count
        (i64.sub (local.get 0) (i64.const 1))))))
```

## Guest instruction trace

1. `local.get 0` — locals `[2]`; stack becomes `[2]`.
2. `i64.eqz` — stack becomes `[0]`.
3. `if` — consumes `0` and selects the `else` body.
4. `local.get 0` — stack becomes `[2]`.
5. `i64.const 1` — stack becomes `[2, 1]`.
6. `i64.sub` — stack becomes `[1]`.
7. `return_call $count` — consumes `1`, writes local 0, retargets the active
   body, resets PC to zero, and preserves the original return continuation.
8. `local.get 0` — locals `[1]`; stack becomes `[1]`.
9. `i64.eqz` — stack becomes `[0]`.
10. `if` — selects `else`.
11. `local.get 0` — stack becomes `[1]`.
12. `i64.const 1` — stack becomes `[1, 1]`.
13. `i64.sub` — stack becomes `[0]`.
14. `return_call $count` — writes local 0 and resets PC again.
15. `local.get 0` — locals `[0]`; stack becomes `[0]`.
16. `i64.eqz` — stack becomes `[1]`.
17. `if` — selects `then`.
18. `local.get 0` — stack becomes `[0]`.
19. End of body — returns `0` through the original continuation.

## What the compiled OCaml Wasm executes

The debuginfo build names the frame loop `$m8`, evaluator `$step`, and tail
helper `$tail_invoke`. The following is condensed from the generated Wasm;
comments describe the omitted CPS plumbing.

```wat
;; Repeated for every guest instruction.
(call $code_next ...)             ;; inspect boxed (values, instruction chunks)
(array.get $block ...)            ;; unpack OCaml blocks
(ref.cast (ref $block) ...)
(br_table ...)                    ;; dispatch the admin-instruction variant
(call $is_jumping ...)

;; Previously generated once for every guest opcode because the inner loop
;; contained `try ... with TailInvoke`.
(call $caml_push_trap
  (struct.new $env_1_129_1153 ...)) ;; allocate exception continuation

(return_call $step
  (array.new_fixed $block 4 ... )   ;; allocate boxed Eval.config
  (struct.new $env_1_10 ...))       ;; allocate CPS continuation
```

For each `return_call`, the generated path adds:

```wat
(return_call $function_arities
  ...
  (struct.new $env_1_23_210 ...))   ;; continuation for cached arity lookup

(return_call $take_5264 ...)        ;; walk the OCaml operand list

(global.get $TailInvoke)             ;; now a single payload-free exception
(global.get $pending_tail_request)   ;; reusable argument/target/location record
(call $caml_pop_trap)                ;; unwind once per tail transfer
```

The handler then calls `$reset_tail_frame`, which performs more boxed accesses,
generic hash-table lookups, list reversal/mapping, CPS calls, and local-cell
updates before the instruction body and PC are changed.

## Finding

The guest algorithm is already the desired `argument = argument - 1; goto
entry`. Tail transfer is represented by one global payload-free exception and
one reusable request record. The exception handler now surrounds an inner
instruction loop, so `wasm_of_ocaml` installs a trap once per tail run instead
of once per interpreted opcode. Raising and catching the same exception still
happens for every recursion. Eliminating that remaining cost requires an
explicit evaluator outcome such as `Continue code | TailCall(args, func) |
Finished values`; the frame loop could then update locals/body/PC without any
exception setup or unwinding.

## C engine target

The production migration in [c-engine-port-plan.md](c-engine-port-plan.md)
removes this representation boundary. The C engine predecodes each body into a
fixed-width instruction array. Its tail-call dispatch validates the target,
rewrites arguments and locals in the active frame, resets the numeric PC to the
callee entry, and returns directly to the dispatch loop. The acceptance test
requires the deep official tail-call cases to perform no allocation per tail
transfer while producing results identical to the OCaml oracle.
