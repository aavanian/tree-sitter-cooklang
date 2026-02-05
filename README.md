# Cooklang Tree-Sitter Grammar

This is a [Tree-Sitter](https://tree-sitter.github.io/) grammar for [Cooklang](https://cooklang.org/).

## TODO

### Bugs

1. **Go and Python bindings missing `scanner.c`** — `bindings/go/binding.go` and
   `setup.py` both include only `parser.c`.  The external scanner is required;
   without it both bindings produce a broken parser at runtime.

### Packaging & metadata

2. **Align version across all files** — `Cargo.toml`, `pyproject.toml`,
   `Makefile` say `0.0.1`; `package.json`, `tree-sitter.json` say `1.0.0`.
3. **Align license across all files** — `Cargo.toml`, `pyproject.toml` say MIT;
   `package.json`, `tree-sitter.json` say ISC.

### Queries & documentation

4. **Sections are parsed as dividers, not containers** — fixing this means:
   change parsing so sections are containers, add a section fold rule to
   `folds.scm`, and document section folding in `queries/README.md`.


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
