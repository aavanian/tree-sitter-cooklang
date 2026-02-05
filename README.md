# Cooklang Tree-Sitter Grammar

This is a [Tree-Sitter](https://tree-sitter.github.io/) grammar for
[Cooklang](https://cooklang.org/).

## TODO

1. **Align version across all files** — `Cargo.toml`,
   `pyproject.toml`, `Makefile` say `0.0.1`; `package.json`,
   `tree-sitter.json` say `1.0.0`.
2. **Align license across all files** — `Cargo.toml`, `pyproject.toml`
   say MIT; `package.json`, `tree-sitter.json` say ISC.
3. **Sections are parsed as dividers, not containers** — fixing this
   means: change parsing so sections are containers, add a section
   fold rule to `folds.scm`, and document section folding in
   `queries/README.md`.
4. **Strengthen Go and Python binding tests** — the Go test
   (`bindings/go/binding_test.go`) only checks that the language
   loads; it does not parse anything, so it would not catch a broken
   external scanner. No Python test exists at all. Both should parse a
   snippet that exercises the scanner (e.g. `@flour{1%cup}`) and
   assert the expected tree shape.
5. **Check and add language extensions** — Two steps: 1/ tests to
   identify where extensions break the grammar and fix those and 2/
   decide whether some/all extensions require specific parsing.

## Notable Differences

* A step with multiple lines will have children node(s) per line
  whereas the rust parser will aggregate consecutive text lines. This
  is probably acceptable.
* It appears that the Cooklang BNF doesn't actually allow for
  punctuation (eg. `-`) in `word`s (like ingredient names), but the
  compiler allows for it. I explicitly put it in this grammar since it
  seems useful. This is hacky.
* Newline and whitespace characters are handled slightly differently
  due to the way Tree-Sitter views them.
* The quantity delimiters `{` and `}` are not highlighted as
  `@punctuation.bracket`. They are produced by the external scanner
  (unlike `(` / `)` which appear as anonymous nodes in the grammar),
  so capturing them in a highlight query would require a dedicated
  `quantity_open` / `quantity_close` node type. The trade-off is
  accepted: adding those nodes would loosen the parse tree for a
  purely cosmetic gain.
* `%` is exposed as a named `percent_separator` node in the AST. A
  separator (either `%` or a space) is required between value and
  unit; bare concatenation like `{2cups}` is a parse error.

## Testing

Tests use tree-sitter system:

```
tree-sitter test
```

tests are organized by the main aspect they're validating (see files
`test/corpus/`):

```
tree-sitter test --file-name ingredient.txt
```

Or we can run test on a restricted scope using a regex against the
test names:

```
tree-sitter test -i ingredient
```

### Highlighting

Highlight queries (`queries/highlights.scm`) are tested with:

```
test/test_highlights.sh
```

The script contains inline `.cook` snippets, runs `tree-sitter query`
against each one, and asserts that the expected captures fire. For
interactive spot-checking, `tree-sitter highlight` works on `.cook`
sfiles once the grammar's parent directory is listed in
`parser-directories` in `~/.config/tree-sitter/config.json` (note: the
parent, not the grammar directory itself).

## Releasing

Before tagging a release, bump the version in every file listed below
to the same value, then create a git tag with that version (e.g.
`v0.1.0`). The Go binding has no explicit version field — it is
versioned by the tag itself, so the tag is the single source of truth
for Go consumers.

| File | Field |
|---|---|
| `Makefile` | `VERSION` (line 1) |
| `package.json` | `version` |
| `tree-sitter.json` | `version` (appears in both `grammar` and `language` blocks) |
| `pyproject.toml` | `version` under `[project]` |
| `Cargo.toml` | `version` under `[package]` |

## References

* OUTDATED [Cooklang EBNF](https://github.com/cooklang/spec/blob/main/EBNF.md)
* [Python Tree-Sitter Grammar (Good for looking at newline stuff.)](https://github.com/tree-sitter/tree-sitter-python/blob/master/grammar.js)
