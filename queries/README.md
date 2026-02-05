# Tree-sitter Cooklang Queries

This directory contains query files that enable various editor features for Cooklang files.

## Query Files

### `highlights.scm`
Provides syntax highlighting for:
- Comments (single line and block)
- Recipe notes
- Section headers
- Metadata
- Ingredients, cookware, and timers
- Recipe references (with special path highlighting)
- Numbers and units
- Punctuation

### `injections.scm`
- Injects YAML syntax highlighting into frontmatter sections

### `folds.scm`
Enables code folding for:
- Frontmatter
- Block comments

## Usage

These queries are automatically used by editors that support tree-sitter, including:
- Neovim (with nvim-treesitter)
- Helix
- Zed
- Emacs (with tree-sitter modes)

## Customization

You can customize the highlighting by modifying the capture names in `highlights.scm`. 
Common capture names include:
- `@comment`
- `@string`
- `@number`
- `@variable`
- `@keyword`
- `@punctuation`
- `@type`

Refer to your editor's documentation for the full list of supported capture names.
