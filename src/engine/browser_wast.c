/*
 * browser_wast.c — Wasm browser platform backend.
 *
 * This file previously contained the Wasm-specific platform implementations
 * (strtod/strtof via JS host imports, heap allocator, FILE I/O no-ops,
 * getenv/isatty/exit stubs).  These have been split into the lib/ directory:
 *
 *   lib/stdlib_2.c      — strtod/strtof (JS host), getenv, exit, heap allocator
 *   lib/stdio_2.c       — FILE I/O no-op stubs, strerror
 *   lib/unistd_2.c      — isatty stub
 *   lib/errno.c         — errno and yydebug globals
 *
 * This file is kept as a placeholder for any future browser-specific code
 * that does not fit into the category-based library split.
 */
