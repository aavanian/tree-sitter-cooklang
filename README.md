# Cooklang Tree-Sitter Grammar

This is a [Tree-Sitter](https://tree-sitter.github.io/) grammar for [Cooklang](https://cooklang.org/).

## TODO

### Bugs

1. **Go and Python bindings missing `scanner.c`** — `bindings/go/binding.go` and
   `setup.py` both include only `parser.c`.  The external scanner is required;
   without it both bindings produce a broken parser at runtime.
2. **`scan_text_until` swallows `@`/`#` after a non-timer `~`** — when `~` is
   not followed by `{` on the same line everything up to `\n` is consumed as
   text, including any ingredient/cookware sigils that follow (`scanner.c`,
   `~` lookahead block).
3. **`frontmatter_content` cannot handle blank lines** — the rule `/[^\n]+/`
   requires at least one non-newline character per line; an empty line within
   YAML frontmatter truncates the block (`grammar.js`).
4. **`(quantity "%") @operator` captures the whole `quantity` node, not the
   `%`** — the capture anchor is on the outer node; should be
   `(quantity "%" @operator)`.  The highlight test asserts the current (wrong)
   behaviour (`queries/highlights.scm`, `test/test_highlights.sh`).
5. **`peerDependenciesMeta` key mismatch** — key is `tree_sitter` (underscores)
   but the peer dep is `tree-sitter` (hyphens); the meta is silently ignored
   (`package.json`).

### Packaging & metadata

6. **Align version across all files** — `Cargo.toml`, `pyproject.toml`,
   `Makefile` say `0.0.1`; `package.json`, `tree-sitter.json` say `1.0.0`.
7. **Align license across all files** — `Cargo.toml`, `pyproject.toml` say MIT;
   `package.json`, `tree-sitter.json` say ISC.

### Queries & documentation

8.  **`queries/README.md` claims sections are foldable** — `folds.scm` has no
    section rule; only frontmatter and block comments are foldable.
9.  **`queries/README.md` lists notes under "Comments"** — `recipe_note` is
    highlighted as `@string`, not `@comment`; should be listed separately.
10. **Sections are parsed as dividers, not containers** — small loss of utility
    for section folding; may need to be reconsidered.

### Code quality

11. **`scan_multiword` / `scan_multiword_require_quantity` duplication** — ~80 %
    shared logic; extract a common helper parameterised on whether a trailing
    `{` is required (`scanner.c`).
### Test coverage

12. **`~notimer @salt`** — non-timer `~` followed by an ingredient on the same
    line (exercises bug #2).
13. **Frontmatter with a blank line in the middle** (exercises bug #3).
14. **Empty file** — minimal smoke case.
15. **`~{5}` — timer with value but no unit.**
16. **`>> key:` — metadata with empty value.**
17. **`@salt{to taste%pinch}` — non-numeric quantity value with a unit.**
18. **Open question in `test/corpus/step.txt` test name** — *"should the comment
    really don't belong to the step?"* — resolve or remove.

## Notable Differences

* A step with multiple lines will have children node(s) per line whereas the rust parser will aggregate consecutive text lines. This is probably acceptable.
* It appears that the Cooklang BNF doesn't actually allow for punctuation (eg. `-`) in `word`s (like ingredient names), but the compiler allows for it. I explicitly put it in this grammar since it seems useful. This is hacky.
* Newline and whitespace characters are handled slightly differently due to the way Tree-Sitter views them.
* The quantity delimiters `{` and `}` are not highlighted as `@punctuation.bracket`.  They are produced by the external scanner (unlike `(` / `)` which appear as anonymous nodes in the grammar), so capturing them in a highlight query would require a dedicated `quantity_open` / `quantity_close` node type.  The trade-off is accepted: adding those nodes would loosen the parse tree for a purely cosmetic gain.

## Testing

Tests use tree-sitter system:

```
tree-sitter test
```

tests are organized by the main aspect they're validating (see files `test/corpus/`):

```
tree-sitter test --file-name ingredient.txt
```

Or we can run test on a restricted scope using a regex against the test names:

```
tree-sitter test -i ingredient
```

### Highlighting

Highlight queries (`queries/highlights.scm`) are tested with:

```
test/test_highlights.sh
```

The script contains inline `.cook` snippets, runs `tree-sitter query` against
each one, and asserts that the expected captures fire.  For interactive spot-checking, `tree-sitter
highlight` works on `.cook` files once the grammar's parent directory is listed
in `parser-directories` in `~/.config/tree-sitter/config.json` (note: the
parent, not the grammar directory itself).

## References

* OUTDATED [Cooklang EBNF](https://github.com/cooklang/spec/blob/main/EBNF.md)
* [Python Tree-Sitter Grammar (Good for looking at newline stuff.)](https://github.com/tree-sitter/tree-sitter-python/blob/master/grammar.js)
