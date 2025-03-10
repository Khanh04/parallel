#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "common.h"
#include "file_operations.h"
#include "lexer.h"
#include "parser.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

class Functions {
public:
    void print();
    int findFunction(const std::string &s);
    void addCall(std::string f1, std::string f2);
    void addCall(std::string f2);
    void addFunction(std::string f);

    std::vector<std::string> names;
    std::vector<std::vector<int>> functionCalls;
    static std::string currentFunction;
private:
};

void parseFunctionOrVariableDefinition(Functions &f, std::string &functionName, std::string fileLine, int maxStatementId, std::ifstream &fIn, std::ofstream &fOut, bool &in_main, int parallelize);
bool checkLoopDependency(int &loopMin, int &loopMax);
void parseForLoop(std::string fileLine, int &maxStatementId, int &loopMin, int &loopMax, std::unordered_map<std::string, bool> &varReads, std::unordered_map<std::string, bool> &varWrites, std::ifstream &fIn, std::ofstream &fOut, int parallelize);
void parseLoopBody(const std::string &varName, int val1, int val2, bool increment, std::vector<std::string> &myvector, int &maxStatementId, std::unordered_map<std::string, bool> &varReads, std::unordered_map<std::string, bool> &varWrites);
bool parseFunctionCall(Functions &f, std::ofstream &fOut, std::string fileLine, const int maxStatementId);
void parseExpression(std::ofstream &fOut, std::string fileLine, const int maxStatementId);
void parallelizeLoop(std::ifstream &fIn, std::ofstream &fOut, const std::string varName, const int val1, const int val2);
void detectDependenciesInLoop(std::ifstream &fIn, std::ofstream &fOut, std::string &fileLine, int &maxStatementId, const int increment, int &loopMin, int &loopMax, const std::string varName, const int val1, const int val2);
bool overlap(const std::set<std::string>& s1, const std::set<std::string>& s2);
void parseWhile(std::string &word, Lexer *p_lexer, int &maxStatementId, std::unordered_map<std::string, bool> &varReads, std::unordered_map<std::string, bool> &varWrites, std::ifstream &fIn, std::ofstream &fOut);
void parseDoWhile(std::string &word, Lexer *p_lexer, int &maxStatementId, std::unordered_map<std::string, bool> &varReads, std::unordered_map<std::string, bool> &varWrites, std::ifstream &fIn, std::ofstream &fOut);
bool primitiveType(std::string &word);

#endif // FUNCTIONS_H
