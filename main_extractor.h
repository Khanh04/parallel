#ifndef MAIN_EXTRACTOR_H
#define MAIN_EXTRACTOR_H

#include "data_structures.h"
#include "clang/AST/AST.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include <map>
#include <string>
#include <vector>

class MainFunctionExtractor : public clang::RecursiveASTVisitor<MainFunctionExtractor> {
public:
    std::vector<FunctionCall> functionCalls;
    std::string mainFunctionBody;
    clang::SourceManager *SM;
    std::map<std::string, LocalVariable> localVariables;
    std::map<std::string, FunctionAnalysis>* functionAnalysisPtr;
    std::vector<LoopInfo> mainLoops; // Loops found in main function
    int variableDeclarationCounter;  // Track declaration order
    
    MainFunctionExtractor(clang::SourceManager *sourceManager);
    
    void setFunctionAnalysis(std::map<std::string, FunctionAnalysis>* analysis);
    
    bool VisitFunctionDecl(clang::FunctionDecl *FD);
    
    const std::vector<LoopInfo>& getMainLoops() const;
    const std::map<std::string, LocalVariable>& getLocalVariables() const;
    
private:
    void collectLocalVariables(clang::CompoundStmt *body);
    void collectLocalVariablesInStmt(clang::Stmt *stmt);
    void processStatement(clang::Stmt *stmt);
    void analyzeLocalDependencies();
    bool isUserFunction(const std::string& funcName);
    void findUsedVariables(clang::Expr *expr, std::set<std::string>& usedVars);
    std::string getSourceText(clang::SourceRange range);
};

#endif // MAIN_EXTRACTOR_H