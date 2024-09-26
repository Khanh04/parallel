#include "lexer.h"
#include "utils.h"
#include <cctype>
#include <sstream>
#include <iostream>

Lexer::Lexer(std::istream& is)
    : p_input{&is}, owns_input{false} {
    init();
}

Lexer::Lexer(std::istream* ps)
    : p_input{ps}, owns_input{false} {
    init();
}

void Lexer::init() {
    current_token = get_token();
    current_token_text = token_buffer;
}

void Lexer::advance() {
    if (current_token != Token::Eofsym) {
        current_token = get_token();
        current_token_text = token_buffer;
    }
}

Token Lexer::get_token() {
    std::istream& input = *p_input;
    token_buffer.clear();
    char c = input.get();
    
    // Skip whitespace
    while (isspace(c)) c = input.get();
    if (!input) return Token::Eofsym;

    // Handle identifiers and potential array-like expressions such as arr[i-1]
    if (isalpha(c)) {
        token_buffer = c; // Start token buffer with the first character
        c = input.get();
        while (isalnum(c)) {  // Continue reading alphanumeric characters (identifier part)
            token_buffer += c;
            c = input.get();
        }

        // Check if it's an array-like expression (e.g., arr[i-1])
        if (c == '[') {
            token_buffer += c;  // Add '[' to token buffer
            c = input.get();
            while (c != ']') {  // Continue adding everything inside the brackets
                token_buffer += c;
                c = input.get();
            }
            token_buffer += ']';  // Add closing ']'
            c = input.get();      // Move past the closing ']'

            input.putback(c);     // Return the next character to the stream

            // Return as an array token
            return Token::Array;
        }

        // Put back any character that might be part of the next token
        input.putback(c);

        return Token::Id;  // Treat this as a single identifier token
    }

    // Handle numbers
    if (isdigit(c)) {
        token_buffer = c;
        c = input.get();
        while (isdigit(c)) {
            token_buffer += c;
            c = input.get();
        }
        if (c == '.') {
            token_buffer += c;
            c = input.get();
            while (isdigit(c)) {
                token_buffer += c;
                c = input.get();
            }
        }
        exponent_part(c);
        input.putback(c);
        return Token::Number;
    }
    // Handle decimal numbers starting with '.'
    if (c == '.') {
        token_buffer = c;
        c = input.get();
        if (!isdigit(c)) {
            throw Lexical_error{token_buffer += c};
        }
        while (isdigit(c)) {
            token_buffer += c;
            c = input.get();
        }
        exponent_part(c);
        input.putback(c);
        return Token::Number;
    }

    // Handle single character tokens (symbols and operators)
    token_buffer = c;
    switch (c) {
        case '=':
        case '+':
        case '-':
        case '*':
        case '/':
        case '%':
        case '^':
        case '(':
        case ')':
        case ';':
        case '<':
        case '>':
        case '[':  // Handle array access and putback if needed
        case ']':
        case ',':
            return Token(c);  // Return as a token
    }

    throw Lexical_error{token_buffer};
}

void Lexer::exponent_part(char& c) {
    std::istream& input = *p_input;
    
    if (c != 'e' || c != 'E')
        return;
    
    token_buffer += c;
    c = input.get();
    
    if (c == '+' || c == '-') {
        token_buffer += c;
        c = input.get();
    }
    
    if (!isdigit(c))
        throw Lexical_error{token_buffer += c};
    while (isdigit(c)) {
        token_buffer += c;
        c = input.get();
    }
}
