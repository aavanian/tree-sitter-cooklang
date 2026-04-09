#include "tree_sitter/parser.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

enum TokenType {
    NEWLINE,
    STEP_NEWLINE,
    INGREDIENT_NAME,
    RECIPE_REFERENCE,
    COOKWARE_NAME,
    TIMER_NAME,
    TEXT_CONTENT,
    PREPARATION_CONTENT,
    METADATA_KEY,
    METADATA_VALUE,
    SECTION_NAME,
    COMMENT_LINE,
    COMMENT_BLOCK,
    RECIPE_NOTE_TEXT,
    WHITESPACE_TOKEN,
    QUANTITY_CLOSE,
    QUANTITY_VALUE,
    QUANTITY_UNIT,
    PERCENT_SEPARATOR,
    TOKEN_EOF
};

typedef struct {
    bool in_metadata;
    bool at_line_start;
    int paren_depth;
    bool whitespace_since_element;
    bool after_percent;
} Scanner;

static inline bool is_word_char(int32_t c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-' ||
           c == '\'' || c == '"' ||
           c > 127; // Unicode characters
}

static inline bool is_whitespace(int32_t c) {
    return c == ' ' || c == '\t';
}

// When require_quantity is true the scan only succeeds if a '{' is found
// (used for timers); otherwise at least one word is enough (ingredients, cookware).
static bool scan_multiword(TSLexer *lexer, bool require_quantity) {
    if (!is_word_char(lexer->lookahead)) {
        return false;
    }

    while (is_word_char(lexer->lookahead)) {
        lexer->advance(lexer, false);
    }

    if (lexer->lookahead == '{') {
        return true;
    }

    lexer->mark_end(lexer);
    bool found_quantity = false;

    while (is_whitespace(lexer->lookahead)) {
        lexer->advance(lexer, false);

        if (is_word_char(lexer->lookahead)) {
            while (is_word_char(lexer->lookahead)) {
                lexer->advance(lexer, false);
            }

            if (lexer->lookahead == '{') {
                lexer->mark_end(lexer);
                found_quantity = true;
            }
        } else {
            break;
        }
    }

    return !require_quantity || found_quantity;
}

static bool scan_text_until(TSLexer *lexer, const char *delimiters) {
    bool has_content = false;

    while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
        // Check for block comment start [-
        if (lexer->lookahead == '[') {
            lexer->mark_end(lexer);  // Mark position BEFORE bracket
            lexer->advance(lexer, false);
            if (lexer->lookahead == '-') {
                // Block comment start - token ends at mark (before bracket)
                return has_content;
            }
            // Not a block comment, single bracket is part of text
            has_content = true;
            continue;
        }

        // Check for ~ - only stop if followed by valid timer pattern ({...} before newline)
        if (lexer->lookahead == '~') {
            lexer->mark_end(lexer);  // Mark position BEFORE ~

            // Scan ahead to check if { exists before newline
            lexer->advance(lexer, false);

            // Skip past potential timer name (word chars and whitespace only;
            // anything else cannot be part of a name so stop there)
            while (lexer->lookahead != '{' && lexer->lookahead != '\n' && !lexer->eof(lexer)
                   && (is_word_char(lexer->lookahead) || is_whitespace(lexer->lookahead))) {
                lexer->advance(lexer, false);
            }

            if (lexer->lookahead == '{') {
                // Valid timer start - text ends before ~
                return has_content;
            }

            // Not a valid timer - include everything we scanned as text
            has_content = true;
            lexer->mark_end(lexer);
            continue;
        }

        // Check if we hit any delimiter (excluding ~ which is handled above)
        bool is_delimiter = false;
        for (const char *d = delimiters; *d; d++) {
            if (lexer->lookahead == *d) {
                is_delimiter = true;
                break;
            }
        }

        if (is_delimiter) {
            break;
        }

        // Check for comment start (--)
        if (lexer->lookahead == '-') {
            lexer->mark_end(lexer);  // Mark position BEFORE first dash
            lexer->advance(lexer, false);
            if (lexer->lookahead == '-') {
                // Comment start - token ends at mark (before first dash)
                return has_content;
            }
            // Not a comment, single dash is part of text
            has_content = true;
            continue;
        }

        has_content = true;
        lexer->advance(lexer, false);
    }

    if (has_content) {
        lexer->mark_end(lexer);
    }
    return has_content;
}

void *tree_sitter_cooklang_external_scanner_create() {
    Scanner *scanner = malloc(sizeof(Scanner));
    scanner->in_metadata = false;
    scanner->at_line_start = true;
    scanner->paren_depth = 0;
    scanner->whitespace_since_element = true;
    scanner->after_percent = false;
    return scanner;
}

void tree_sitter_cooklang_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    free(scanner);
}

unsigned tree_sitter_cooklang_external_scanner_serialize(void *payload, char *buffer) {
    Scanner *scanner = (Scanner *)payload;
    buffer[0] = scanner->in_metadata;
    buffer[1] = scanner->at_line_start;
    buffer[2] = (char)(scanner->paren_depth & 0xFF);
    buffer[3] = (char)((scanner->paren_depth >> 8) & 0xFF);
    buffer[4] = scanner->whitespace_since_element;
    buffer[5] = scanner->after_percent;
    return 6;
}

void tree_sitter_cooklang_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    if (length >= 5) {
        scanner->in_metadata = buffer[0];
        scanner->at_line_start = buffer[1];
        scanner->paren_depth = (unsigned char)buffer[2] | ((unsigned char)buffer[3] << 8);
        scanner->whitespace_since_element = buffer[4];
    }
    if (length >= 6) {
        scanner->after_percent = buffer[5];
    }
}

bool tree_sitter_cooklang_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;

    // Handle block comments FIRST - they have highest priority and can appear anywhere
    if (lexer->lookahead == '[' && valid_symbols[COMMENT_BLOCK]) {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '-') {
            lexer->advance(lexer, false);

            // Block comment - scan until -]
            while (!lexer->eof(lexer)) {
                if (lexer->lookahead == '-') {
                    lexer->advance(lexer, false);
                    if (lexer->lookahead == ']') {
                        lexer->advance(lexer, false);
                        break;
                    }
                } else {
                    lexer->advance(lexer, false);
                }
            }

            // Don't update at_line_start for block comments
            lexer->result_symbol = COMMENT_BLOCK;
            return true;
        }
    }

    // Handle whitespace as a token (for extras)
    if (is_whitespace(lexer->lookahead) && valid_symbols[WHITESPACE_TOKEN]) {
        lexer->result_symbol = WHITESPACE_TOKEN;
        lexer->advance(lexer, false);
        while (is_whitespace(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }
        scanner->whitespace_since_element = true;
        return true;
    }

    // Skip whitespace if not handling it as a token
    while (is_whitespace(lexer->lookahead)) {
        lexer->advance(lexer, true);
    }

    // Track line starts
    if (lexer->get_column(lexer) == 0) {
        scanner->at_line_start = true;
    }

    // Handle EOF
    if (lexer->eof(lexer)) {
        if (valid_symbols[TOKEN_EOF]) {
            lexer->result_symbol = TOKEN_EOF;
            return true;
        }
        return false;
    }

    // Handle newlines
    if (lexer->lookahead == '\n') {
        lexer->advance(lexer, false);
        scanner->at_line_start = true;

        if (valid_symbols[STEP_NEWLINE]) {
            // Continuation: next line is non-blank and doesn't start a special construct
            bool continuation = !lexer->eof(lexer) &&
                                lexer->lookahead != '\n' &&
                                lexer->lookahead != '=' &&
                                lexer->lookahead != '>' &&
                                lexer->lookahead != '-' &&
                                lexer->lookahead != '[';
            if (continuation) {
                lexer->result_symbol = STEP_NEWLINE;
                return true;
            }
        }

        if (valid_symbols[NEWLINE]) {
            lexer->result_symbol = NEWLINE;
            return true;
        }
        return false;
    }

    // Handle recipe note text (after '>' has been matched by grammar)
    // The grammar has: recipe_note: $ => seq('>', optional($.recipe_note_text))
    // So when RECIPE_NOTE_TEXT is valid, we're positioned after '>'
    //
    // Recipe notes can span multiple lines:
    // - Lines starting with '>' continue the note
    // - Lines without '>' also continue the note
    // - A blank line (empty line) ends the note
    if (valid_symbols[RECIPE_NOTE_TEXT]) {
        // Skip optional whitespace after '>'
        while (is_whitespace(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }

        bool has_content = false;

        while (!lexer->eof(lexer)) {
            // Scan content until end of line
            while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
                has_content = true;
                lexer->advance(lexer, false);
            }

            if (lexer->eof(lexer)) {
                break;
            }

            // We hit a newline - check if note continues
            lexer->advance(lexer, false);  // consume the newline

            // Check for blank line (another newline = end of note)
            if (lexer->lookahead == '\n' || lexer->eof(lexer)) {
                break;
            }

            // Check for continuation
            // If next line starts with '>', it's a continuation - skip the '>'
            // If next line starts with other content, it's also a continuation
            if (lexer->lookahead == '>') {
                lexer->advance(lexer, false);  // skip the '>'
                // Skip optional whitespace after '>'
                while (is_whitespace(lexer->lookahead)) {
                    lexer->advance(lexer, false);
                }
            }
        }

        // Only return a token if we have content
        if (has_content) {
            scanner->at_line_start = false;
            lexer->result_symbol = RECIPE_NOTE_TEXT;
            return true;
        }
        // No text content - recipe_note_text is optional, so return false
        return false;
    }

    // Handle metadata (at start of line with >>)
    if (scanner->at_line_start && lexer->lookahead == '>' && valid_symbols[METADATA_KEY]) {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '>') {
            lexer->advance(lexer, false);

            // Skip whitespace
            while (is_whitespace(lexer->lookahead)) {
                lexer->advance(lexer, false);
            }

            // Scan metadata key (can be multi-word); reject all-whitespace keys
            bool has_non_ws = false;
            while (!lexer->eof(lexer) && lexer->lookahead != ':' && lexer->lookahead != '\n') {
                has_non_ws = has_non_ws || !is_whitespace(lexer->lookahead);
                lexer->advance(lexer, false);
            }

            if (has_non_ws && lexer->lookahead == ':') {
                scanner->in_metadata = true;
                scanner->at_line_start = false;
                lexer->result_symbol = METADATA_KEY;
                return true;
            }
        }
        // Single '>' at line start - not metadata, let grammar handle it as recipe_note
        // Return false to allow tree-sitter to reset lexer and try other paths
        return false;
    }

    // Handle metadata value (after colon in metadata line)
    if (scanner->in_metadata && valid_symbols[METADATA_VALUE]) {
        // Skip the colon if present
        if (lexer->lookahead == ':') {
            lexer->advance(lexer, false);
        }

        // Skip whitespace
        while (is_whitespace(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }

        // Scan until end of line
        bool has_content = false;
        while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
            has_content = true;
            lexer->advance(lexer, false);
        }

        scanner->in_metadata = false;
        if (has_content) {
            lexer->result_symbol = METADATA_VALUE;
            return true;
        }
    }

    // Handle section headers (at start of line with =)
    if (scanner->at_line_start && lexer->lookahead == '=' && valid_symbols[SECTION_NAME]) {
        int equals_count = 0;
        while (lexer->lookahead == '=') {
            equals_count++;
            lexer->advance(lexer, false);
        }

        // Skip whitespace
        while (is_whitespace(lexer->lookahead)) {
            lexer->advance(lexer, false);
        }

        // Scan section name
        while (!lexer->eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '=') {
            lexer->advance(lexer, false);
        }

        // Skip trailing equals
        while (lexer->lookahead == '=' || is_whitespace(lexer->lookahead)) {
            if (lexer->lookahead == '\n') break;
            lexer->advance(lexer, false);
        }

        scanner->at_line_start = false;
        lexer->result_symbol = SECTION_NAME;
        return true;
    }

    // Handle comments (both at line start and inline)
    if (lexer->lookahead == '-' && valid_symbols[COMMENT_LINE]) {
        // Check if this could be frontmatter (--- at line start)
        if (scanner->at_line_start && lexer->get_column(lexer) == 0) {
            // Peek ahead to see if it's ---
            int dash_count = 0;
            while (lexer->lookahead == '-' && dash_count < 3) {
                lexer->advance(lexer, false);
                dash_count++;
            }

            // If we have exactly --- followed by newline or whitespace, it's frontmatter
            if (dash_count == 3 && (lexer->lookahead == '\n' || lexer->eof(lexer))) {
                // Don't consume this as a comment, let the grammar handle it
                return false;
            }

            // Otherwise, reprocess as comment
            // We've already consumed some dashes, so include them in the comment
            if (dash_count >= 2) {
                // Skip optional space after --
                while (is_whitespace(lexer->lookahead)) {
                    lexer->advance(lexer, false);
                }

                // Continue with rest of line
                while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
                    lexer->advance(lexer, false);
                }

                scanner->at_line_start = false;
                lexer->result_symbol = COMMENT_LINE;
                return true;
            }
            return false;
        }

        // Normal comment handling
        lexer->advance(lexer, false);
        if (lexer->lookahead == '-') {
            lexer->advance(lexer, false);

            // Skip optional space after --
            while (is_whitespace(lexer->lookahead)) {
                lexer->advance(lexer, false);
            }

            // Line comment
            while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
                lexer->advance(lexer, false);
            }

            scanner->at_line_start = false;
            lexer->result_symbol = COMMENT_LINE;
            return true;
        }
    }


    // Handle recipe references (after @, starts with ./ or .\)
    if (valid_symbols[RECIPE_REFERENCE] && lexer->lookahead == '.') {
        lexer->advance(lexer, false);

        if (lexer->lookahead == '/' || lexer->lookahead == '\\') {
            lexer->advance(lexer, false);

            // Consume path characters
            while (!lexer->eof(lexer) && lexer->lookahead != '{' &&
                   lexer->lookahead != '(' && lexer->lookahead != '\n' &&
                   lexer->lookahead != '@' && lexer->lookahead != '#' &&
                   lexer->lookahead != '~') {
                lexer->advance(lexer, false);
            }

            scanner->at_line_start = false;
            scanner->whitespace_since_element = false;
            lexer->result_symbol = RECIPE_REFERENCE;
            return true;
        }
    }

    // Handle ingredient names (after @)
    if (valid_symbols[INGREDIENT_NAME]) {
        if (scan_multiword(lexer, false)) {
            scanner->at_line_start = false;
            scanner->whitespace_since_element = false;
            lexer->result_symbol = INGREDIENT_NAME;
            return true;
        }
    }

    // Handle cookware names (after #)
    if (valid_symbols[COOKWARE_NAME]) {
        if (scan_multiword(lexer, false)) {
            scanner->at_line_start = false;
            scanner->whitespace_since_element = false;
            lexer->result_symbol = COOKWARE_NAME;
            return true;
        }
    }

    // Handle timer names (after ~) - only match if followed by '{'
    if (valid_symbols[TIMER_NAME]) {
        if (scan_multiword(lexer, true)) {
            scanner->at_line_start = false;
            scanner->whitespace_since_element = false;
            lexer->result_symbol = TIMER_NAME;
            return true;
        }
    }

    // Scan the numeric or non-numeric value inside a quantity (before unit or close)
    if (valid_symbols[QUANTITY_VALUE]) {
        if (lexer->lookahead == '}' || lexer->lookahead == '\n' || lexer->eof(lexer)) {
            // guard: empty quantity — let optional not be taken
        } else if (lexer->lookahead < '0' || lexer->lookahead > '9') {
            // Non-numeric value (e.g. "to taste"): scan until '%' or '}',
            // trimming trailing whitespace via mark_end.
            bool has_content = false;
            while (!lexer->eof(lexer) && lexer->lookahead != '}' &&
                   lexer->lookahead != '%' && lexer->lookahead != '\n') {
                if (!is_whitespace(lexer->lookahead)) {
                    lexer->advance(lexer, false);
                    lexer->mark_end(lexer);
                    has_content = true;
                } else {
                    lexer->advance(lexer, false);
                }
            }
            if (has_content) {
                lexer->result_symbol = QUANTITY_VALUE;
                return true;
            }
        } else {
            // Numeric path: scan integer part
            while (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
                lexer->advance(lexer, false);
            }
            lexer->mark_end(lexer); // tentative end = integer

            if (lexer->lookahead == '.') {
                lexer->advance(lexer, false); // consume '.'
                if (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
                    while (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
                        lexer->advance(lexer, false);
                    }
                    lexer->mark_end(lexer); // decimal number
                }
                lexer->result_symbol = QUANTITY_VALUE;
                return true;
            }

            // Peek past whitespace (no mark_end — peeking)
            while (is_whitespace(lexer->lookahead)) {
                lexer->advance(lexer, false);
            }

            if (lexer->lookahead == '/') {
                // Simple fraction: "5 / 2" or "5/2"
                lexer->advance(lexer, false); // consume '/'
                while (is_whitespace(lexer->lookahead)) {
                    lexer->advance(lexer, false);
                }
                if (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
                    while (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
                        lexer->advance(lexer, false);
                    }
                    lexer->mark_end(lexer); // confirmed fraction
                }
                lexer->result_symbol = QUANTITY_VALUE;
                return true;
            }

            if (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
                // Mixed fraction candidate: "1 1/2"
                // We've already peeked past whitespace. Save state by scanning
                // the second integer without marking end yet.
                while (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
                    lexer->advance(lexer, false);
                }
                if (lexer->lookahead == '/') {
                    lexer->advance(lexer, false); // consume '/'
                    if (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
                        while (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
                            lexer->advance(lexer, false);
                        }
                        lexer->mark_end(lexer); // confirmed mixed fraction
                    }
                }
                // If no '/' or no denominator digits, mark_end stays at first integer.
                // Lexer resets to that position.
                lexer->result_symbol = QUANTITY_VALUE;
                return true;
            }

            // No fraction pattern — return with mark_end at the integer
            lexer->result_symbol = QUANTITY_VALUE;
            return true;
        }
    }

    // Consume the '%' separator between quantity value and unit.
    // Using an external token lets the scanner track that '%' was consumed
    // so QUANTITY_UNIT can require an explicit separator.
    if (valid_symbols[PERCENT_SEPARATOR] && lexer->lookahead == '%') {
        lexer->advance(lexer, false);
        lexer->mark_end(lexer);
        scanner->after_percent = true;
        lexer->result_symbol = PERCENT_SEPARATOR;
        return true;
    }

    // Scan the unit part inside a quantity (after value, before close brace).
    // Requires either a '%' separator or at least one space before the unit,
    // so that {2cups} (no separator) is rejected as invalid.
    if (valid_symbols[QUANTITY_UNIT]) {
        bool has_separator = scanner->whitespace_since_element || scanner->after_percent;
        if (has_separator && lexer->lookahead != '}' && lexer->lookahead != '%' &&
            lexer->lookahead != '\n' && !lexer->eof(lexer)) {
            bool has_content = false;
            while (!lexer->eof(lexer) && lexer->lookahead != '}' && lexer->lookahead != '\n') {
                if (!is_whitespace(lexer->lookahead)) {
                    lexer->advance(lexer, false);
                    lexer->mark_end(lexer);
                    has_content = true;
                } else {
                    lexer->advance(lexer, false);
                }
            }
            if (has_content) {
                scanner->after_percent = false;
                lexer->result_symbol = QUANTITY_UNIT;
                return true;
            }
        }
        // Fall through to QUANTITY_CLOSE if no separator or no content found
    }

    // Handle quantity close brace - tracked externally to reset whitespace flag
    if (valid_symbols[QUANTITY_CLOSE] && lexer->lookahead == '}') {
        lexer->advance(lexer, false);
        scanner->whitespace_since_element = false;
        scanner->after_percent = false;
        lexer->result_symbol = QUANTITY_CLOSE;
        return true;
    }

    // Handle preparation content (inside parentheses, only immediately after element)
    if (valid_symbols[PREPARATION_CONTENT]) {
        if (scanner->whitespace_since_element) {
            return false;
        }
        bool has_content = false;
        int paren_depth = scanner->paren_depth;

        while (!lexer->eof(lexer)) {
            if (lexer->lookahead == '(') {
                paren_depth++;
                has_content = true;
                lexer->advance(lexer, false);
            } else if (lexer->lookahead == ')') {
                if (paren_depth == 0) {
                    // End of note
                    break;
                }
                paren_depth--;
                has_content = true;
                lexer->advance(lexer, false);
            } else if (lexer->lookahead == '\n') {
                // Notes can't span lines in standard Cooklang
                break;
            } else {
                has_content = true;
                lexer->advance(lexer, false);
            }
        }

        if (has_content) {
            scanner->at_line_start = false;
            scanner->whitespace_since_element = true;
            lexer->result_symbol = PREPARATION_CONTENT;
            return true;
        }
    }

    // Handle plain text content
    if (valid_symbols[TEXT_CONTENT]) {
        // Don't start text with special line starters at column 0
        // Use get_column directly instead of at_line_start flag which can be stale
        if (lexer->get_column(lexer) == 0) {
            // These characters start special constructs, not text:
            // '-' could be comment (--) or frontmatter (---)
            // '=' is section header
            // '[' could be block comment ([-)
            if (lexer->lookahead == '-' || lexer->lookahead == '=' ||
                lexer->lookahead == '[') {
                return false;
            }
            if (lexer->lookahead == '>') {
                lexer->advance(lexer, false);
                if (lexer->lookahead != '>') {
                    // Single '>' — defer to recipe_note
                    return false;
                }
                if (valid_symbols[METADATA_KEY]) {
                    // '>>' with metadata valid — defer to metadata handler
                    return false;
                }
                // '>>' with metadata not valid (frontmatter present):
                // first '>' already consumed; fall through to scan_text_until
                // which picks up from the second '>'.  Token start is col 0.
            }
        }

        // Stop at ( only when immediately after an element — preparation may follow.
        // Otherwise ( is ordinary text.
        const char *delimiters = scanner->whitespace_since_element ? "@#{}" : "@#{}(";
        if (scan_text_until(lexer, delimiters)) {
            scanner->at_line_start = false;
            scanner->whitespace_since_element = true;
            lexer->result_symbol = TEXT_CONTENT;
            return true;
        }
    }

    return false;
}
