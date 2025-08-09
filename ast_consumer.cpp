#include "ast_consumer.h"
#include "clang/AST/ASTContext.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <fstream>

using namespace clang;

HybridParallelizerConsumer::HybridParallelizerConsumer(CompilerInstance &CI) 
    : CI(CI), functionAnalyzer(globalCollector.globalVariables),
      mainExtractor(&CI.getSourceManager()),
      loopAnalyzer(&CI.getSourceManager(), globalCollector.globalVariables) {}

void HybridParallelizerConsumer::HandleTranslationUnit(ASTContext &Context) {
    TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
    
    // First pass: collect global variables
    globalCollector.TraverseDecl(TU);
    
    // Update analyzers with global variables
    functionAnalyzer.globalVars = globalCollector.globalVariables;
    functionAnalyzer.setSourceManager(&CI.getSourceManager());
    
    // Second pass: analyze all functions (this will also find loops)
    functionAnalyzer.TraverseDecl(TU);
    
    // Third pass: analyze loops across all functions
    loopAnalyzer.TraverseDecl(TU);
    
    // Fourth pass: set loop information in function analyzer
    functionAnalyzer.setFunctionLoops(loopAnalyzer.getAllFunctionLoops());
    
    // Fifth pass: extract main function calls (only visits main function)
    mainExtractor.setFunctionAnalysis(&functionAnalyzer.functionAnalysis);
    mainExtractor.TraverseDecl(TU);
    
    // Perform parallelization analysis
    HybridParallelizer parallelizer(mainExtractor.functionCalls, 
                                   functionAnalyzer.functionAnalysis,
                                   mainExtractor.getLocalVariables(),
                                   functionAnalyzer.functionInfo,
                                   mainExtractor.getMainLoops(),
                                   globalCollector.globalVariables);
    
    // Generate output
    std::string hybridCode = parallelizer.generateHybridMPIOpenMPCode();
    
    // Write to output file
    std::ofstream outFile("enhanced_hybrid_mpi_openmp_output.cpp");
    if (outFile.is_open()) {
        outFile << hybridCode;
        outFile.close();
        llvm::outs() << "Enhanced Hybrid MPI/OpenMP parallelized code generated: enhanced_hybrid_mpi_openmp_output.cpp\n";
    } else {
        llvm::errs() << "Error: Could not create output file\n";
    }
    
    // Print comprehensive analysis results
    printEnhancedAnalysisResults(parallelizer);
}

void HybridParallelizerConsumer::printEnhancedAnalysisResults(const HybridParallelizer& parallelizer) {
    llvm::outs() << "\n=== Enhanced Hybrid MPI/OpenMP Analysis Results ===\n";
    
    llvm::outs() << "\nGlobal Variables Found:\n";
    for (const auto& var : globalCollector.globalVariables) {
        llvm::outs() << "  " << var << "\n";
    }
    
    llvm::outs() << "\nLocal Variables Found:\n";
    const auto& localVars = parallelizer.getLocalVariables();
    for (const auto& pair : localVars) {
        const LocalVariable& localVar = pair.second;
        llvm::outs() << "  " << localVar.name << " (" << localVar.type << ")\n";
    }
    
    llvm::outs() << "\nFunction Analysis:\n";
    for (const auto& pair : functionAnalyzer.functionAnalysis) {
        llvm::outs() << "  Function: " << pair.first << "\n";
        llvm::outs() << "    Return Type: " << pair.second.returnType << "\n";
        llvm::outs() << "    Global Reads: ";
        for (const auto& var : pair.second.readSet) {
            llvm::outs() << var << " ";
        }
        llvm::outs() << "\n    Global Writes: ";
        for (const auto& var : pair.second.writeSet) {
            llvm::outs() << var << " ";
        }
        llvm::outs() << "\n";
    }
    
    llvm::outs() << "\n=== Comprehensive Loop Analysis (All Functions) ===\n";
    const auto& allFunctionLoops = loopAnalyzer.getAllFunctionLoops();
    int totalLoops = 0;
    int parallelizableLoops = 0;
    
    for (const auto& pair : allFunctionLoops) {
        const std::string& funcName = pair.first;
        const std::vector<LoopInfo>& loops = pair.second;
        
        if (!loops.empty()) {
            llvm::outs() << "\nFunction: " << funcName << "\n";
            for (const auto& loop : loops) {
                totalLoops++;
                llvm::outs() << "  Loop at lines " << loop.start_line << "-" << loop.end_line << ":\n";
                llvm::outs() << "    Type: " << loop.type << "\n";
                llvm::outs() << "    Parallelizable: " << (loop.parallelizable ? "YES" : "NO") << "\n";
                if (loop.parallelizable) {
                    parallelizableLoops++;
                    llvm::outs() << "    OpenMP Schedule: " << loop.schedule_type << "\n";
                    llvm::outs() << "    Generated Pragma: " << loop.pragma_text << "\n";
                    if (!loop.reduction_vars.empty()) {
                        llvm::outs() << "    Reduction variables: ";
                        for (const auto& var : loop.reduction_vars) {
                            llvm::outs() << var << " ";
                        }
                        llvm::outs() << "(" << loop.reduction_op << ")\n";
                    }
                    if (!loop.loop_variable.empty()) {
                        llvm::outs() << "    Loop variable: " << loop.loop_variable << "\n";
                    }
                }
                llvm::outs() << "    Analysis: " << loop.analysis_notes << "\n";
                
                if (!loop.read_vars.empty()) {
                    llvm::outs() << "    Variables read: ";
                    for (const auto& var : loop.read_vars) {
                        llvm::outs() << var << " ";
                    }
                    llvm::outs() << "\n";
                }
                
                if (!loop.write_vars.empty()) {
                    llvm::outs() << "    Variables written: ";
                    for (const auto& var : loop.write_vars) {
                        llvm::outs() << var << " ";
                    }
                    llvm::outs() << "\n";
                }
            }
        }
    }
    
    llvm::outs() << "\n=== Loop Parallelization Summary ===\n";
    llvm::outs() << "Total loops found: " << totalLoops << "\n";
    llvm::outs() << "Parallelizable loops: " << parallelizableLoops << "\n";
    llvm::outs() << "Parallelization rate: " << (totalLoops > 0 ? (100.0 * parallelizableLoops / totalLoops) : 0) << "%\n";
    
    llvm::outs() << "\nFunctions with parallelizable loops:\n";
    for (const auto& pair : functionAnalyzer.functionInfo) {
        const FunctionInfo& info = pair.second;
        if (info.has_parallelizable_loops) {
            int funcParallelizable = 0;
            for (const auto& loop : info.loops) {
                if (loop.parallelizable) funcParallelizable++;
            }
            llvm::outs() << "  " << info.name << ": " << funcParallelizable 
                        << "/" << info.loops.size() << " loops parallelized\n";
        }
    }
    
    llvm::outs() << "\nFunction Calls in main():\n";
    for (int i = 0; i < mainExtractor.functionCalls.size(); ++i) {
        const auto& call = mainExtractor.functionCalls[i];
        llvm::outs() << "  " << i << ": " << call.functionName 
                    << " (line " << call.lineNumber << ")\n";
    }
    
    auto groups = parallelizer.getParallelizableGroups();
    llvm::outs() << "\nMPI Parallelizable Function Groups:\n";
    for (int i = 0; i < groups.size(); ++i) {
        llvm::outs() << "  Group " << i << ": ";
        for (int idx : groups[i]) {
            llvm::outs() << mainExtractor.functionCalls[idx].functionName << " ";
        }
        llvm::outs() << "\n";
    }
    
    llvm::outs() << "\n=== Enhanced Hybrid Parallelization Complete! ===\n";
    llvm::outs() << "The generated code combines:\n";
    llvm::outs() << "  - MPI for function-level parallelism across processes\n";
    llvm::outs() << "  - OpenMP for loop-level parallelism within each process\n";
    llvm::outs() << "  - Comprehensive loop analysis across ALL functions\n";
    llvm::outs() << "  - Automatic pragma generation for parallelizable loops\n";
    llvm::outs() << "  - Thread-safe function execution with nested parallelism\n";
}

std::unique_ptr<ASTConsumer> HybridParallelizerAction::CreateASTConsumer(CompilerInstance &CI,
                                                                        llvm::StringRef file) {
    return std::make_unique<HybridParallelizerConsumer>(CI);
}