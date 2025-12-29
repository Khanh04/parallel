#ifndef FUNCTION_ANALYZER_H
#define FUNCTION_ANALYZER_H

#include "data_structures.h"
#include "clang/AST/AST.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include <map>
#include <set>
#include <string>

// Collects global variable declarations with type information
class GlobalVariableCollector : public clang::RecursiveASTVisitor<GlobalVariableCollector> {
public:
    std::set<std::string> globalVariables;  // Keep for backward compatibility
    std::map<std::string, GlobalVariable> globalVariableInfo;  // NEW: With type info
    
    bool VisitVarDecl(clang::VarDecl *VD);
};

// Enhanced function analyzer that captures complete function information
class ComprehensiveFunctionAnalyzer : public clang::RecursiveASTVisitor<ComprehensiveFunctionAnalyzer> {
private:
    std::string currentFunction;
    std::set<std::string> currentFunctionParams;
    
public:
    std::set<std::string> globalVars;
    std::map<std::string, FunctionInfo> functionInfo;
    std::map<std::string, FunctionAnalysis> functionAnalysis;
    clang::SourceManager *SM;
    
    ComprehensiveFunctionAnalyzer();
    ComprehensiveFunctionAnalyzer(const std::set<std::string>& globals);
    
    void setSourceManager(clang::SourceManager *sourceManager);
    void setFunctionLoops(const std::map<std::string, std::vector<LoopInfo>>& functionLoops);
    
    bool VisitFunctionDecl(clang::FunctionDecl *FD);
    bool VisitDeclRefExpr(clang::DeclRefExpr *DRE);
    bool VisitBinaryOperator(clang::BinaryOperator *BO);
    bool VisitUnaryOperator(clang::UnaryOperator *UO);
    
    // Generate parallelized function body
    std::string generateParallelizedFunction(const std::string& funcName);
    
private:
    std::string getSourceText(clang::SourceRange range);
};

#endif // FUNCTION_ANALYZER_H