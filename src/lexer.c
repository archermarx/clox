#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "lexer.h"

typedef struct {
    const char* start;
    const char* current;
    int line;
} Lexer;

Lexer lexer;

void init_lexer(const char *source) {
    lexer.start = source;
    lexer.current = source;
    lexer.line = 1;
}

static bool is_at_end() {
    return *lexer.current == '\0';
}

static Token make_token(TokenType type) {
    return (Token) { 
        .type = type,
        .start = lexer.start,
        .length = (int)(lexer.current - lexer.start),
        .line = lexer.line,
    };
}

static Token error_token(const char *message) {
    return (Token) {
        .type = TOKEN_ERROR,
        .start = message,
        .length = (int) strlen(message),
        .line = lexer.line,
    };
}

static char advance() {
    lexer.current++;
    return lexer.current[-1];
}

static bool match(char expected) {
    if (is_at_end()) return false;
    if (*lexer.current != expected) return false;
    lexer.current++;
    return true;
}

static char peek() {
    return *lexer.current;
}

static char peek_next() {
    if (is_at_end()) return '\0';
    return lexer.current[1];
}

static void skip_whitespace() {
    for (;;) {
        char c = peek();
        switch(c) {
        case ' ':
        case '\r':
        case '\t':
            advance();
            break;
        case '\n':
            lexer.line++;
            advance();
            break;
        case '/':
            if (peek_next() == '/') {
                // A comment goes until the end of the line
                while (peek() != '\n' && !is_at_end()) advance();
            } else {
                return;
            }
            break;
        default:
            return;
        }
    }
}

static Token string() {
    while (peek() != '"' && !is_at_end()) {
        if (peek() == '\n') lexer.line++;
        advance();
    }

    if (is_at_end()) return error_token("Unterminated string.");

    // Closing quote
    advance();
    return make_token(TOKEN_STRING);
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static Token number() {
    char c;
    while (is_digit(c = peek()) || c == '_') advance();

    bool has_fraction = false;
    // Look for a fractional part
    if (peek() == '.') {
        // Consume the decimal point
        advance();
        // Check for valid fractional part
        if (!is_digit(peek())) {
            return error_token("Expect digit after decimal point.");
        }
        has_fraction = true;
        while (is_digit(c = peek()) || c == '_') advance();
    }

    // Look for an exponent
    bool has_exponent = false;
    if ((c = peek()) == 'e' || c == 'E') {   

        // Consume exponent
        advance();
        // Check for valid exponent
        c = peek();
        if (!(is_digit(c) || c == '+' || c == '-')) {
            return error_token("Expect number after exponent.");
        }
    
        has_exponent = true;
        if (!is_digit(c)) {
            // we have a + or -
            advance();
            // need a digit after that
            if (!is_digit(peek())) return error_token("Expect number after exponent.");
        }

        while (is_digit(c = peek()) || c == '_') advance();
    }

    if (has_fraction || has_exponent) {
        return make_token(TOKEN_FLOAT64);
    } else {
        return make_token(TOKEN_INT);
    }
}

static TokenType check_keyword(int start, int length, const char *rest, TokenType type) {
    if ((lexer.current - lexer.start == start + length) && memcmp(lexer.start + start, rest, (size_t)length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifier_type() {
    switch(lexer.start[0]) {
        case 'a': return check_keyword(1, 2, "nd", TOKEN_AND);
        case 'b': return check_keyword(1, 4, "reak", TOKEN_BREAK);
        case 'c': return check_keyword(1, 4, "lass", TOKEN_CLASS);
        case 'e': return check_keyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (lexer.current - lexer.start > 1) {
                switch(lexer.start[1]) {
                    case 'a': return check_keyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return check_keyword(2, 1, "r", TOKEN_FOR);
                    case 'u': return check_keyword(2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i': return check_keyword(1, 1, "f", TOKEN_IF);
        case 'n': return check_keyword(1, 2, "il", TOKEN_NIL);
        case 'o': return check_keyword(1, 1, "r", TOKEN_OR);
        case 'r': return check_keyword(1, 5, "eturn", TOKEN_RETURN);
        case 's': return check_keyword(1, 4, "uper", TOKEN_SUPER);
        case 't': 
            if (lexer.current - lexer.start > 1) {
                switch(lexer.start[1]) {
                    case 'h': return check_keyword(2, 2, "is", TOKEN_THIS);
                    case 'r': return check_keyword(2, 2, "ue", TOKEN_TRUE);
                }
            } break;
        case 'v': return check_keyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return check_keyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier() {
    char c;
    while (is_alpha(c = peek()) || is_digit(c)) advance();
    return make_token(identifier_type());
}

Token lex_token(void) {
    skip_whitespace();
    lexer.start = lexer.current;

    if (is_at_end()) return make_token(TOKEN_EOF);

    char c = advance();

    if (is_alpha(c)) return identifier();
    if (is_digit(c)) return number();

    switch (c) {
        // single-character tokens
        case '(': return make_token(TOKEN_LEFT_PAREN);
        case ')': return make_token(TOKEN_RIGHT_PAREN);
        case '{': return make_token(TOKEN_LEFT_BRACE);
        case '}': return make_token(TOKEN_RIGHT_BRACE);
        case ';': return make_token(TOKEN_SEMICOLON);
        case ',': return make_token(TOKEN_COMMA);
        case '.': return make_token(TOKEN_DOT);
        case '-': return make_token(TOKEN_MINUS);
        case '+': return make_token(TOKEN_PLUS);
        case '/': return make_token(TOKEN_SLASH);
        case '*': return make_token(TOKEN_STAR);
        // multi-character tokens
        case '!': return make_token(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=': return make_token(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '>': return make_token(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '<': return make_token(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        // literals
        case '"': return string();
    }

    return error_token("Unexpected character.");
}