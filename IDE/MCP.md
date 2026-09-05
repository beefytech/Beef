# BeefIDE as an MCP server

The IDE can host an MCP endpoint so an agent can drive it: open files, edit, build, debug, read
panels, and (in later phases) inspect and operate the UI itself. The goal is an IDE that can be
tested end to end without a human at the keyboard.

## Running it

```
BeefIDE.exe -mcp
BeefIDE.exe -mcp=4301 -workspace=C:\proj\Foo
```

- `-mcp` starts the endpoint on the default port, 4300. `-mcp=<port>` picks the port.
- Give each IDE instance you automate its own port, and leave the one you work in alone.
- `-deterministic` is worth adding for automated runs: it removes timing-dependent behavior
  (hover and autocomplete delays) that makes results vary from run to run.

The Output panel logs `MCP: listening on http://127.0.0.1:<port>/mcp` once the endpoint is up, and the
window title carries `(MCP Waiting)` until a client talks to it, then `(MCP Control)` while one is
connected or has made a request in the last 15 seconds.

`-mcp` also puts the IDE into an automation posture:

- **Virtual focus.** The agent's own window is normally the OS foreground, which would make the IDE
  treat itself as unfocused: popup menus would close the instant they opened and injected keys would
  go nowhere. With `-mcp` the IDE keeps its logical focus regardless of which application is in
  front (`BFApp.SetVirtualFocus`); genuine focus gains are still honored.
- **Errors are caught, not dialogs.** A Beef assert or fatal error normally ends in a modal crash
  dialog with nobody there to click it. Under `-mcp` the server records the error in
  `status.lastRuntimeError` and in `mcp_last_error.txt` beside the exe (which survives the
  process), attaches it to whatever tool call was running, and lets asserts continue so the session
  can report and recover. `mcp_self_test` trips a harmless assert to confirm this is working.
  If the endpoint stops answering, read `mcp_last_error.txt` first.
- **Background window.** The main window comes up without activation (and not maximized), so an
  automated IDE never takes the foreground from the window you are working in; with virtual
  focus on, the IDE's own foreground requests stay logical too. Clicking the window still
  activates it normally.
- `-deterministic` is not implied, but recommended for repeatable runs.

Register it with Claude Code:

```
claude mcp add --transport http beefide http://127.0.0.1:4300/mcp
```

The endpoint binds to localhost only and rejects requests carrying a non-local `Origin` header, so a
web page cannot reach it. `GET /` on the port returns a plain-text status page, the quickest way to
confirm by hand that the IDE you think is listening is the one that actually is.

## How it is built

The pieces are layered so the IDE-specific part stays small and a derived IDE (BriskIDE) can add its
own tools without touching the core:

| Where | What |
| --- | --- |
| `BeefLibs/Beefy2D/src/mcp/MCPServer.bf` | Generic server: localhost HTTP, JSON-RPC, `tools/list`, `tools/call`, deferred replies, image content. No IDE knowledge; usable by any Beefy app. |
| `BeefLibs/Beefy2D/src/mcp/MCPToolSet.bf` | `[MCPTool]` / `[MCPParam]` attributes, the per-call `MCPCall` object, and the `MCPToolSet` base class. |
| `BeefLibs/Beefy2D/src/mcp/MCPJson.bf` | JSON string building and base64. |
| `IDE/src/mcp/*.bf` | The IDE's tool sets. |
| `IDEApp.RegisterMCPTools` | Virtual; a derived IDE calls `base` then adds its own tool sets. |

Everything runs on the main thread, pumped from `IDEApp.Update`, so tools see exactly the state the
UI does. A tool that needs the IDE to do work over several frames calls `MCPCall.Defer` with a poll
delegate; the HTTP response is held until the poll reports completion or the timeout passes.

### Writing a tool

```beef
[MCPTool("open_file", "Open a source file in the editor and optionally jump to a line.")]
void OpenFile(MCPCall call,
	[MCPParam("Path, absolute or relative to the workspace.", true)] String path,
	[MCPParam("0-based line to show. Omit to keep the current position.")] int line)
{
	...
	call.Result("{\"ok\":true}");
}
```

The first parameter is always the `MCPCall`. The rest are marshalled by name from the request:
`String`, `int`/`int32`/`int64`, `float`/`double` and `bool` are supported, and the input schema is
generated from them. A tool wanting nested or array arguments passes an explicit schema as the third
attribute argument (written with `'` in place of `"`) and reads `call.mArgs` directly. Results are
JSON text by convention; `call.Error` marks the call failed, `call.AddImage` attaches a PNG.

Tools that wait use the same idle test the IDE's own test scripts use (`ScriptManager.IsIdle`), so
"run this, then read that" sequences work without the client polling.

## Tools

### Core (`IDECoreToolSet`)

| Tool | What it does |
| --- | --- |
| `status` | Exe path and age, workspace and projects, config/platform, compiling, idle, debugger state, active document and cursor, open dialogs. Call it first, and again whenever something did not behave -- an open dialog is the usual reason. |
| `run_script` | Run lines of the IDE's `-test` script language through a queue that waits for the IDE to be idle between commands. Exposes every existing `ScriptHelper` command. |
| `wait_idle` | Wait until no compile is running, resolve has finished and the debugger has settled. |
| `wait_frames` | Wait for N update frames, for UI that settles over time. |
| `get_output` | Read the Output panel, incrementally via the returned `next` offset. |

### UI (`Beefy.mcp.UIToolSet`, works for any Beefy app)

Seeing:

| Tool | What it does |
| --- | --- |
| `get_windows` | Every window (main, dialogs, menus, tooltips, floating panels) with id, kind, rect, focus and mouse state. |
| `get_ui_tree` | The widget tree with each widget's id, type, rect and state: labels, text, checked, selected, open, scroll position. Narrow with `root` and `depth`. |
| `find_widgets` | Locate widgets by shown text and/or type name. |
| `screenshot` | PNG of the main window, one window, one widget, or every window composited at its screen position (`all`). Rendered offscreen, so it works minimized or covered. |
| `pixel_probe` | The color at one point, for exact checks without an image. |
| `get_tooltip` | The tooltip on screen, its text and owner. |
| `get_dialogs` | Open dialogs with title, text and buttons. |
| `get_popup_menus` | Open context/dropdown menus and their items. |

Driving:

| Tool | What it does |
| --- | --- |
| `mouse_move`, `click`, `drag`, `wheel` | Pointer input at a point or on a widget, through the real `WidgetWindow` paths so hover, tooltips, drag handlers and focus behave as with a mouse. |
| `key`, `type_text` | Key chords (`Ctrl+Shift+B`, `F5`, `Escape`) and typed text, through the real key paths so shortcuts fire. |
| `focus_window` | Bring a window to the front and give it focus. |
| `click_dialog_button`, `select_popup_item` | Operate dialogs and menus by label. |

Widget state comes from `Widget.Describe(StructuredData)`, a virtual any widget can override to add what it shows. Buttons, check boxes, combo boxes, edit widgets, list view items, tabs, dialogs, menu items, tooltips and scrollable widgets already do; a new widget type that carries state should too.

Screenshots need `Res_EncodePNG` in BeefySysLib, added alongside `Res_WritePNG`, so BeefySysLib has to be rebuilt once.

### Editor (`IDEEditorToolSet`)

Lines are 0-based, as the IDE counts them; columns are character offsets within the line.

| Tool | What it does |
| --- | --- |
| `get_open_documents` | Open documents with path, project, unsaved state, active flag, cursor, and panel/tab widget ids. |
| `open_file` | Open or raise a file, optionally at a line and column. |
| `get_document` | The unsaved buffer text with numbered lines, a window of it for big files, plus cursor and selection. |
| `set_cursor` | Move the cursor and scroll it into view, optionally selecting to a second position. |
| `get_errors` | The Errors panel's parse and resolve errors with file, position, code, message and related info. |
| `get_autocomplete`, `select_autocomplete`, `close_autocomplete` | Read the autocomplete list and parameter info, accept an entry, or dismiss it. |
| `hover_at`, `get_hover` | Hover the mouse over a position through the real mouse path and return the popup: type info and docs, or the watch tree while debugging. |

### Workspace and commands (`IDEWorkspaceToolSet`)

| Tool | What it does |
| --- | --- |
| `get_workspace_tree` | Projects, folders and files with full paths. |
| `get_project`, `get_workspace` | The full configuration as it would be saved to BeefProj.toml / BeefSpace.toml, as JSON. |
| `list_commands`, `execute_command` | The named commands behind every menu item and shortcut, and running one. |
| `get_menu_bar`, `select_menu` | The menu bar with enabled/checked state refreshed as opening would, and activating an item by label path. |
| `get_panels`, `show_panel` | Every tab in every tab group, and showing a panel by type name. `activate_tab` in the UI set raises a tab by id. |

### Editing (`IDEEditToolSet`)

| Tool | What it does |
| --- | --- |
| `edit_document` | Replace a line/column range (or insert at a position) through the edit widget, so it is undoable and re-parsed like typing. |
| `insert_text` | Insert at the cursor, replacing any selection. |
| `undo`, `redo` | Step the document's undo history. |
| `save_file`, `save_all` | Write to disk. |

### Build (`IDEBuildToolSet`)

| Tool | What it does |
| --- | --- |
| `build` | Build Workspace, with the reply held until the build finishes: success flag, the Output panel text it produced, error and warning counts. |
| `cancel_build`, `clean` | Cancel the build in progress; delete the build cache (`beef=true` for Clean Beef). |

### Debugger (`IDEDebugToolSet`)

Actions run the IDE's own commands so the panels follow along; the ones that wait reply once the target has paused again or exited, with the debug state and stop location.

| Tool | What it does |
| --- | --- |
| `debug_state` | Running, paused, run state, active frame and thread, exception, location, breakpoint count. |
| `debug_start` | Start Debugging / Start Without Debugging / Start Without Compiling; returns once the target runs or the build fails. |
| `debug_stop`, `debug_break`, `debug_continue`, `wait_for_break` | Lifecycle, with waits. |
| `debug_step` | Into, over or out, N times, waiting after each. |
| `run_to_cursor` | Run to a file and line. |
| `get_breakpoints`, `add_breakpoint`, `set_breakpoint`, `remove_breakpoint`, `remove_all_breakpoints` | Source and symbol breakpoints with conditions and hit counts. |
| `eval` | Evaluate an expression in the active frame; asynchronous calls into the target are awaited. |
| `get_callstack`, `select_frame`, `get_threads`, `select_thread` | Navigate the paused target. |
| `get_watches`, `add_watch`, `remove_watch`, `get_locals` | The Watch and Auto Watch panels, values and types included. |

Everything an agent needs to exercise the IDE end to end is now in place: build, run, break, inspect, edit, and see. Later work is refinement (widget Ids on important controls, more `Describe` overrides) and the BriskIDE tool set.
