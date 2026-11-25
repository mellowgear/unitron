# Editor support

unitron has a language server, `unitron lsp`, and a VS Code extension in `editors/vscode/`.
Both were written entirely by Claude (Anthropic's AI model) at the repository owner's request.

## What you get

- **Colouring** (the extension's TextMate grammars, no server needed): asm900 (`.asm`, `.inc`),
  Z80 (`.z80`), link files (`.lcf`) and character maps (`charmap.txt`, `*.charmap`).
- **With `unitron lsp`**, for asm900:
  - go to definition (labels, `equ`, macros, `#define`), including on `$include`, `#include` and
    `$charmap` paths
  - find references and rename, across every file of the include tree
  - hover: the defining line, a label's address or an `equ`'s value (hex and decimal), its section,
    and the comment block above the definition (so a routine's notes and a hardware name's
    citation show where it is used)
  - the file outline and workspace symbol search
  - errors and warnings as you type, from a real assembly

## How the server works

Each analysis assembles the project's main file with the same `Assembler` as `unitron asm`,
reading open editor buffers instead of the disk. The include tree, the symbols, their values and
the errors are therefore the build's own. Each file of the tree is then split into identifier
tokens (outside comments and strings), which definition, references and rename use. An analysis
runs when the editor has been quiet for 300 ms after a change; a large project (a whole 1 MB ROM)
takes one to three seconds.

The project is set in `unitron.json` at the workspace root, else by the client
(`initializationOptions`), else the defaults:

```json
{ "mainFile": "src/main.asm", "includeDirs": ["."] }
```

`includeDirs` are the `-I` folders of your `unitron asm` command line, relative to the root.
Includes that don't exist yet (files a build generates under `build/`, as `-MG` allows) are
skipped; build once to get every value and error.

Not covered: names made inside macro expansions (`labels` names, `?param`), and a symbol written
only inside a macro body is found by its spelling, not its meaning. Rename edits only the files of
the include tree.

## VS Code

See `editors/vscode/README.md`: build the `.vsix` with `npm ci && npm run package`, install it
with `code --install-extension`, and put `unitron` on your PATH (or set `unitron.path`).

## Other editors

Any LSP client works: the command is `unitron lsp` (stdio). Use language id `asm900` for `*.asm`
and `*.inc`.

- **IntelliJ (LSP4IJ)**: Settings, Languages & Frameworks, Language Servers, `+`: command
  `unitron lsp`; Mappings, file name patterns `*.asm` and `*.inc`, language id `asm900`.
- **Neovim** (0.10+):

  ```lua
  vim.filetype.add({ extension = { asm = "asm900", inc = "asm900" } })
  vim.api.nvim_create_autocmd("FileType", {
    pattern = "asm900",
    callback = function()
      vim.lsp.start({ name = "unitron", cmd = { "unitron", "lsp" },
                      root_dir = vim.fs.root(0, { "unitron.json", "Makefile", ".git" }) })
    end,
  })
  ```

## Testing the server

`unitron lsp --script FILE` reads messages from FILE, one JSON object per line, and prints each
reply and notification on a line; the current folder's file URI is written `file:///ROOT`. The
cases in `tests/lsp/` use it. `unitron lsp --keywords` prints the reserved words the grammar is
generated from (`editors/vscode/scripts/gen-grammar.mjs`); `tests/cli/lsp-keywords` checks that
`editors/vscode/keywords.txt` matches it.
