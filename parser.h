#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include "lexer.h"

class Parser {
public:
    Parser();
    double operator()(const std::string& s);
    static std::string _lhsToken;
    static std::vector<std::string> *_dependsOnList;
    
private:
    Lexer* p_lexer;
    
    double assign_expr();
    double add_expr();
    double mul_expr();
    double pow_expr();
    double unary_expr();
    double primary();
    
    double get_argument();
    static void check_domain(double x, double y);
};

void parse(const std::string s, std::vector<std::string> &dependsOnList);

#endif // PARSER_H
