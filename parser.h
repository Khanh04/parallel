#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "utils.h"
#include <string>
#include <vector>
#include <map>

class Parser {
public:
    Parser();
    double operator()(const std::string &s);
    static std::string _lhsToken;
    static std::vector<std::string> *_dependsOnList;

private:
    Lexer *p_lexer;
    double assign_expr();
    double add_expr();
    double mul_expr();
    double pow_expr();
    double unary_expr();
    double primary();
    double get_argument();
    static void check_domain(double x, double y);
};

void parse(const std::string &s, std::vector<std::string> &dependsOnList);

// Symbol table to hold variable values
extern std::map<std::string, double> symbol_table;

#endif // PARSER_H
