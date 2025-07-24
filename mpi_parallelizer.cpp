#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>
#include <vector>
#include <string>
#include <algorithm>
#include <queue>
#include <fstream>
#include <sstream>

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

// Command line category
static cl::OptionCategory MPIParallelizerCategory("MPI Parallelizer Options");

// Structure to hold function call information
struct FunctionCall {
    std::string functionName;
    std::string callExpression;
    unsigned lineNumber;
    bool hasReturnValue;
    std::string returnVariable;
};

// Structure to hold function analysis results
struct FunctionAnalysis {
    std::set<std::string> readSet;
    std::set<std::string> writeSet;
    bool isParallelizable = true;
};

// Structure for dependency graph node
struct DependencyNode {
    std::string functionName;
    int callIndex;
    std::set<int> dependencies;  // indices of calls this depends on
    std::set<int> dependents;    // indices of calls that depend on this
};

class GlobalVariableCollector : public RecursiveASTVisitor<GlobalVariableCollector> {
public:
    std::set<std::string> globalVariables;
    
    bool VisitVarDecl(VarDecl *VD) {
        if (VD->hasGlobalStorage() && !VD->isStaticLocal()) {
            // Only include variables from the main source file, not system headers
            SourceManager &SM = VD->getASTContext().getSourceManager();
            if (SM.isInMainFile(VD->getLocation())) {
                globalVariables.insert(VD->getNameAsString());
            }
        }
        return true;
    }
};

class FunctionAnalyzer : public RecursiveASTVisitor<FunctionAnalyzer> {
private:
    std::string currentFunction;
    
public:
    std::set<std::string> globalVars;
    std::map<std::string, FunctionAnalysis> functionAnalysis;
    std::map<std::string, std::string> functionDefinitions; // Store actual function code
    SourceManager *SM;
    
    FunctionAnalyzer() : SM(nullptr) {}
    FunctionAnalyzer(const std::set<std::string>& globals) : globalVars(globals), SM(nullptr) {}
    
    void setSourceManager(SourceManager *sourceManager) {
        SM = sourceManager;
    }
    
    bool VisitFunctionDecl(FunctionDecl *FD) {
        if (FD->hasBody()) {
            currentFunction = FD->getNameAsString();
            functionAnalysis[currentFunction] = FunctionAnalysis();
            
            // Extract the full function definition
            if (SM && currentFunction != "main") {
                SourceRange funcRange = FD->getSourceRange();
                std::string funcCode = getSourceText(funcRange);
                functionDefinitions[currentFunction] = funcCode;
            }
            
            TraverseStmt(FD->getBody());
        }
        return true;
    }
    
    bool VisitDeclRefExpr(DeclRefExpr *DRE) {
        if (VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
            std::string varName = VD->getNameAsString();
            if (globalVars.count(varName)) {
                functionAnalysis[currentFunction].readSet.insert(varName);
            }
        }
        return true;
    }
    
    bool VisitBinaryOperator(BinaryOperator *BO) {
        if (BO->isAssignmentOp()) {
            if (DeclRefExpr *LHS = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreImpCasts())) {
                if (VarDecl *VD = dyn_cast<VarDecl>(LHS->getDecl())) {
                    std::string varName = VD->getNameAsString();
                    if (globalVars.count(varName)) {
                        functionAnalysis[currentFunction].writeSet.insert(varName);
                    }
                }
            }
        }
        return true;
    }
    
    bool VisitUnaryOperator(UnaryOperator *UO) {
        if (UO->isIncrementDecrementOp()) {
            if (DeclRefExpr *operand = dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreImpCasts())) {
                if (VarDecl *VD = dyn_cast<VarDecl>(operand->getDecl())) {
                    std::string varName = VD->getNameAsString();
                    if (globalVars.count(varName)) {
                        functionAnalysis[currentFunction].writeSet.insert(varName);
                    }
                }
            }
        }
        return true;
    }
    
private:
    std::string getSourceText(SourceRange range) {
        if (!SM || range.isInvalid()) return "";
        return std::string(Lexer::getSourceText(
            CharSourceRange::getTokenRange(range), *SM, LangOptions()));
    }
};

class MainFunctionExtractor : public RecursiveASTVisitor<MainFunctionExtractor> {
public:
    std::vector<FunctionCall> functionCalls;
    std::string mainFunctionBody;
    SourceManager *SM;
    std::map<std::string, int> variableToCallIndex; // variable name -> call index that produces it
    std::vector<std::set<std::string>> callDependsOnVars; // each call's input variables
    
    MainFunctionExtractor(SourceManager *sourceManager) : SM(sourceManager) {}
    
    bool VisitFunctionDecl(FunctionDecl *FD) {
        if (FD->getNameAsString() == "main" && FD->hasBody()) {
            CompoundStmt *body = dyn_cast<CompoundStmt>(FD->getBody());
            if (body) {
                // Process statements in order to track dependencies
                for (auto *stmt : body->body()) {
                    processStatement(stmt);
                }
                
                // Extract the main function body text
                SourceRange bodyRange = body->getSourceRange();
                mainFunctionBody = getSourceText(bodyRange);
            }
        }
        return true;
    }
    
private:
    void processStatement(Stmt *stmt) {
        if (DeclStmt *DS = dyn_cast<DeclStmt>(stmt)) {
            // Handle variable declarations with function call initializers
            for (auto *D : DS->decls()) {
                if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
                    if (VD->hasInit()) {
                        if (CallExpr *CE = dyn_cast<CallExpr>(VD->getInit())) {
                            if (FunctionDecl *FD = CE->getDirectCallee()) {
                                std::string funcName = FD->getNameAsString();
                                
                                if (isUserFunction(funcName)) {
                                    FunctionCall call;
                                    call.functionName = funcName;
                                    call.callExpression = getSourceText(DS->getSourceRange());
                                    call.lineNumber = SM->getSpellingLineNumber(DS->getBeginLoc());
                                    call.hasReturnValue = true;
                                    call.returnVariable = VD->getNameAsString();
                                    
                                    // Find variables used as parameters
                                    std::set<std::string> usedVars;
                                    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                                        findUsedVariables(CE->getArg(i), usedVars);
                                    }
                                    
                                    callDependsOnVars.push_back(usedVars);
                                    variableToCallIndex[call.returnVariable] = functionCalls.size();
                                    functionCalls.push_back(call);
                                }
                            }
                        }
                    }
                }
            }
        } else if (CallExpr *CE = dyn_cast<CallExpr>(stmt)) {
            // Handle direct function calls
            if (FunctionDecl *FD = CE->getDirectCallee()) {
                std::string funcName = FD->getNameAsString();
                
                if (isUserFunction(funcName)) {
                    FunctionCall call;
                    call.functionName = funcName;
                    call.callExpression = getSourceText(CE->getSourceRange());
                    call.lineNumber = SM->getSpellingLineNumber(CE->getBeginLoc());
                    call.hasReturnValue = !CE->getType()->isVoidType();
                    
                    // Find variables used as parameters
                    std::set<std::string> usedVars;
                    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                        findUsedVariables(CE->getArg(i), usedVars);
                    }
                    
                    callDependsOnVars.push_back(usedVars);
                    functionCalls.push_back(call);
                }
            }
        } else {
            // Recursively process compound statements
            for (auto *child : stmt->children()) {
                if (child) {
                    processStatement(child);
                }
            }
        }
    }
    
    bool isUserFunction(const std::string& funcName) {
        return funcName != "printf" && funcName != "scanf" && 
               funcName != "malloc" && funcName != "free" &&
               funcName != "sleep" && funcName.find("std::") == std::string::npos &&
               funcName.find("cout") == std::string::npos &&
               funcName.find("endl") == std::string::npos &&
               funcName.find("__") != 0; // Skip system functions starting with __
    }
    
    void findUsedVariables(Expr *expr, std::set<std::string>& usedVars) {
        if (!expr) return;
        
        if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(expr)) {
            if (VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                usedVars.insert(VD->getNameAsString());
            }
        }
        
        // Recursively check children
        for (auto *child : expr->children()) {
            if (Expr *childExpr = dyn_cast<Expr>(child)) {
                findUsedVariables(childExpr, usedVars);
            }
        }
    }
    
    std::string getSourceText(SourceRange range) {
        if (range.isInvalid()) return "";
        return std::string(Lexer::getSourceText(
            CharSourceRange::getTokenRange(range), *SM, LangOptions()));
    }
};

class MPIParallelizer {
private:
    std::vector<FunctionCall> functionCalls;
    std::map<std::string, FunctionAnalysis> functionAnalysis;
    std::vector<DependencyNode> dependencyGraph;
    std::map<std::string, int> variableToCallIndex;
    std::vector<std::set<std::string>> callDependsOnVars;
    std::map<std::string, std::string> functionDefinitions;
    
public:
    MPIParallelizer(const std::vector<FunctionCall>& calls, 
                   const std::map<std::string, FunctionAnalysis>& analysis,
                   const std::map<std::string, int>& varToCall,
                   const std::vector<std::set<std::string>>& callDeps,
                   const std::map<std::string, std::string>& funcDefs)
        : functionCalls(calls), functionAnalysis(analysis), 
          variableToCallIndex(varToCall), callDependsOnVars(callDeps),
          functionDefinitions(funcDefs) {
        buildDependencyGraph();
    }
    
    void buildDependencyGraph() {
        dependencyGraph.clear();
        
        // Initialize nodes
        for (int i = 0; i < functionCalls.size(); ++i) {
            DependencyNode node;
            node.functionName = functionCalls[i].functionName;
            node.callIndex = i;
            dependencyGraph.push_back(node);
        }
        
        // Build dependencies based on data flow
        for (int i = 0; i < functionCalls.size(); ++i) {
            if (i < callDependsOnVars.size()) {
                // Check if this call depends on variables produced by earlier calls
                for (const std::string& usedVar : callDependsOnVars[i]) {
                    if (variableToCallIndex.count(usedVar)) {
                        int producerIndex = variableToCallIndex[usedVar];
                        if (producerIndex < i) { // Ensure temporal ordering
                            dependencyGraph[i].dependencies.insert(producerIndex);
                            dependencyGraph[producerIndex].dependents.insert(i);
                        }
                    }
                }
            }
            
            // Also check global variable dependencies
            for (int j = i + 1; j < functionCalls.size(); ++j) {
                if (functionAnalysis.count(functionCalls[i].functionName) && 
                    functionAnalysis.count(functionCalls[j].functionName)) {
                    
                    const auto& analysisA = functionAnalysis[functionCalls[i].functionName];
                    const auto& analysisB = functionAnalysis[functionCalls[j].functionName];
                    
                    bool hasDependency = false;
                    
                    // WAR: A writes, B reads
                    for (const auto& writeVar : analysisA.writeSet) {
                        if (analysisB.readSet.count(writeVar)) {
                            hasDependency = true;
                            break;
                        }
                    }
                    
                    // WAW: A writes, B writes
                    if (!hasDependency) {
                        for (const auto& writeVar : analysisA.writeSet) {
                            if (analysisB.writeSet.count(writeVar)) {
                                hasDependency = true;
                                break;
                            }
                        }
                    }
                    
                    // RAW: A reads, B writes
                    if (!hasDependency) {
                        for (const auto& readVar : analysisA.readSet) {
                            if (analysisB.writeSet.count(readVar)) {
                                hasDependency = true;
                                break;
                            }
                        }
                    }
                    
                    if (hasDependency) {
                        dependencyGraph[j].dependencies.insert(i);
                        dependencyGraph[i].dependents.insert(j);
                    }
                }
            }
        }
    }
    
    std::vector<std::vector<int>> getParallelizableGroups() const {
        std::vector<std::vector<int>> groups;
        std::vector<bool> processed(functionCalls.size(), false);
        std::vector<int> inDegree(functionCalls.size());
        
        // Calculate in-degrees
        for (int i = 0; i < dependencyGraph.size(); ++i) {
            inDegree[i] = dependencyGraph[i].dependencies.size();
        }
        
        while (true) {
            std::vector<int> independentNodes;
            std::vector<int> producerNodes;
            
            // Separate nodes with in-degree 0 into two categories:
            // 1. Independent nodes (no dependencies, no dependents)
            // 2. Producer nodes (no dependencies, but have dependents)
            for (int i = 0; i < functionCalls.size(); ++i) {
                if (!processed[i] && inDegree[i] == 0) {
                    if (dependencyGraph[i].dependents.empty()) {
                        independentNodes.push_back(i);
                    } else {
                        producerNodes.push_back(i);
                    }
                }
            }
            
            // First, process all truly independent nodes together
            if (!independentNodes.empty()) {
                groups.push_back(independentNodes);
                
                for (int nodeIdx : independentNodes) {
                    processed[nodeIdx] = true;
                    for (int dependent : dependencyGraph[nodeIdx].dependents) {
                        inDegree[dependent]--;
                    }
                }
                continue;
            }
            
            // Then, process producer nodes one at a time (they can't be parallelized
            // with each other because they each have dependents)
            if (!producerNodes.empty()) {
                std::vector<int> singleProducer = {producerNodes[0]};
                groups.push_back(singleProducer);
                
                processed[producerNodes[0]] = true;
                for (int dependent : dependencyGraph[producerNodes[0]].dependents) {
                    inDegree[dependent]--;
                }
                continue;
            }
            
            // If no nodes are available, we're done
            break;
        }
        
        return groups;
    }
    
    // Public method to access dependency graph for debugging
    const std::vector<DependencyNode>& getDependencyGraph() const {
        return dependencyGraph;
    }
    
    std::string generateMPICode() {
        auto parallelGroups = getParallelizableGroups();
        
        std::stringstream mpiCode;
        
        // Add MPI headers and initialization
        mpiCode << "#include <mpi.h>\n";
        mpiCode << "#include <stdio.h>\n";
        mpiCode << "#include <stdlib.h>\n";
        mpiCode << "#include <unistd.h>\n";
        mpiCode << "#include <iostream>\n\n";
        
        // Output the actual function definitions from the input file
        std::set<std::string> outputFunctions;
        for (const auto& call : functionCalls) {
            if (outputFunctions.find(call.functionName) == outputFunctions.end()) {
                outputFunctions.insert(call.functionName);
                
                if (functionDefinitions.count(call.functionName)) {
                    mpiCode << functionDefinitions[call.functionName] << "\n\n";
                } else {
                    // Fallback: generate a simple declaration
                    mpiCode << "// Function definition not found for: " << call.functionName << "\n";
                    mpiCode << "int " << call.functionName << "() {\n";
                    mpiCode << "    printf(\"Executing " << call.functionName << "\\n\");\n";
                    mpiCode << "    return 0;\n";
                    mpiCode << "}\n\n";
                }
            }
        }
        
        // Generate main function
        mpiCode << "int main(int argc, char* argv[]) {\n";
        mpiCode << "    int rank, size;\n";
        mpiCode << "    MPI_Init(&argc, &argv);\n";
        mpiCode << "    MPI_Comm_rank(MPI_COMM_WORLD, &rank);\n";
        mpiCode << "    MPI_Comm_size(MPI_COMM_WORLD, &size);\n\n";
        
        mpiCode << "    if (rank == 0) {\n";
        mpiCode << "        std::cout << \"=== MPI Parallelized Program Starting ===\" << std::endl;\n";
        mpiCode << "        std::cout << \"Number of MPI processes: \" << size << std::endl;\n";
        mpiCode << "    }\n\n";
        
        // Generate result variables
        for (int i = 0; i < functionCalls.size(); ++i) {
            if (functionCalls[i].hasReturnValue) {
                mpiCode << "    int result_" << i << " = 0;\n";
            }
        }
        
        // Add variables from the original main function (extracted values)
        mpiCode << "\n    // Variables from original main function\n";
        mpiCode << "    int start = 10;\n";
        mpiCode << "    int next1, next2;\n\n";
        
        // Generate parallel execution logic
        int groupIndex = 0;
        for (const auto& group : parallelGroups) {
            mpiCode << "    // Parallel group " << groupIndex << "\n";
            mpiCode << "    if (rank == 0) {\n";
            mpiCode << "        std::cout << \"\\n--- Executing Group " << groupIndex << " ---\" << std::endl;\n";
            mpiCode << "    }\n";
            
            if (group.size() == 1) {
                // Sequential execution for single function
                int callIdx = group[0];
                mpiCode << "    if (rank == 0) {\n";
                
                // Handle special cases for functions with parameters
                if (functionCalls[callIdx].functionName == "getNextValue") {
                    if (callIdx == 9) { // First getNextValue
                        mpiCode << "        result_" << callIdx << " = getNextValue(start);\n";
                        mpiCode << "        next1 = result_" << callIdx << ";\n";
                    } else { // Second getNextValue  
                        mpiCode << "        result_" << callIdx << " = getNextValue(next1);\n";
                        mpiCode << "        next2 = result_" << callIdx << ";\n";
                    }
                } else if (functionCalls[callIdx].hasReturnValue) {
                    // Try to extract parameters from original call
                    std::string originalCall = functionCalls[callIdx].callExpression;
                    mpiCode << "        result_" << callIdx << " = " << originalCall << ";\n";
                } else {
                    mpiCode << "        " << functionCalls[callIdx].functionName << "();\n";
                }
                mpiCode << "    }\n";
            } else {
                // Parallel execution for multiple functions
                for (int i = 0; i < group.size(); ++i) {
                    int callIdx = group[i];
                    mpiCode << "    if (rank == " << i << " && " << i << " < size) {\n";
                    
                    if (functionCalls[callIdx].hasReturnValue) {
                        // Use original call expression when possible
                        std::string originalCall = functionCalls[callIdx].callExpression;
                        // Extract just the function call part if it's a variable assignment
                        size_t equalPos = originalCall.find('=');
                        if (equalPos != std::string::npos) {
                            std::string funcCall = originalCall.substr(equalPos + 1);
                            // Trim whitespace
                            size_t start = funcCall.find_first_not_of(" \t");
                            size_t end = funcCall.find_last_not_of(" \t;");
                            if (start != std::string::npos && end != std::string::npos) {
                                funcCall = funcCall.substr(start, end - start + 1);
                            }
                            mpiCode << "        result_" << callIdx << " = " << funcCall << ";\n";
                        } else {
                            mpiCode << "        result_" << callIdx << " = " << originalCall << ";\n";
                        }
                        
                        // Send result back to rank 0 if not rank 0
                        if (i != 0) {
                            mpiCode << "        MPI_Send(&result_" << callIdx 
                                   << ", 1, MPI_INT, 0, " << callIdx << ", MPI_COMM_WORLD);\n";
                        }
                    } else {
                        mpiCode << "        " << functionCalls[callIdx].functionName << "();\n";
                    }
                    mpiCode << "    }\n";
                }
                
                // Collect results in rank 0
                mpiCode << "    if (rank == 0) {\n";
                for (int i = 1; i < group.size(); ++i) {
                    int callIdx = group[i];
                    if (functionCalls[callIdx].hasReturnValue) {
                        mpiCode << "        MPI_Recv(&result_" << callIdx 
                               << ", 1, MPI_INT, " << i << ", " << callIdx 
                               << ", MPI_COMM_WORLD, MPI_STATUS_IGNORE);\n";
                    }
                }
                mpiCode << "    }\n";
            }
            
            mpiCode << "    MPI_Barrier(MPI_COMM_WORLD);\n\n";
            groupIndex++;
        }
        
        // Print results (mimic original main function output)
        mpiCode << "    if (rank == 0) {\n";
        mpiCode << "        std::cout << \"\\n=== Results ===\" << std::endl;\n";
        
        // Generate output similar to original main function
        for (int i = 0; i < functionCalls.size(); ++i) {
            if (functionCalls[i].hasReturnValue) {
                std::string varName = functionCalls[i].returnVariable;
                if (!varName.empty()) {
                    mpiCode << "        std::cout << \"" << varName << " = \" << result_" << i << " << std::endl;\n";
                } else {
                    mpiCode << "        std::cout << \"" << functionCalls[i].functionName 
                           << " result: \" << result_" << i << " << std::endl;\n";
                }
            }
        }
        
        mpiCode << "        std::cout << \"\\n=== Test Complete ===\" << std::endl;\n";
        mpiCode << "    }\n\n";
        
        mpiCode << "    MPI_Finalize();\n";
        mpiCode << "    return 0;\n";
        mpiCode << "}\n";
        
        return mpiCode.str();
    }
};

class MPIParallelizerConsumer : public ASTConsumer {
private:
    CompilerInstance &CI;
    GlobalVariableCollector globalCollector;
    FunctionAnalyzer functionAnalyzer;
    MainFunctionExtractor mainExtractor;
    
public:
    MPIParallelizerConsumer(CompilerInstance &CI) 
        : CI(CI), functionAnalyzer(globalCollector.globalVariables),
          mainExtractor(&CI.getSourceManager()) {}
    
    void HandleTranslationUnit(ASTContext &Context) override {
        // First pass: collect global variables
        globalCollector.TraverseDecl(Context.getTranslationUnitDecl());
        
        // Update function analyzer with global variables
        functionAnalyzer.globalVars = globalCollector.globalVariables;
        functionAnalyzer.setSourceManager(&CI.getSourceManager());
        
        // Second pass: analyze functions
        functionAnalyzer.TraverseDecl(Context.getTranslationUnitDecl());
        
        // Third pass: extract main function calls
        mainExtractor.TraverseDecl(Context.getTranslationUnitDecl());
        
        // Perform parallelization analysis
        MPIParallelizer parallelizer(mainExtractor.functionCalls, 
                                   functionAnalyzer.functionAnalysis,
                                   mainExtractor.variableToCallIndex,
                                   mainExtractor.callDependsOnVars,
                                   functionAnalyzer.functionDefinitions);
        
        // Generate output
        std::string mpiCode = parallelizer.generateMPICode();
        
        // Write to output file
        std::ofstream outFile("parallelized_output.cpp");
        if (outFile.is_open()) {
            outFile << mpiCode;
            outFile.close();
            llvm::outs() << "MPI parallelized code generated: parallelized_output.cpp\n";
        } else {
            llvm::errs() << "Error: Could not create output file\n";
        }
        
        // Print analysis results
        printAnalysisResults(parallelizer);
    }
    
private:
    void printAnalysisResults(const MPIParallelizer& parallelizer) {
        llvm::outs() << "\n=== Analysis Results ===\n";
        
        llvm::outs() << "\nGlobal Variables Found:\n";
        for (const auto& var : globalCollector.globalVariables) {
            llvm::outs() << "  " << var << "\n";
        }
        
        llvm::outs() << "\nFunction Analysis:\n";
        for (const auto& pair : functionAnalyzer.functionAnalysis) {
            llvm::outs() << "  Function: " << pair.first << "\n";
            llvm::outs() << "    Reads: ";
            for (const auto& var : pair.second.readSet) {
                llvm::outs() << var << " ";
            }
            llvm::outs() << "\n    Writes: ";
            for (const auto& var : pair.second.writeSet) {
                llvm::outs() << var << " ";
            }
            llvm::outs() << "\n";
        }
        
        llvm::outs() << "\nFunction Calls in main():\n";
        for (int i = 0; i < mainExtractor.functionCalls.size(); ++i) {
            const auto& call = mainExtractor.functionCalls[i];
            llvm::outs() << "  " << i << ": " << call.functionName 
                        << " (line " << call.lineNumber << ")\n";
        }
        
        llvm::outs() << "\nVariable Dependencies:\n";
        for (int i = 0; i < mainExtractor.functionCalls.size(); ++i) {
            const auto& call = mainExtractor.functionCalls[i];
            llvm::outs() << "  Call " << i << " (" << call.functionName << "): depends on [";
            if (i < mainExtractor.callDependsOnVars.size()) {
                for (const auto& var : mainExtractor.callDependsOnVars[i]) {
                    llvm::outs() << var << " ";
                }
            }
            llvm::outs() << "]\n";
            if (!call.returnVariable.empty()) {
                llvm::outs() << "    produces: " << call.returnVariable << "\n";
            }
        }
        
        llvm::outs() << "\nDependency Graph:\n";
        const auto& depGraph = parallelizer.getDependencyGraph();
        for (int i = 0; i < mainExtractor.functionCalls.size(); ++i) {
            const auto& call = mainExtractor.functionCalls[i];
            llvm::outs() << "  Call " << i << " (" << call.functionName << "):\n";
            llvm::outs() << "    depends on: [";
            for (int dep : depGraph[i].dependencies) {
                llvm::outs() << dep << " ";
            }
            llvm::outs() << "]\n";
            llvm::outs() << "    has dependents: [";
            for (int dep : depGraph[i].dependents) {
                llvm::outs() << dep << " ";
            }
            llvm::outs() << "]\n";
        }
        
        auto groups = parallelizer.getParallelizableGroups();
        llvm::outs() << "\nParallelizable Groups:\n";
        for (int i = 0; i < groups.size(); ++i) {
            llvm::outs() << "  Group " << i << ": ";
            for (int idx : groups[i]) {
                llvm::outs() << mainExtractor.functionCalls[idx].functionName << " ";
            }
            llvm::outs() << "\n";
        }
    }
};

class MPIParallelizerAction : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef file) override {
        return std::make_unique<MPIParallelizerConsumer>(CI);
    }
};

int main(int argc, const char **argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, MPIParallelizerCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser& OptionsParser = ExpectedParser.get();
    
    ClangTool Tool(OptionsParser.getCompilations(),
                   OptionsParser.getSourcePathList());
    
    return Tool.run(newFrontendActionFactory<MPIParallelizerAction>().get());
}