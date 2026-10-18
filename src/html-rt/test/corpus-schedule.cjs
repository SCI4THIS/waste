"use strict";

const fs = require("node:fs");

/* Return manifest indices rather than rearranging tests: dispatch may change
 * order, but console output and result records still use the manifest index.
 * Timing input uses the runners' existing --results JSON contract. */
function corpusSchedule(tests, flagArgs) {
  const scheduleFlag = flagArgs.find(arg => arg.startsWith("--schedule="));
  const timingFlag = flagArgs.find(arg => arg.startsWith("--timings="));
  const schedule = scheduleFlag ? scheduleFlag.slice("--schedule=".length) : "manifest";
  if (schedule !== "manifest" && schedule !== "longest-first")
    throw new Error(`unknown corpus schedule: ${schedule}`);
  if (schedule === "longest-first" && !timingFlag)
    throw new Error("--schedule=longest-first requires --timings=PATH from a previous --results run");
  if (timingFlag && schedule !== "longest-first")
    throw new Error("--timings=PATH requires --schedule=longest-first");

  const indices = tests.map((_, index) => index);
  if (schedule === "manifest") return indices;
  const history = JSON.parse(fs.readFileSync(timingFlag.slice("--timings=".length), "utf8"));
  if (!Array.isArray(history.tests))
    throw new Error("corpus timing input must contain a tests array from --results");
  const durations = new Map();
  for (const record of history.tests) {
    if (!record || typeof record.identity !== "string" || !record.identity ||
        !Number.isFinite(record.elapsedMs) || record.elapsedMs < 0)
      throw new Error("corpus timing records require an identity and a finite nonnegative elapsedMs");
    if (durations.has(record.identity))
      throw new Error(`duplicate corpus timing identity: ${record.identity}`);
    durations.set(record.identity, record.elapsedMs);
  }
  const duration = tests.map(test =>
    durations.get(test.path || `${test.group}/${test.file}`) ?? 0);
  // New tests follow timed tests; ties and missing durations keep manifest order.
  indices.sort((a, b) => duration[b] - duration[a] || a - b);
  return indices;
}

module.exports = {corpusSchedule};
