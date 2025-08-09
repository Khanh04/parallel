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

class HybridParallelizerConsumer : public clang::ASTConsumer {
private:
    clang::CompilerInstance &CI;
    GlobalVariableCollector globalCollector;
    ComprehensiveFunctionAnalyzer functionAnalyzer;
    MainFunctionExtractor mainExtractor;
    ComprehensiveLoopAnalyzer loopAnalyzer;
    
public:
    HybridParallelizerConsumer(clang::CompilerInstance &CI);
    
    void HandleTranslationUnit(clang::ASTContext &Context) override;
    
private:
    void printEnhancedAnalysisResults(const HybridParallelizer& parallelizer);
    void generateDependencyGraphVisualization(const HybridParallelizer& parallelizer);
    void generateGraphvizDependencyGraph(const HybridParallelizer& parallelizer);
};

class HybridParallelizerAction : public clang::ASTFrontendAction {
public:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &CI,
                                                         llvm::StringRef file) override;
};

#endif // AST_CONSUMER_H