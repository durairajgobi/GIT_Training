# CAPL Scripting for VS Code

Language support for **Vector CAPL** (`.can`, `.cin`) — the scripting language of CANoe / CANalyzer —
with a keyword database you can extend from settings or JSON files.

## Features

| Feature | What you get |
|---|---|
| Syntax highlighting | `variables` / `includes` sections, `on <event>` procedures (start, timer, message, `signal_update`, `sysvar_update`, diagRequest/Response, LIN, FlexRay, Ethernet, PDU, J1939 …), `testcase` / `testfunction`, `struct` / `enum`, `$Signal`, `@sysvar::Ns::Var`, message selectors (`this.dlc`, `.byte(0)`, `.phys`), `printf` placeholders, hex/binary literals, doc-comment tags |
| Completion | 130+ built-in functions with parameter snippets, keywords, types, constants; your functions, globals, locals and parameters; symbols from `#include`d files; event list after `on `; selector list after `.` |
| Hover hints | Signature, overloads, parameter descriptions, category and source for every function; descriptions for keywords/types/events; doc comments (`/** … */` or `//` lines) of your own functions |
| Signature help | Active parameter tracking, overload selection by argument count, variadic (`...`) support |
| Inlay hints | Parameter names in calls — for literal arguments only (default), every argument, or off |
| Navigation | Outline (sections, event handlers, functions, test cases, structs, variables), go to definition across includes, clickable `#include` paths, brace/comment folding |
| Diagnostics | Unbalanced brackets, duplicate `variables` section, duplicate event procedures / test cases, unknown event types, deprecated calls, optional unknown-function check |
| Quick fixes | "Add *X* as a custom CAPL event keyword / function" on unknown events/functions |
| Snippets | Node skeleton, event handlers, test module, diagnostics request, structs, loops, write helpers … |

## Configuring keywords and hints

Definitions are layered — later layers override earlier ones:

1. built-in database (`data/builtins.json`)
2. JSON files in `capl.keywordFiles`
3. `capl.customKeywords` / `capl.customFunctions` in settings

### From settings

```jsonc
// .vscode/settings.json
{
  "capl.customKeywords": [
    { "name": "on_myBus", "kind": "event", "description": "Fires when MyBus reports a frame." },
    { "name": "MyFrame",  "kind": "type",  "description": "Frame type of the MyBus DLL." },
    { "name": "kTimeout", "kind": "constant", "description": "Default timeout (ms)." }
  ],
  "capl.customFunctions": [
    {
      "name": "MyLib_Send",
      "returnType": "long",
      "category": "MyLib",
      "description": "Sends a frame through the MyLib CAPL DLL.",
      "parameters": [
        { "name": "id",   "type": "dword",  "description": "CAN identifier" },
        { "name": "data", "type": "byte[]", "description": "payload" }
      ],
      "overloads": [[ { "name": "msg", "type": "message" } ]]
    }
  ],
  "capl.deprecatedFunctions": { "getValue": "Use @sysvar:: access instead" },
  "capl.disabledBuiltins": ["beep"]
}
```

Parameters may also be written as strings: `"parameters": ["dword id", "byte data[]", "..."]`.

### From keyword files (shareable per project / team)

Run **CAPL: Create Keyword Definition File…** — it writes a template `capl-keywords.json`, adds it to
`capl.keywordFiles` and opens it. Files named `capl-keywords.json` or `*.capl-keywords.json` get JSON
schema validation and completion. Files are watched and reloaded on save.

```json
{
  "keywords":  [ { "name": "someipEvent", "kind": "event", "description": "SOME/IP event", "snippet": "on someipEvent ${1:Service.Event}\n{\n\t$0\n}" } ],
  "functions": [ { "name": "SomeIpSubscribe", "returnType": "long", "parameters": ["dword serviceId", "dword eventGroup"] } ]
}
```

### Commands

| Command | |
|---|---|
| `CAPL: Add Custom Keyword / Function…` | Guided input; writes to workspace (or user) settings |
| `CAPL: Create Keyword Definition File…` | Template JSON file, registered in `capl.keywordFiles` |
| `CAPL: Browse Keyword Database…` | Search all functions/keywords with their hints and insert one |
| `CAPL: Insert Event Handler…` | Pick an event and insert its handler skeleton |
| `CAPL: Reload Keyword Definitions` | Re-read builtins, files and settings (log in the *CAPL* output channel) |

### All settings

| Setting | Default | |
|---|---|---|
| `capl.customKeywords` | `[]` | Extra keywords / types / events / constants |
| `capl.customFunctions` | `[]` | Extra functions with signatures and hints |
| `capl.keywordFiles` | `[]` | JSON definition files (relative to workspace folders, `~` allowed) |
| `capl.disabledBuiltins` | `[]` | Hide built-in entries |
| `capl.deprecatedFunctions` | `{}` | name → message; usages are struck through |
| `capl.completion.enable` | `true` | |
| `capl.hover.enable` | `true` | |
| `capl.signatureHelp.enable` | `true` | |
| `capl.inlayHints.parameterNames` | `literals` | `none` / `literals` / `all` |
| `capl.diagnostics.enable` | `true` | |
| `capl.diagnostics.unknownFunctions` | `false` | Warn on calls not found in builtins, settings, the file or its includes |

## Development

```bash
npm install
npm test          # compile + parser, registry, grammar and extension tests
npm run package   # build capl-scripting-<version>.vsix (needs @vscode/vsce)
```

Press <kbd>F5</kbd> in VS Code to launch an Extension Development Host on `examples/`.

Install a built package with:

```bash
code --install-extension capl-scripting-1.0.0.vsix
```

### Layout

```
src/registry.ts     keyword/function database: validation, layering, signatures (no vscode dependency)
src/parser.ts       tolerant CAPL parser: symbols, includes, calls, diagnostics (no vscode dependency)
src/definitions.ts  loads builtins + keyword files + settings, watches for changes
src/analysis.ts     per-document cache, #include resolution
src/providers.ts    completion, hover, signature help, inlay hints, symbols, diagnostics …
src/commands.ts     command palette commands
data/builtins.json  built-in CAPL keywords and functions
syntaxes/           TextMate grammar
snippets/           code snippets
schemas/            JSON schema for keyword files
```

## Notes

The built-in descriptions are short summaries written for this extension; the Vector CAPL
documentation in CANoe is the reference. Function availability depends on your CANoe version and
options (e.g. Diagnostics, LIN, FlexRay, Ethernet). Add anything that is missing for your setup with
`capl.customFunctions` or a keyword file.
