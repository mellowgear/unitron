# Drift check for the editor grammar: lsp-keywords.out must equal editors/vscode/keywords.txt
# (the file scripts/gen-grammar.mjs reads). When this fails, refresh both:
# unitron lsp --keywords > editors/vscode/keywords.txt, copy it here, run node scripts/gen-grammar.mjs.
lsp --keywords
