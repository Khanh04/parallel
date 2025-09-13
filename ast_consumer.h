#ifndef AST_CONSUMER_H
#define AST_CONSUMER_H

#include "data_structures.h"
#include "function_analyzer.h"
#include "loop_analyzer.h"
#include "main_extractor.h"
#include "hybrid_parallelizer.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/RecursiveASTVisitor.h"

// NEW: Typedef collector for extracting type definitions
class TypedefCollector : public clang::RecursiveASTVisitor<TypedefCollector> {
public:
    SourceCodeContext sourceContext;
    clang::SourceManager *SM;
    
    TypedefCollector(clang::SourceManager *sourceManager) : SM(sourceManager) {}
    
    bool VisitTypedefDecl(clang::TypedefDecl *TD);
    bool VisitTypeAliasDecl(clang::TypeAliasDecl *TAD);  // For 'using' aliases
    
private:
    std::string getSourceText(clang::SourceRange range);
};

class HybridParallelizerConsumer : public clang::ASTConsumer {
private:
    clang::CompilerInstance &CI;
    std::string inputFileName;  // Store input filename for output naming
    GlobalVariableCollector globalCollector;
    ComprehensiveFunctionAnalyzer functionAnalyzer;
    MainFunctionExtractor mainExtractor;
    ComprehensiveLoopAnalyzer loopAnalyzer;
    TypedefCollector typedefCollector;  // NEW: Typedef collector
    
public:
    HybridParallelizerConsumer(clang::CompilerInstance &CI, const std::string &inputFile);
    
    void HandleTranslationUnit(clang::ASTContext &Context) override;
    
private:
    void printEnhancedAnalysisResults(const HybridParallelizer& parallelizer);
    void generateDependencyGraphVisualization(const HybridParallelizer& parallelizer);
    void generateGraphvizDependencyGraph(const HybridParallelizer& parallelizer);
    std::string extractOriginalIncludes(clang::ASTContext &Context);
    std::string generateOutputFileName() const;  // Generate output filename from input
};

class HybridParallelizerAction : public clang::ASTFrontendAction {
public:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI,
                                                         llvm::StringRef file) override;
};

#endif // AST_CONSUMER_H