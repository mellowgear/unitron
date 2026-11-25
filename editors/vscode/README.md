# unitron: TLCS-900 asm900 for VS Code

Colouring for TLCS-900 (Neo Geo Pocket) asm900 source, and, through `unitron lsp`, go to
definition, find references, rename, hover, document and workspace symbols, links on
`$include` paths, and errors as you type.

This extension, and the `unitron lsp` language server it starts, were written entirely by
Claude, Anthropic's AI model, at the repository owner's request.

## Install

There is no Marketplace release. Build the `.vsix` and install it:

```sh
cd editors/vscode
npm ci
npm run package                 # writes unitron-asm900-<version>.vsix
code --install-extension unitron-asm900-*.vsix
```

(Or in VS Code: Extensions view, `...` menu, "Install from VSIX...".)

The language server needs a built `unitron` on your `PATH`, or `unitron.path` set. Without
it you get one message with a button to open the settings; colouring keeps working.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `unitron.path` | `"unitron"` | The unitron binary; a path, or a name looked up on `PATH`. |
| `unitron.mainFile` | `"src/main.asm"` | Main source file, relative to the workspace root. |
| `unitron.includeDirs` | `["."]` | Include directories, relative to the workspace root. |
| `unitron.trace.server` | `"off"` | `messages` or `verbose` logs LSP traffic to the output channel. |

Changing `unitron.path`, `unitron.mainFile` or `unitron.includeDirs` restarts the server.

## unitron.json

A `unitron.json` at the workspace root sets the same keys for the whole project and wins over
the settings. Its presence also activates the extension.

```json
{
  "mainFile": "src/main.asm",
  "includeDirs": [".", "include", "build/gen"]
}
```

## Commands

- **unitron: Restart Language Server**
- **unitron: Show Output**

## Problem matcher

`$unitron-asm` reads `unitron asm` diagnostics such as

```
b.inc:1: error E215: no section definition before this statement
    included from a.asm:1
main.asm:11: warning W507: text after `end` is ignored
```

(the `included from` lines are not separate problems). Paths are taken as relative to the
workspace folder unless absolute. A sample `.vscode/tasks.json`:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "make",
      "type": "shell",
      "command": "make",
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": "$unitron-asm"
    }
  ]
}
```

If make runs `unitron asm` from a subdirectory, override the location, e.g.
`"problemMatcher": { "base": "$unitron-asm", "fileLocation": ["relative", "${workspaceFolder}/src"] }`.

## Using another extension for .asm / .inc

This extension claims `.asm` and `.inc`. VS Code cannot turn a contributed file extension
off at runtime, so to hand them to another language use `files.associations`, globally or
per workspace:

```json
{
  "files.associations": {
    "*.asm": "other-asm-language-id",
    "**/z80/**/*.inc": "other-asm-language-id"
  }
}
```

The language server only runs for files in the `asm900` language.

## Development

`npm run compile` bundles `src/extension.ts` into `dist/extension.js` with esbuild,
`npm run watch` rebuilds on change, `npm run typecheck` runs `tsc --noEmit`.

## License

MIT; see `LICENSE`.
