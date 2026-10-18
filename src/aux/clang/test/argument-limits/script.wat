#!/bin/wat
(module
  (import "env" "exit" (func $exit (param i32)))
  (func (export "_start") i32.const 42 call $exit))
