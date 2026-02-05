#!/bin/bash
# Highlight-query tests for tree-sitter-cooklang.
#
# Each test writes a small .cook snippet, runs `tree-sitter query` with the
# highlights.scm file, and asserts that specific (text, capture-name) pairs
# appear in the output.  The script is intentionally kept at the grammar root
# so that tree-sitter can locate the language via the current directory.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GRAMMAR_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
QUERY="$GRAMMAR_DIR/queries/highlights.scm"
WORK="$GRAMMAR_DIR/_hl_test.cook"

passed=0
failed=0

cleanup() { rm -f "$WORK"; }
trap cleanup EXIT

# Run the highlight query and capture its output.
run_query() {
    tree-sitter query "$QUERY" "$WORK" -p "$GRAMMAR_DIR" 2>&1
}

# Assert that the query output contains a capture line matching the given
# capture name on the given literal text.
# Usage: expect <capture-name> <text>
expect() {
    local capture_name="$1"
    local text="$2"
    # Each capture is a single line in one of two formats:
    #   capture: N - <name>, start: ..., end: ..., text: `<text>`
    #   capture: <name>, start: ..., end: ...          (multi-line tokens)
    # tree-sitter omits the text field for tokens that span multiple lines.
    # grep -F -- stops option processing so leading dashes in text are safe.
    local matching_lines
    matching_lines="$(echo "$OUTPUT" | grep -F -- "$capture_name,")" || return 1
    if echo "$matching_lines" | grep -qF -- "text: \`$text\`"; then
        return 0
    fi
    # For multi-line tokens tree-sitter elides the text; accept if the capture
    # is present and text is empty (caller uses "" as a sentinel).
    [ -z "$text" ] && return 0
    return 1
}

run_test() {
    local name="$1"
    local input="$2"
    shift 2
    # Remaining args are pairs: capture_name text capture_name text ...

    printf '%s' "$input" > "$WORK"
    OUTPUT="$(run_query)"

    local ok=true
    while [ $# -ge 2 ]; do
        local cap="$1" txt="$2"
        shift 2
        if ! expect "$cap" "$txt"; then
            if $ok; then
                echo "FAIL: $name"
                ok=false
            fi
            echo "  missing capture '$cap' on text '$txt'"
            echo "  actual output:"
            echo "$OUTPUT" | sed 's/^/    /'
        fi
    done

    if $ok; then
        echo "ok:   $name"
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
}

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

run_test "line comment" \
    "-- a comment" \
    comment "-- a comment"

run_test "block comment" \
    "[- a block comment -]" \
    comment "[- a block comment -]"

run_test "recipe note sigil and text" \
    "> a note
" \
    punctuation.special ">" \
    string ""

run_test "metadata key, colon, and value" \
    ">> serves: 4
" \
    keyword ">> serves" \
    punctuation.delimiter ":" \
    string "4"

run_test "frontmatter delimiters and content" \
    "---
title: Test
---
" \
    punctuation.delimiter "---" \
    string ""

run_test "section name" \
    "= Dessert =
" \
    tag "= Dessert ="

run_test "ingredient sigil and name" \
    "@flour
" \
    punctuation.special "@" \
    constant "flour"

run_test "ingredient with quantity" \
    "@flour{2%cups}
" \
    punctuation.special "@" \
    constant "flour" \
    number "2" \
    operator "{2%cups}" \
    type "cups"

run_test "ingredient with preparation" \
    "@flour{1%cup}(sifted)
" \
    constant "flour" \
    punctuation.bracket "(" \
    string "sifted" \
    punctuation.bracket ")"

run_test "recipe reference" \
    "@./other recipe{1%cup}
" \
    punctuation.special "@" \
    string.special "./other recipe" \
    number "1" \
    type "cup"

run_test "cookware sigil and name" \
    "#pan{}
" \
    punctuation.special "#" \
    constant "pan"

run_test "timer sigil, value, and unit" \
    "~{10%minutes}
" \
    punctuation.special "~" \
    number "10" \
    type "minutes"

run_test "named timer" \
    "~rest{30%min}
" \
    punctuation.special "~" \
    constant "rest" \
    number "30" \
    type "min"

run_test "quantity percent operator" \
    "@salt{1%tsp}
" \
    operator "{1%tsp}"

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

total=$((passed + failed))
echo
echo "$passed/$total highlight tests passed"

[ $failed -eq 0 ]
