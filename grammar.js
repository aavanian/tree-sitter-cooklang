module.exports = grammar({
  name: "cooklang",

  externals: ($) => [
    $._newline,
    $.ingredient_name,
    $.cookware_name,
    $.timer_name,
    $.text_content,
    $.note_content,
    $.metadata_key,
    $.metadata_value,
    $.section_name,
    $.comment_line,
    $.comment_block,
    $.recipe_note_text,
    $._whitespace_token,
    $._eof,
  ],

  extras: ($) => [$._whitespace_token, $.comment, $.block_comment],

  word: ($) => $.word,

  conflicts: ($) => [
    [$.ingredient],
    [$.cookware],
    [$.timer],
    [$.recipe, $.frontmatter],
  ],

  rules: {
    recipe: ($) =>
      seq(
        optional($.frontmatter),
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

    frontmatter_content: ($) => repeat1(seq(/[^\n]+/, $._newline)),

    metadata: ($) =>
      seq(field("key", $.metadata_key), ":", field("value", $.metadata_value)),

    section: ($) => field("name", $.section_name),

    step: ($) => repeat1($._step_content),

    _step_content: ($) =>
      choice($.text, $.ingredient, $.cookware, $.timer, $.note),

    text: ($) => $.text_content,

    ingredient: ($) =>
      seq(
        "@",
        field("name", $.ingredient_name),
        optional($.quantity),
        optional($.note),
      ),

    cookware: ($) =>
      seq(
        "#",
        field("name", $.cookware_name),
        optional($.quantity),
        optional($.note),
      ),

    timer: ($) =>
      seq(
        "~",
        optional(field("name", $.timer_name)),
        $.quantity, // Required per Cooklang spec
        optional($.note),
      ),

    quantity: ($) =>
      seq("{", optional(field("amount", $._quantity_content)), "}"),

    _quantity_content: ($) => /[^}]+/,

    note: ($) => seq("(", field("content", $.note_content), ")"),

    comment: ($) => field("content", $.comment_line),

    block_comment: ($) => field("content", $.comment_block),

    recipe_note: ($) => seq(">", optional(field("text", $.recipe_note_text))),

    word: ($) => /[a-zA-Z0-9_\-']+/,
  },
});
