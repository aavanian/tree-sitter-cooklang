# Cooklang Tree-Sitter Grammar

This is a [Tree-Sitter](https://tree-sitter.github.io/) grammar for [Cooklang](https://cooklang.org/).

## TODO

* [ ] BUG: fix ERROR when frontmatter.
* [ ] BUG: fix multiline steps.
* [ ] BUG: parenthesis not following a timer/ingredient/cookware shouldn't be preparations.
* [ ] BUG: multiple preparations seem to be broken
* [ ] BUG: comment (block or line) on the same line as step fail to parse if the step doesn't include cookware
* [ ] BUG: recipe_note mostly don't work (only > without other following character is recognized as a recipe_note). The parser expect newline then > and text to be a note, on same level as a step but not a step (it's text for the parser) and another newline to end the note.
* [ ] BUG: ~text without closing curly brackets shouldn't be parsed as a timer

* [ ] IMPR: rename ingredient note in preparation(
* [ ] IMPR: make a pass on node names vs parser names)
* [ ] IMPR: frontmatter granularity: show key/value
* [ ] IMPR: the parser has section as containers of everything, except metadata. Even if not section is defined, an nameless one is assumed. Question is then whether tree-sitter need to represent the same or not. On one hand, it doesn't seem useful, on the other, it might bite back in the future
* [ ] IMPR: recipe_reference is not implemented/parsed. This doesn't seem urgent. A first step would be to assign it a different node type for highlighting. Not sure if breaking down the path in component like the parser is necessary
* [ ] IMPR: refine quantity node in amount + optional unit

* [ ] DEPR: >> metadata is deprecated and can't coexist with yaml frontmatter. I don't know if tree-sitter need to handle the deprecation warning. Note that if frontmatter exist, then old-style metadata is parsed as a note... That seems a parser bug. For TS: questions are what to do if both are present?

* [ ] FEAT: Syntax highlighting
* [ ] Properly handle `unreserved_symbol`s.

## Notable Differences

* It appears that the Cooklang BNF doesn't actually allow for punctuation (eg. `-`) in `word`s (like ingredient names), but the compiler allows for it. I explicitly put it in this grammar since it seems useful. This is hacky.
* Newline and whitespace characters are handled slightly differently due to the way Tree-Sitter views them.

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

## References

* [Cooklang EBNF](https://github.com/cooklang/spec/blob/main/EBNF.md)
* [Python Tree-Sitter Grammar (Good for looking at newline stuff.)](https://github.com/tree-sitter/tree-sitter-python/blob/master/grammar.js)
