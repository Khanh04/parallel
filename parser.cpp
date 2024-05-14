#include "parser.h"
#include "utils.h"
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <map>
#include <simd/math.h>

using namespace std;

std::string Parser::_lhsToken;
std::vector<std::string> *Parser::_dependsOnList = NULL;
std::map<std::string, double> symbol_table;

Parser::Parser() {
    symbol_table["pi"] = 4.0 * atan(1.0);
    symbol_table["e"] = exp(1.0);
}

// Parsing the expression given in s
double Parser::operator()(const std::string& s) {
    std::istringstream ist{s};
    p_lexer = new Lexer{ist};
    double result = assign_expr();
    delete p_lexer;
    return result;
}

double Parser::assign_expr() {
    Token t = p_lexer->get_current_token();
    std::string text = p_lexer->get_token_text();
    
    Parser::_lhsToken = "";
    
    double result = add_expr();
    
    if (p_lexer->get_current_token() == Token::Assign) {
        if (t != Token::Id)
            throw Syntax_error{"target of assignment must be an identifier"};
        
        if (text == "pi" || text == "e")
            throw Syntax_error{"attempt to modify the constant " + text};
        
        p_lexer->advance();
        return symbol_table[text] = add_expr();
    }
    
    return result;
}

double Parser::add_expr() {
    double result = mul_expr();
    
    for (;;) {
        switch (p_lexer->get_current_token()) {
        case Token::Plus:
            p_lexer->advance();
            result += mul_expr();
            break;
        case Token::Minus:
            p_lexer->advance();
            result -= mul_expr();
        default:
            return result;
        }
    }
}

// ... (Remaining functions from the original code)
