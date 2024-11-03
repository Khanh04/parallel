#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "utils.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdio.h>

class Parser {
public:
    Parser();
    double operator()(const std::string &s);
    static std::string _lhsToken;
    static std::set<std::string>* _dependsOnList;
    double get_variable_value(const std::string &varName);
    void set_symbol_value(const std::string &varName, double value);
    const std::unordered_map<std::string, bool>& get_varReads() const { return varReads; }
    const std::unordered_map<std::string, bool>& get_varWrites() const { return varWrites; }
    void resetReadWriteTracking() {
        varReads.clear();
        varWrites.clear();
    }
    void trackVarRead(const std::string &varName) {
        varReads[varName] = true;
    }
    void trackVarWrite(const std::string &varName) {
        varWrites[varName] = true;
    }



private:
    std::unordered_map<std::string, bool> varReads;
    std::unordered_map<std::string, bool> varWrites;
        // Helper functions for tracking reads and writes

    Lexer *p_lexer;
    double assign_expr();
    double add_expr();
    double mul_expr();
    double pow_expr();
    double unary_expr();
    double primary();
    double get_argument();
    static void check_domain(double x, double y);
    std::string handleBracketedExpression(const std::string& text);
};

void parse(const std::string &s, std::set<std::string> &dependsOnList, Parser* parser = nullptr);

// Symbol table to hold variable values
extern std::map<std::string, double> symbol_table;
int precedence(char op);
bool isOperator(char c);
double applyOperation(double a, double b, char op);
double evaluateExpression(const std::string &expr);
#endif // PARSER_H
