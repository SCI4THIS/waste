# Thread-safety analyzer stream compatibility

The localization-free guest C++ profile has no complete std::ostream type.
ThreadSafetyUtil.h defined an unused StringRef insertion adapter for that type,
preventing its production owners from compiling. Only that adapter is excluded
under `LLVM_WASTE_GUEST && !LLVM_ENABLE_THREADS`. The existing TIL StdPrinter,
sx::toString and diagnostic paths already use LLVM raw streams and remain
unchanged. No analyzer, thread support, synchronization or runtime behavior is
removed or replaced.

With the guest compiler prerequisites configured, run:

```sh
make -C src/aux thread-safety-source
```

This private source gate compiles all four real guest owners (ThreadSafety,
ThreadSafetyCommon, ThreadSafetyLogical and ThreadSafetyTIL) through their
production rules. Each full source also receives native, native-threaded and
guest-threaded syntax checks using host C++ headers. These twelve syntax checks
explicitly call the original std::ostream/StringRef adapter to preserve that
API in the corresponding profiles. Native configuration headers come first;
the configured guest tree then supplies target-independent generated enums,
so missing OpenMP output cannot fall through to an unrelated installed LLVM. They do not establish a threaded guest
runtime or full analyzer/compiler runtime acceptance.

There is no new functional WAST fixture for this compatibility-only exclusion:
the production raw-stream printer and all analyzer logic are unchanged. Actual
compiler execution remains covered by the package acceptance gate.
