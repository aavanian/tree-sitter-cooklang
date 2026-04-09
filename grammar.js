module.exports = grammar({
  name: "cooklang",

  externals: ($) => [
    $._newline,
    $._step_newline,
    $.ingredient_name,
    $.recipe_reference,
    $.cookware_name,
    $.timer_name,
    $.text_content,
    $.preparation_content,
    $.metadata_key,
    $.metadata_value,
    $.section_name,
    $.comment_line,
    $.comment_block,
    $.recipe_note_text,
    $._whitespace_token,
    $._quantity_close,
    $.quantity_value,
    $.quantity_unit,
    $.percent_separator,
    $._eof,
  ],

  extras: ($) => [$._whitespace_token, $.comment, $.block_comment],

  word: ($) => $.word,

  conflicts: ($) => [[$.recipe, $.frontmatter]],

  rules: {
    recipe: ($) =>
      choice(
        seq(
          $.frontmatter,
          repeat(
            choice(
              seq($.section, $._newline),
              seq($.step, $._newline),
              seq($.recipe_note, $._newline),
              $._newline,
            ),
          ),
          optional(choice($.section, $.step, $.recipe_note)),
        ),
        seq(
          repeat(
            choice(
              seq($.metadata, $._newline),
              seq($.section, $._newline),
              seq($.step, $._newline),
              seq($.recipe_note, $._newline),
              $._newline,
            ),
          ),
          optional(choice($.metadata, $.section, $.step, $.recipe_note)),
        ),
      ),

    // Frontmatter allows optional leading blank lines (for test format compatibility)
    frontmatter: ($) =>
      seq(
        repeat($._newline),
        token(prec(10, "---")),
        $._newline,
        optional($.frontmatter_content),
        token(prec(10, "---")),
        $._newline,
      ),

    frontmatter_content: ($) => repeat1(seq(/[^\n]*/, $._newline)),

    metadata: ($) =>
      seq(field("key", $.metadata_key), ":", optional(field("value", $.metadata_value))),

    section: ($) => field("name", $.section_name),

    step: ($) =>
      seq(
        repeat1($._step_content),
        repeat(seq($._step_newline, repeat1($._step_content))),
      ),

    _step_content: ($) =>
      choice($.text, $.ingredient, $.cookware, $.timer),

    text: ($) => $.text_content,

    ingredient: ($) =>
      seq(
        "@",
        field("name", choice($.ingredient_name, $.recipe_reference)),
        optional($.quantity),
        optional($.preparation),
      ),

    cookware: ($) =>
      seq(
        "#",
        field("name", $.cookware_name),
        optional($.quantity),
        optional($.preparation),
      ),

    timer: ($) =>
      seq(
        "~",
        optional(field("name", $.timer_name)),
        $.quantity, // Required per Cooklang spec
        optional($.preparation),
      ),

    quantity: ($) =>
      seq(
        "{",
        optional(
          seq(
            field("value", $.quantity_value),
            optional(seq(optional($.percent_separator), field("unit", $.quantity_unit))),
          ),
        ),
        $._quantity_close,
      ),

    preparation: ($) => seq("(", field("content", $.preparation_content), ")"),

    comment: ($) => field("content", $.comment_line),

    block_comment: ($) => field("content", $.comment_block),

    recipe_note: ($) => seq(">", optional(field("text", $.recipe_note_text))),

    word: ($) => /[a-zA-Z0-9_\-']+/,
  },
});
