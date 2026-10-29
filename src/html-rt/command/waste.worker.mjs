import {createWaste} from "./waste.mjs";

// The same entry is installed on a browser WorkerGlobalScope or a test port.
export function installWasteWorker(port) {
  let runtime, initializing = false, active, lastId = 0, defaultLimits = {};
  const post = message => port.postMessage({protocol: 1, ...message});
  const failure = (id, error) => post({type: "error", id, error: {
    name: error.name || "WasteError", code: error.code || 1, message: error.message || String(error)
  }});
  port.addEventListener("message", async ({data: message}) => {
    if (!message || message.protocol !== 1) { failure(message?.id ?? 0, Error("Incompatible worker protocol")); return; }
    const {id, type} = message;
    if (type === "init") {
      if (id !== 0 || runtime || initializing) { failure(id, Error("Worker is already initialized")); return; }
      initializing = true;
      try {
        defaultLimits = message.limits || {};
        runtime = await createWaste({engine: message.engine, files: message.files, limits: message.limits});
        post({type: "ready", id: 0, abiVersion: runtime.abiVersion});
      } catch (error) { failure(0, error); }
      finally { initializing = false; }
      return;
    }
    if (!Number.isSafeInteger(id) || id < 1) { failure(id, Error("Invalid worker request ID")); return; }
    if (type === "cancel") {
      if (active?.id === id) active.abort.abort();
      return;
    }
    if (type === "input") {
      if (active?.id !== id || !active.streaming || active.eof) return;
      if (!Number.isSafeInteger(message.sequence) || message.sequence !== active.sequence + 1 ||
          !(message.bytes instanceof Uint8Array)) {
        active.inputError = Error("Invalid input sequence or bytes");
        active.abort.abort(); return;
      }
      active.sequence = message.sequence;
      active.received += message.bytes.length;
      if (active.received > active.maxInput) {
        active.inputError = Error("Worker input limit exceeded");
        active.abort.abort(); return;
      }
      active.eof = Boolean(message.eof);
      active.queue.push({value: message.bytes, done: active.eof && !message.bytes.length});
      active.wake?.(); active.wake = undefined;
      post({type: "input-ack", id, sequence: message.sequence});
      return;
    }
    if (type !== "run") return;
    if (!runtime || active || id <= lastId) { failure(id, Error("Worker is busy, uninitialized, or request is stale")); return; }
    lastId = id;
    const current = {id, abort: new AbortController(), queue: [], eof: false, streaming: Boolean(message.streaming),
      sequence: 0, received: 0, maxInput: message.job?.limits?.maxInputBytes ?? defaultLimits.maxInputBytes ?? 16 * 1024 * 1024};
    active = current;
    try {
      let input = message.job.stdin;
      if (current.streaming) input = {
        [Symbol.asyncIterator]() { return this; },
        async next() {
          while (active === current && !current.queue.length && !current.eof)
            await new Promise(resolve => { current.wake = resolve; });
          return current.queue.shift() || {done: true};
        },
        async return() { current.eof = true; current.wake?.(); return {done: true}; }
      };
      const result = await runtime.run({...message.job, stdin: input, signal: current.abort.signal});
      if (current.inputError) throw current.inputError;
      post({type: "result", id, result});
    } catch (error) { failure(id, error); }
    finally {
      current.eof = true; current.wake?.(); current.queue.length = 0;
      if (active === current) active = undefined;
    }
  });
  port.start?.();
}

if (typeof WorkerGlobalScope !== "undefined" && globalThis instanceof WorkerGlobalScope)
  installWasteWorker(globalThis);
