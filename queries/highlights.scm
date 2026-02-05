; Cooklang syntax highlighting queries

; Comments
(comment) @comment
(block_comment) @comment

; Recipe notes (user-facing, distinct from comments)
(recipe_note ">" @punctuation.special)
(recipe_note text: (recipe_note_text) @string)

; Sections
(section_name) @tag

; Metadata
(metadata key: (metadata_key) @keyword)
(metadata ":" @punctuation.delimiter)
(metadata value: (metadata_value) @string)

; Frontmatter
(frontmatter_content) @string

; Ingredients
"@" @punctuation.special
(ingredient name: (ingredient_name) @constant)
(ingredient name: (recipe_reference) @string.special)

; Cookware
"#" @punctuation.special
(cookware name: (cookware_name) @constant)

; Timer
"~" @punctuation.special
(timer name: (timer_name) @constant)

; Amounts
(quantity_value) @number
(quantity_unit) @type
(quantity "%") @operator

; Preparations
(preparation content: (preparation_content) @string)

; Brackets
"(" @punctuation.bracket
")" @punctuation.bracket

; Delimiters
"---" @punctuation.delimiter
