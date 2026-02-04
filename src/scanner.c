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
    TOKEN_EOF
};

typedef struct {
    bool in_metadata;
    bool at_line_start;
    int paren_depth;
    bool whitespace_since_element;
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

static bool scan_multiword(TSLexer *lexer) {
    // First word
    if (!is_word_char(lexer->lookahead)) {
        return false;
    }

    while (is_word_char(lexer->lookahead)) {
        lexer->advance(lexer, false);
    }

    // Check if we have a quantity/unit pattern immediately following
    if (lexer->lookahead == '{') {
        return true;
    }

    // Save position after first word
    lexer->mark_end(lexer);

    // Look ahead for more words
    while (is_whitespace(lexer->lookahead)) {
        lexer->advance(lexer, false);

        // After whitespace, check for another word
        if (is_word_char(lexer->lookahead)) {
            // Continue collecting the word
            while (is_word_char(lexer->lookahead)) {
                lexer->advance(lexer, false);
            }

            // Update end position if we find a quantity
            if (lexer->lookahead == '{') {
                lexer->mark_end(lexer);
            }
        } else {
            break;
        }
    }

    return true;
}

// Variant that only succeeds if followed by '{' (for timer names where quantity is required)
static bool scan_multiword_require_quantity(TSLexer *lexer) {
    if (!is_word_char(lexer->lookahead)) {
        return false;
    }

    while (is_word_char(lexer->lookahead)) {
        lexer->advance(lexer, false);
    }

    // Check if we have a quantity immediately following
    if (lexer->lookahead == '{') {
        return true;
    }

    // Save position after first word
    lexer->mark_end(lexer);
    bool found_quantity = false;

    // Look ahead for more words, but only accept if we find a '{'
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

    return found_quantity;
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

            // Skip past potential timer name
            while (lexer->lookahead != '{' && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
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
    buffer[2] = scanner->paren_depth;
    buffer[3] = scanner->whitespace_since_element;
    return 4;
}

void tree_sitter_cooklang_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
    Scanner *scanner = (Scanner *)payload;
    if (length >= 4) {
        scanner->in_metadata = buffer[0];
        scanner->at_line_start = buffer[1];
        scanner->paren_depth = buffer[2];
        scanner->whitespace_since_element = buffer[3];
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

            if (has_non_ws) {
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

        if (has_content) {
            scanner->in_metadata = false;
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

        if (equals_count > 0) {
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
        if (scan_multiword(lexer)) {
            scanner->at_line_start = false;
            scanner->whitespace_since_element = false;
            lexer->result_symbol = INGREDIENT_NAME;
            return true;
        }
    }

    // Handle cookware names (after #)
    if (valid_symbols[COOKWARE_NAME]) {
        if (scan_multiword(lexer)) {
            scanner->at_line_start = false;
            scanner->whitespace_since_element = false;
            lexer->result_symbol = COOKWARE_NAME;
            return true;
        }
    }

    // Handle timer names (after ~) - only match if followed by '{'
    if (valid_symbols[TIMER_NAME]) {
        if (scan_multiword_require_quantity(lexer)) {
            scanner->at_line_start = false;
            scanner->whitespace_since_element = false;
            lexer->result_symbol = TIMER_NAME;
            return true;
        }
    }

    // Handle quantity close brace - tracked externally to reset whitespace flag
    if (valid_symbols[QUANTITY_CLOSE] && lexer->lookahead == '}') {
        lexer->advance(lexer, false);
        scanner->whitespace_since_element = false;
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
            // '>' is recipe_note or metadata (>>)
            if (lexer->lookahead == '-' || lexer->lookahead == '=' ||
                lexer->lookahead == '[' || lexer->lookahead == '>') {
                return false;
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
