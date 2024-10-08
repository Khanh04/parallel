#include "parser.h"
#include "utils.h"
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cmath>
#include <set>
#include <iostream>
#include <stdio.h>

// Static member initialization
std::string Parser::_lhsToken;
std::set<std::string>* Parser::_dependsOnList = nullptr;
// Symbol table definition
std::map<std::string, double> symbol_table;

Parser::Parser() {
    symbol_table["pi"] = 4.0 * atan(1.0);
    symbol_table["e"] = exp(1.0);
}

double Parser::operator()(const std::string &s) {
    std::istringstream ist{s};
    p_lexer = new Lexer{ist};
    double result = assign_expr();
    delete p_lexer;
    return result;
}

// Function to get the current value of a variable
double Parser::get_variable_value(const std::string &varName) {
    auto it = symbol_table.find(varName);
    if (it != symbol_table.end()) {
        return it->second;
    } else {
        throw std::runtime_error("Variable not found: " + varName);
    }
}


double Parser::assign_expr() {
    Token t = p_lexer->get_current_token();
    std::string text = p_lexer->get_token_text();
    Parser::_lhsToken = "";
    double result = add_expr();  // Evaluate the LHS expression

    // If the current token is an assignment, process the assignment
    if (p_lexer->get_current_token() == Token::Assign) {
        if (t != Token::Id) {
            throw std::runtime_error("Syntax error: target of assignment must be an identifier");
        }

        if (text == "pi" || text == "e") {
            throw std::runtime_error("Syntax error: attempt to modify the constant " + text);
        }

        p_lexer->advance();  // Move past the assignment operator
        
        // Evaluate the RHS expression and assign it to the LHS variable
        double rhs_value = add_expr();

        // Handle cases like a = a + 1 by updating the symbol table
        if (symbol_table.find(text) != symbol_table.end()) {
            result = symbol_table[text] = rhs_value;
        } else {
            // If LHS is not in the symbol table, treat it as a new variable
            symbol_table[text] = rhs_value;
            result = rhs_value;
        }
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
                break;
            default:
                return result;
        }
    }
}

double Parser::mul_expr() {
    double result = pow_expr();
    double x;
    for (;;) {
        switch (p_lexer->get_current_token()) {
            case Token::Mul:
                p_lexer->advance();
                result *= pow_expr();
                break;
            case Token::Div:
                p_lexer->advance();
                x = pow_expr();
                if (x == 0)
                    throw std::runtime_error("Runtime error: attempt to divide by zero");
                result /= x;
                break;
            case Token::Mod:
                p_lexer->advance();
                x = pow_expr();
                if (x == 0)
                    throw std::runtime_error("Runtime error: attempt to divide by zero");
                result = fmod(result, x);
                break;
            default:
                return result;
        }
    }
}

double Parser::pow_expr() {
    double result = unary_expr();
    if (p_lexer->get_current_token() == Token::Pow) {
        p_lexer->advance();
        double x = unary_expr();
        check_domain(result, x);
        return pow(result, x);
    }
    return result;
}

double Parser::unary_expr() {
    switch (p_lexer->get_current_token()) {
        case Token::Plus:
            p_lexer->advance();
            return +primary();
        case Token::Minus:
            p_lexer->advance();
            return -primary();
        default:
            return primary();
    }
}

double Parser::primary() {
    std::string text = p_lexer->get_token_text();
    double arg;
    Token current_token = p_lexer->get_current_token();
    switch (current_token) {
        case Token::Array:
            Parser::_dependsOnList->insert(text);
            return 0;

        case Token::Id:
            if (Parser::_lhsToken.empty()) {
                Parser::_lhsToken = text;
                p_lexer->advance();
                std::string word = p_lexer->get_token_text();
                if (word == "[") {
                    Parser::_lhsToken = handleBracketedExpression(text);
                }
            } else {
                p_lexer->advance();
                std::string word = p_lexer->get_token_text();
                if (word == "[") {
                    text = handleBracketedExpression(text);
                }
                // Use std::set for _dependsOnList to simplify and avoid duplicates
                Parser::_dependsOnList->insert(text);
            }
            return symbol_table[text];

        case Token::Number:
            Parser::_dependsOnList->insert("PR");  // Use std::set for efficient checking
            p_lexer->advance();
            // Safely convert text to number
            try {
                return to_number(text);
            } catch (const std::invalid_argument&) {
                throw std::runtime_error("Syntax error: invalid number format");
            }

        case Token::Lp:
            p_lexer->advance();
            arg = add_expr();
            if (p_lexer->get_current_token() != Token::Rp) {
                throw std::runtime_error("Syntax error: missing ) after subexpression");
            }
            p_lexer->advance();
            return arg;

        case Token::Sin:
            return sin(get_argument());

        case Token::Cos:
            return cos(get_argument());

        case Token::Tan:
            arg = get_argument();
            return tan(arg);

        case Token::Asin:
            arg = get_argument();
            if (arg < -1 || arg > 1) {
                throw std::runtime_error("Runtime error: asin out of range");
            }
            return asin(arg);

        case Token::Acos:
            arg = get_argument();
            if (arg < -1 || arg > 1) {
                throw std::runtime_error("Runtime error: acos out of range");
            }
            return acos(arg);

        case Token::Atan:
            return atan(get_argument());

        case Token::Log:
            arg = get_argument();
            if (arg <= 0) {
                throw std::runtime_error("Runtime error: logarithm of non-positive number");
            }
            return log(arg);

        case Token::Exp:
            return exp(get_argument());

        case Token::Log10:
            arg = get_argument();
            if (arg <= 0) {
                throw std::runtime_error("Runtime error: logarithm of non-positive number");
            }
            return log10(arg);

        case Token::Sqrt:
            arg = get_argument();
            if (arg < 0) {
                throw std::runtime_error("Runtime error: attempt to take square root of negative number");
            }
            return sqrt(arg);

        case Token::Int:
            arg = get_argument();
            return (arg < 0) ? ceil(arg) : floor(arg);

        default:
            throw std::runtime_error("Syntax error: invalid primary expression. Token: " + p_lexer->get_token_text());
    }
}

std::string Parser::handleBracketedExpression(const std::string& text) {
    p_lexer->advance();
    std::string value = p_lexer->get_token_text();
    std::cout << "Value: " << value << std::endl;
    p_lexer->advance();
    std::string word = p_lexer->get_token_text();
    if (word != "]") {
        throw std::runtime_error("Parsing expression error: Expected ']' sign!");
    }
    p_lexer->advance();
    return text + "[" + value + "]";
}

void Parser::check_domain(double x, double y) {
    if (x >= 0) return;
    double e = std::abs(y);
    if (e <= 0 || e >= 1) return;
    throw std::runtime_error("Runtime error: attempt to take root of a negative number");
}

double Parser::get_argument() {
    p_lexer->advance();
    if (p_lexer->get_current_token() != Token::Lp)
        throw std::runtime_error("Syntax error: missing ( after function name");
    p_lexer->advance();
    double arg = add_expr();
    if (p_lexer->get_current_token() != Token::Rp)
        throw std::runtime_error("Syntax error: missing ) after function argument");
    p_lexer->advance();
    return arg;
}

void parse(const std::string &s, std::set<std::string> &dependsOnList) {
    Parser parser;
    Parser::_dependsOnList = &dependsOnList;

    try {
        double result = parser(s);
        // std::cout << "Result: " << result << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Parsing error: " << e.what() << '\n';
    }
}
