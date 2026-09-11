# WASTE

Webassembly Threading Environment is webassembly implementation targeting web browsers
that provides threading functionality.  It compiles to webassembly that can be 
ran in a browser's webassembly module.  The nested approach allows for the waste
implementation to provide features that aren't available in the browser.  
It allows for threading and for process yielding and restarting without use of asyncify.

## Webassembly terms

.wasm files are binary files that contain webassembly op-codes and can be ran by a web assembly machine

.wat files are text files that are able to be converted directly to a .wasm file

.wast files are text files that are for scripting.  These are meant to be ran in a streaming mode where if an error occurs, the execution will continue.  The wasm specification
is encoded in several wast files, so in order to run the spec tests directly an
implementation must be able to execute .wast files.

## POSIX environment

This project is evolving towards a POSIX environment that can run in the browser.
Because of the sandbox nature of the browser it won't be able to offer an entire
POSIX environment.  The problem can be understood by the example of the POSIX socket
library.  Web browsers do not have the ability to create raw sockets.  The best they can
do is to create websockets.  So a complete POSIX environment can't exist within a
browser.  This project recognizes 3 categories of posix libraries:

1. Pure computation - string, math, conversion, formatting.  Entirely self-contained
in C, no environment interaction

2. Emulatable environment - memory allocation, file I/O, clock, signal.  Require an
an action from the environment but can be emulated in-memory (VFS, virtual process,
virtual clock)

3. Physical hardware - sockets, serial, USB.  Require control over a physical NIC, 
serial or USB port.

Categories 1 and 2 can run in the browser, but category 3 can't.  The idea for
the implementation of 3 is to have a peer server hosted on the machine that is able 
to handle the category 3 functionality.  It would be controlled from the browser over 
a websocket connection.

## Development Timeline

This started as an exploratory project in 2024.  Originally, I was looking
at the fantastic wasm3 project (https://github.com/wasm3/wasm3).  While looking
through the comments I saw Volodymyr briefly mention using asyncify to add threading
into wasm3.  I put together a small proof of concept compiling wasm3 with clang
to a wasm target and then asyncifying the webassembly.  The wasm3 interpreter
was then able to run a simple sleep program (that wasn't asyncified) to show that
asyncify may only be needed on the implementation and not on the execution target.

I was interested trying to get bash running in the browser, so I had compiled a
bash wasm target, but I ran into a major hangup with the signal handling that
bash needs.  wasm3 is highly optimized for speed and as a result its architecture
does not lend itself to the execution framing needed for signal handling.

The reference OCaml interpreter has an execution model that lends itself to
signal handling much better than wasm3 and I had poked at it a little bit, but
I did not have very much experience with OCaml, which was hindering the progress.
I had started working on building a C based wasm implementation to handle this, getting
to the point of writing a wasm disassembler which could parse the op codes.  The
development just stalled out at that point.

August 2026 I decided to get a Claude and Codex subscription and try them out on
this large project and really go to town on this project.

I had the agents build a patch file to apply to the OCaml reference interpreter
which inject a threading environment onto it.  This is able to run all the spec
tests in parallel.  It doesn't really run any faster, but the tests will start
finishing about the same time.  The OCaml implementation does poorly with the
tail call functions.  After trying to performance tweak the return call functions
would take about 60 seconds to complete.

I got the bash prompt to run through the OCaml interpreter.  Initially it would
take 26 seconds to display, after some performance tweaks I was able to get it
down to 6 seconds, but that was much too slow.

I put together a small proof of concept running a c-engine based frame system to
run the return call functions.  This was able to complete in under a seconds, compared
to the 60 seconds in OCaml, so I decided to try to build out a full implementation.

After draining several AI weekly usage alotments I was able to put together a
flex/bison based parser for .wat and .wast along with a .wasm execution engine.
This is able to complete the return call functions in seconds instead of minutes.
It is able to get to the bash prompt in 300ms instead of 6 seconds.  This is
fast enough to be practical.

## TUI

Leveraging the AI I have created a TUI that can be started by running `./start.sh`
It has a pre-check to ensure that all the required tools are installed then it
launches a menu where specific compilations can be performed.  This allows for
spec-testing the OCaml impelementation and the waste engine.  Generating a bash
shell static HTML using OCaml and C-engine, and running unit tests on the compiled
code.

I have only tested it on Omarchy / Arch.  Other Linux distros will need some tweaks 
to get it to work.  It has a secondary mode where you can feed it arguments to 
execute specific components. 

