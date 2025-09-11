#ifndef LOOP_ANALYZER_H
#define LOOP_ANALYZER_H

#include "data_structures.h"
#include "clang/AST/AST.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include <map>
#include <set>
#include <string>
#include <vector>

// Enhanced loop analyzer that works across all functions
class ComprehensiveLoopAnalyzer : public clang::RecursiveASTVisitor<ComprehensiveLoopAnalyzer> {
private:
    clang::SourceManager *SM;
    std::map<std::string, std::vector<LoopInfo>> functionLoops;
    std::string currentFunction;
    bool insideLoop = false;
    int loopDepth = 0;
    std::set<std::string> globalVariables;
    
public:
    ComprehensiveLoopAnalyzer(clang::SourceManager *sourceManager, const std::set<std::string>& globals);
    
    bool VisitFunctionDecl(clang::FunctionDecl *FD);
    bool VisitForStmt(clang::ForStmt *FS);
    bool VisitWhileStmt(clang::WhileStmt *WS);
    bool VisitDoStmt(clang::DoStmt *DS);
    
    const std::map<std::string, std::vector<LoopInfo>>& getAllFunctionLoops() const;
    
private:
    void processForLoop(clang::ForStmt *FS);
    void processWhileLoop(clang::WhileStmt *WS);
    void processDoWhileLoop(clang::DoStmt *DS);
    void analyzeLoopBody(clang::Stmt *body, LoopInfo &loop);
    void performDependencyAnalysis(LoopInfo &loop, const std::set<std::string> &localVars = std::set<std::string>());
    std::string generateOpenMPPragma(const LoopInfo& loop);
    std::string getSourceText(clang::SourceRange range);
};

#endif // LOOP_ANALYZER_H