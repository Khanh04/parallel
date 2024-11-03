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
#include <stack>
#include <stdexcept>
#include <cctype>
#include <unordered_map>


// Static member initialization
std::string Parser::_lhsToken;
std::set<std::string>* Parser::_dependsOnList = nullptr;
// Symbol table definition
std::map<std::string, double> symbol_table;

Parser::Parser() {
    symbol_table["pi"] = 4.0 * atan(1.0);
    symbol_table["e"] = exp(1.0);
}

// Main parse function
double Parser::operator()(const std::string &s) {
    std::istringstream ist{s};
    p_lexer = new Lexer{ist};
    resetReadWriteTracking();  // Clear previous read/write info for a fresh parse
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

void Parser::set_symbol_value(const std::string &varName, double value) {
    if (varName == "pi" || varName == "e") {
        throw std::runtime_error("Cannot update constant variable: " + varName);
    }
    symbol_table[varName] = value;
    trackVarWrite(varName);  // Track the variable as written
}

double Parser::assign_expr() {
    Token t = p_lexer->get_current_token();
    std::string text = p_lexer->get_token_text();
    Parser::_lhsToken = "";
    double result = add_expr();  // Evaluate LHS expression
    std::cout << "LHS: " << Parser::_lhsToken << std::endl;

    if (p_lexer->get_current_token() == Token::Assign) {
        if (t != Token::Id) {
            throw std::runtime_error("Syntax error: target of assignment must be an identifier");
        }

        if (text == "pi" || text == "e") {
            throw std::runtime_error("Syntax error: attempt to modify constant " + text);
        }

        p_lexer->advance();  // Move past '='
        double rhs_value = add_expr();

        if (symbol_table.find(text) != symbol_table.end()) {
            result = symbol_table[text] = rhs_value;
        } else {
            symbol_table[text] = rhs_value;
            result = rhs_value;
        }

        trackVarWrite(text);  // Track variable as written
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
        {            
            std::cout << text << std::endl;
            size_t openBracket = text.find('[');
            size_t closeBracket = text.find(']');
            
            // Check for valid brackets in the array expression
            if (openBracket == std::string::npos || closeBracket == std::string::npos || closeBracket <= openBracket) {
                throw std::runtime_error("Invalid array access syntax in: " + text);
            }

            // Array name is everything before the '['
            std::string arrayName = text.substr(0, openBracket);

            // Index expression is everything inside the brackets
            std::string indexExpr = text.substr(openBracket + 1, closeBracket - openBracket - 1);
            // Process the index expression to replace variables with their current values from symbol_table
            std::string evaluatedIndexExpr;
            std::istringstream iss(indexExpr);
            std::string token;
            // Tokenize the index expression
            for (size_t pos = 0; pos < indexExpr.size(); ++pos) {
                char c = indexExpr[pos];

                if (std::isalpha(c)) {
                    // Handle variables
                    token += c;
                    
                    // Process and replace the full variable name with its value
                    while (pos + 1 < indexExpr.size() && std::isalnum(indexExpr[pos + 1])) {
                        token += indexExpr[++pos];  // Continue reading the variable name
                    }
                    
                    // Replace the variable with its value
                    double variableValue = get_variable_value(token);

                    evaluatedIndexExpr += std::to_string(variableValue);
                    token.clear(); // Clear token for the next variable or literal
                }
                else if (std::isdigit(c) || c == '.') {
                    // Handle numeric values
                    token += c;
                    
                    // Continue reading the full numeric value
                    while (pos + 1 < indexExpr.size() && (std::isdigit(indexExpr[pos + 1]) || indexExpr[pos + 1] == '.')) {
                        token += indexExpr[++pos];
                    }
                    
                    evaluatedIndexExpr += token;  // Append the complete number
                    token.clear();
                }
                else if (isOperator(c) || c == '(' || c == ')') {
                    // Directly append operators and parentheses
                    evaluatedIndexExpr += c;
                }
                else if (std::isspace(c)) {
                    // Skip any whitespace
                    continue;
                }
                else {
                    throw std::runtime_error("Invalid character in expression: " + std::string(1, c));
                }
            }

            // Now, evaluate the expression `evaluatedIndexExpr`, e.g., "5-1"
            double evaluatedIndex = evaluateExpression(evaluatedIndexExpr);

            std::string evaluatedArrayAccess = arrayName + "[" + std::to_string(static_cast<int>(evaluatedIndex)) + "]";
            trackVarWrite(evaluatedArrayAccess);
            Parser::_dependsOnList->insert(evaluatedArrayAccess);
            return symbol_table[evaluatedArrayAccess];
        }
        case Token::Id:
            trackVarRead(text);
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

void parse(const std::string &s, std::set<std::string> &dependsOnList, Parser* parser) {
    bool ownsParser = false;  // Track if we created a new Parser instance

    if (!parser) {
        parser = new Parser();  // Create a new instance if none is provided
        ownsParser = true;      // Mark ownership to delete it later
    }

    // Set the dependency list for the parser instance
    Parser::_dependsOnList = &dependsOnList;

    try {
        parser->resetReadWriteTracking();  // Clear previous read/write info
        double result = (*parser)(s);      // Use the provided or newly created parser
    } catch (const std::exception &e) {
        std::cerr << "Parsing error: " << e.what() << '\n';
    }

    // Clean up if we created the parser instance
    if (ownsParser) {
        delete parser;
    }
}



// Helper functions to check operator precedence and type
int precedence(char op) {
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;
    return 0;
}

bool isOperator(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/';
}

// Function to perform arithmetic operations
double applyOperation(double a, double b, char op) {
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': 
            if (b == 0) throw std::runtime_error("Division by zero");
            return a / b;
        default: throw std::runtime_error("Unsupported operator");
    }
}

// Main function to evaluate the arithmetic expression
double evaluateExpression(const std::string &expr) {
    std::stack<double> values; // Stack for numbers
    std::stack<char> ops; // Stack for operators
    std::string token;
    std::istringstream tokens(expr);

    for (size_t i = 0; i < expr.size(); ++i) {
        char c = expr[i];

        // Skip whitespace
        if (std::isspace(c)) continue;

        // Parse numbers (handling decimals)
        if (std::isdigit(c) || c == '.') {
            token += c;
            // Continue reading digits (or a single decimal)
            while (i + 1 < expr.size() && (std::isdigit(expr[i + 1]) || expr[i + 1] == '.')) {
                token += expr[++i];
            }
            // Push the full number onto the values stack
            values.push(std::stod(token));
            token.clear();
        }
        // Handle negative numbers at the start or after an operator
        else if (c == '-' && (i == 0 || isOperator(expr[i - 1]))) {
            token += c;
            // Continue reading digits for a negative number
            while (i + 1 < expr.size() && (std::isdigit(expr[i + 1]) || expr[i + 1] == '.')) {
                token += expr[++i];
            }
            values.push(std::stod(token));
            token.clear();
        }
        // Handle operators
        else if (isOperator(c)) {
            // Apply operators based on precedence
            while (!ops.empty() && precedence(ops.top()) >= precedence(c)) {
                double b = values.top(); values.pop();
                double a = values.top(); values.pop();
                char op = ops.top(); ops.pop();
                values.push(applyOperation(a, b, op));
            }
            ops.push(c);
        }
    }

    // Apply remaining operators to remaining values
    while (!ops.empty()) {
        double b = values.top(); values.pop();
        double a = values.top(); values.pop();
        char op = ops.top(); ops.pop();
        values.push(applyOperation(a, b, op));
    }

    return values.top();
}
