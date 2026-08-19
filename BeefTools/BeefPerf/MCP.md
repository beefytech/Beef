# BeefPerf as an MCP server

BeefPerf can expose the capture it is holding over MCP, so an agent can search zones, break one down,
and compare threads across a slice of time without a human driving the window.

## Running it

```
BeefPerf.exe -port=4209 -mcp
```

- `-port=<n>` is the port instrumented programs connect to. Default 4208.
- `-mcp` starts the MCP endpoint on `-port` + 1. `-mcp=<n>` picks the port outright.

Running on a non-default port is the point of `-port`: an automated capture gets its own BeefPerf and
leaves whatever you already have open on 4208 alone. The status bar shows `MCP: <port>` once the
endpoint is up, and says so in red if the port was taken.

BeefPerf now honors the show state it was launched with, so `start /min BeefPerf.exe -mcp` (or a
shortcut set to "Minimized") leaves it out of the way while it collects.

Register it with Claude Code:

```
claude mcp add --transport http beefperf http://127.0.0.1:4210/mcp
```

The endpoint only binds to localhost, and rejects requests carrying a non-local `Origin` header so a
web page cannot reach it. `GET /` returns a plain-text status page, which is the quickest way to
confirm by hand that the BeefPerf you think is listening is the one that actually is.

## Pointing a program at it

The server name passed to `BpInit` may carry a port:

```cpp
BpInit("127.0.0.1:4209", "My Program");
```

For programs whose `BpInit` call is baked in -- `IDEHelper` calls it from `DllMain`, which is what
gives you a capture of the Beef compiler -- set the `BeefPerfServer` environment variable instead. It
overrides the argument entirely, so no rebuild is needed:

```
set BeefPerfServer=127.0.0.1:4209
BeefBuild.exe -workspace=... -run
```

## Tools

| Tool | What it answers |
| --- | --- |
| `status` | Is the profiler up, what address do programs connect to, is anything connected |
| `list_sessions` | Every capture held, live and finished, with duration/threads/zone count |
| `session_info` | One session in detail, including the thread indexes the other tools take |
| `select_session` | Show a session in the window, so a human sees what is being queried |
| `clear_session` | Throw away what has been captured so far without dropping the connection |
| `find` | Search zones and events by name, over an optional time range and thread filter |
| `profile` | Count/total/self breakdown under one zone, or over a time range on a thread |
| `time_slice` | What every thread was doing across one span -- for cross-thread stalls |

Times crossing the API are microseconds from the start of the session. `find` also returns the raw
`startTick` and `depth` of each hit, which is what `profile` takes to address that exact zone.

A useful starting move: `find` with `text` empty sorts by duration descending, which returns the
slowest zones in the session (or in a range) without needing to know any names yet.

## Repeated runs

`clear_session` is what makes a before/after comparison readable. It is the same thing Ctrl+X does in
the timeline: the capture is emptied and its clock restarts at zero, but the session and its socket
survive, so a client that is still attached keeps recording into the clean timeline. The loop is
clear, exercise the thing, read, clear again -- each run measured on its own instead of buried in the
accumulated capture.

A program that exits between runs (a compiler invocation, say) produces a new session per run
instead, and those stack up in `list_sessions` with distinct ids.

## How the queries differ from the panels

The Find and Profile panels scan incrementally, a slice per frame, re-walking the tail of a live
session -- hence all their bookkeeping to avoid reporting the same entry twice. `BPQuery.bf` instead
walks each stream buffer exactly once with `BPStateContext.mAutoLeave` turned off, which makes that
bookkeeping unnecessary: an entry whose `.Leave` has not arrived by the end of a buffer is dropped
there and picked up in the buffer that does contain it, carrying its original start tick across every
split it spans.

The trade-off is that zones still open at the live edge of a running capture are not reported until
they close. The panels show those with an inferred end tick; a query would rather say nothing than
report a duration that is not real.
