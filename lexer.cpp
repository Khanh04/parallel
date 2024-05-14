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
    
    while (isspace(c)) c = input.get();
    if (!input) return Token::Eofsym;
    
    if (isalpha(c)) {
        token_buffer = c;
        c = input.get();
        while (isalnum(c) || (c == '[') || (c == ']')) {
            token_buffer += c;
            c = input.get();
        }
        input.putback(c);
        if (token_buffer == "sin") return Token::Sin;
        if (token_buffer == "cos") return Token::Cos;
        if (token_buffer == "tan") return Token::Tan;
        if (token_buffer == "asin") return Token::Asin;
        if (token_buffer == "acos") return Token::Acos;
        if (token_buffer == "atan") return Token::Atan;
        if (token_buffer == "log") return Token::Log;
        if (token_buffer == "exp") return Token::Exp;
        if (token_buffer == "log10") return Token::Log10;
        if (token_buffer == "exp10") return Token::Exp10;
        if (token_buffer == "sqrt") return Token::Sqrt;
        if (token_buffer == "int") return Token::Int;
        return Token::Id;
    }
    
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
        case '[':
        case ']':
        case ',':
        return Token(c);
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
