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
    std::string returnType;
    std::vector<std::string> parameterVariables; // Variables passed as parameters
    std::set<std::string> usedLocalVariables;    // Local variables used in this call
};

// Structure to hold local variable information
struct LocalVariable {
    std::string name;
    std::string type;
    int definedAtCall;        // Index of call that defines this variable (-1 if not from function call)
    std::set<int> usedInCalls; // Indices of calls that use this variable
    bool isParameter;         // True if this variable is passed as parameter to functions
};

// Structure to hold function analysis results
struct FunctionAnalysis {
    std::set<std::string> readSet;
    std::set<std::string> writeSet;
    std::set<std::string> localReads;  // Local variables read by this function
    std::set<std::string> localWrites; // Local variables written by this function
    bool isParallelizable = true;
    std::string returnType;
    std::vector<std::string> parameterTypes; // Types of function parameters
};

// Structure for dependency graph node
struct DependencyNode {
    std::string functionName;
    int callIndex;
    std::set<int> dependencies;  // indices of calls this depends on
    std::set<int> dependents;    // indices of calls that depend on this
    std::string dependencyReason; // Why this dependency exists
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
    std::set<std::string> currentFunctionParams;
    
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
            currentFunctionParams.clear();
            
            // Extract return type information
            QualType returnQType = FD->getReturnType();
            std::string returnTypeStr = returnQType.getAsString();
            functionAnalysis[currentFunction].returnType = returnTypeStr;
            
            // Extract parameter information
            for (unsigned i = 0; i < FD->getNumParams(); ++i) {
                ParmVarDecl *param = FD->getParamDecl(i);
                std::string paramName = param->getNameAsString();
                std::string paramType = param->getType().getAsString();
                currentFunctionParams.insert(paramName);
                functionAnalysis[currentFunction].parameterTypes.push_back(paramType);
            }
            
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
            } else if (VD->hasLocalStorage() && !currentFunctionParams.count(varName)) {
                // Local variable (not parameter)
                functionAnalysis[currentFunction].localReads.insert(varName);
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
                    } else if (VD->hasLocalStorage() && !currentFunctionParams.count(varName)) {
                        // Local variable (not parameter)
                        functionAnalysis[currentFunction].localWrites.insert(varName);
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
                    } else if (VD->hasLocalStorage() && !currentFunctionParams.count(varName)) {
                        // Local variable (not parameter)
                        functionAnalysis[currentFunction].localWrites.insert(varName);
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
    std::map<std::string, LocalVariable> localVariables; // Track all local variables in main
    std::map<std::string, FunctionAnalysis>* functionAnalysisPtr;
    
    MainFunctionExtractor(SourceManager *sourceManager) : SM(sourceManager), functionAnalysisPtr(nullptr) {}
    
    void setFunctionAnalysis(std::map<std::string, FunctionAnalysis>* analysis) {
        functionAnalysisPtr = analysis;
    }
    
    bool VisitFunctionDecl(FunctionDecl *FD) {
        if (FD->getNameAsString() == "main" && FD->hasBody()) {
            CompoundStmt *body = dyn_cast<CompoundStmt>(FD->getBody());
            if (body) {
                // First pass: collect all local variable declarations
                collectLocalVariables(body);
                
                // Second pass: process statements to build function calls and dependencies
                for (auto *stmt : body->body()) {
                    processStatement(stmt);
                }
                
                // Third pass: analyze local variable dependencies
                analyzeLocalDependencies();
                
                // Extract the main function body text
                SourceRange bodyRange = body->getSourceRange();
                mainFunctionBody = getSourceText(bodyRange);
            }
        }
        return true;
    }
    
private:
    void collectLocalVariables(CompoundStmt *body) {
        for (auto *stmt : body->body()) {
            collectLocalVariablesInStmt(stmt);
        }
    }
    
    void collectLocalVariablesInStmt(Stmt *stmt) {
        if (DeclStmt *DS = dyn_cast<DeclStmt>(stmt)) {
            for (auto *D : DS->decls()) {
                if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
                    LocalVariable localVar;
                    localVar.name = VD->getNameAsString();
                    localVar.type = VD->getType().getAsString();
                    localVar.definedAtCall = -1; // Will be updated if defined by function call
                    localVar.isParameter = false;
                    localVariables[localVar.name] = localVar;
                }
            }
        } else {
            // Recursively check compound statements
            for (auto *child : stmt->children()) {
                if (child) {
                    if (CompoundStmt *compound = dyn_cast<CompoundStmt>(child)) {
                        collectLocalVariablesInStmt(compound);
                    }
                }
            }
        }
    }
    
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
                                    
                                    // Get return type from function analysis
                                    if (functionAnalysisPtr && functionAnalysisPtr->count(funcName)) {
                                        call.returnType = (*functionAnalysisPtr)[funcName].returnType;
                                    } else {
                                        call.returnType = VD->getType().getAsString();
                                    }
                                    
                                    // Find variables used as parameters
                                    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                                        std::set<std::string> argVars;
                                        findUsedVariables(CE->getArg(i), argVars);
                                        for (const std::string& var : argVars) {
                                            call.parameterVariables.push_back(var);
                                            call.usedLocalVariables.insert(var);
                                        }
                                    }
                                    
                                    // Update local variable tracking
                                    if (localVariables.count(call.returnVariable)) {
                                        localVariables[call.returnVariable].definedAtCall = functionCalls.size();
                                    }
                                    
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
                    
                    // Get return type
                    if (call.hasReturnValue) {
                        if (functionAnalysisPtr && functionAnalysisPtr->count(funcName)) {
                            call.returnType = (*functionAnalysisPtr)[funcName].returnType;
                        } else {
                            call.returnType = CE->getType().getAsString();
                        }
                    } else {
                        call.returnType = "void";
                    }
                    
                    // Find variables used as parameters
                    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                        std::set<std::string> argVars;
                        findUsedVariables(CE->getArg(i), argVars);
                        for (const std::string& var : argVars) {
                            call.parameterVariables.push_back(var);
                            call.usedLocalVariables.insert(var);
                        }
                    }
                    
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
    
    void analyzeLocalDependencies() {
        // For each function call, mark which local variables it uses
        for (int i = 0; i < functionCalls.size(); ++i) {
            for (const std::string& usedVar : functionCalls[i].usedLocalVariables) {
                if (localVariables.count(usedVar)) {
                    localVariables[usedVar].usedInCalls.insert(i);
                }
            }
        }
        
        // Mark variables passed as parameters
        for (int i = 0; i < functionCalls.size(); ++i) {
            for (const std::string& paramVar : functionCalls[i].parameterVariables) {
                if (localVariables.count(paramVar)) {
                    localVariables[paramVar].isParameter = true;
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
    
public:
    // Getter for local variables (for debugging)
    const std::map<std::string, LocalVariable>& getLocalVariables() const {
        return localVariables;
    }
};

class MPIParallelizer {
private:
    std::vector<FunctionCall> functionCalls;
    std::map<std::string, FunctionAnalysis> functionAnalysis;
    std::vector<DependencyNode> dependencyGraph;
    std::map<std::string, LocalVariable> localVariables;
    std::map<std::string, std::string> functionDefinitions;
    
    // Helper function to normalize type names
    std::string normalizeType(const std::string& cppType) {
        if (cppType == "_Bool") return "bool";
        return cppType;
    }
    
    // Helper function to get MPI datatype from C++ type
    std::string getMPIDatatype(const std::string& cppType) {
        std::string normalizedType = normalizeType(cppType);
        if (normalizedType == "int") return "MPI_INT";
        if (normalizedType == "double") return "MPI_DOUBLE";
        if (normalizedType == "float") return "MPI_FLOAT";
        if (normalizedType == "bool") return "MPI_C_BOOL";
        if (normalizedType == "char") return "MPI_CHAR";
        if (normalizedType == "long") return "MPI_LONG";
        if (normalizedType == "unsigned int") return "MPI_UNSIGNED";
        if (normalizedType == "long long") return "MPI_LONG_LONG";
        // Default fallback
        return "MPI_INT";
    }
    
    // Helper function to get default value for a type
    std::string getDefaultValue(const std::string& cppType) {
        std::string normalizedType = normalizeType(cppType);
        if (normalizedType == "int") return "0";
        if (normalizedType == "double") return "0.0";
        if (normalizedType == "float") return "0.0f";
        if (normalizedType == "bool") return "false";
        if (normalizedType == "char") return "'\\0'";
        if (normalizedType == "long") return "0L";
        if (normalizedType == "unsigned int") return "0U";
        if (normalizedType == "long long") return "0LL";
        // Default fallback
        return "0";
    }
    
public:
    MPIParallelizer(const std::vector<FunctionCall>& calls, 
                   const std::map<std::string, FunctionAnalysis>& analysis,
                   const std::map<std::string, LocalVariable>& localVars,
                   const std::map<std::string, std::string>& funcDefs)
        : functionCalls(calls), functionAnalysis(analysis), 
          localVariables(localVars), functionDefinitions(funcDefs) {
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
        
        // Build dependencies based on local variable data flow
        for (int i = 0; i < functionCalls.size(); ++i) {
            for (int j = i + 1; j < functionCalls.size(); ++j) {
                std::string reason = "";
                bool hasDependency = false;
                
                // Check if call j uses a variable defined by call i
                if (functionCalls[i].hasReturnValue && !functionCalls[i].returnVariable.empty()) {
                    const std::string& producedVar = functionCalls[i].returnVariable;
                    if (functionCalls[j].usedLocalVariables.count(producedVar)) {
                        hasDependency = true;
                        reason = "Local variable data flow: " + producedVar;
                    }
                }
                
                // Check for global variable dependencies
                if (!hasDependency && functionAnalysis.count(functionCalls[i].functionName) && 
                    functionAnalysis.count(functionCalls[j].functionName)) {
                    
                    const auto& analysisA = functionAnalysis[functionCalls[i].functionName];
                    const auto& analysisB = functionAnalysis[functionCalls[j].functionName];
                    
                    // WAR: A writes, B reads
                    for (const auto& writeVar : analysisA.writeSet) {
                        if (analysisB.readSet.count(writeVar)) {
                            hasDependency = true;
                            reason = "Global variable WAR: " + writeVar;
                            break;
                        }
                    }
                    
                    // WAW: A writes, B writes
                    if (!hasDependency) {
                        for (const auto& writeVar : analysisA.writeSet) {
                            if (analysisB.writeSet.count(writeVar)) {
                                hasDependency = true;
                                reason = "Global variable WAW: " + writeVar;
                                break;
                            }
                        }
                    }
                    
                    // RAW: A reads, B writes
                    if (!hasDependency) {
                        for (const auto& readVar : analysisA.readSet) {
                            if (analysisB.writeSet.count(readVar)) {
                                hasDependency = true;
                                reason = "Global variable RAW: " + readVar;
                                break;
                            }
                        }
                    }
                }
                
                if (hasDependency) {
                    dependencyGraph[j].dependencies.insert(i);
                    dependencyGraph[i].dependents.insert(j);
                    if (dependencyGraph[j].dependencyReason.empty()) {
                        dependencyGraph[j].dependencyReason = reason;
                    } else {
                        dependencyGraph[j].dependencyReason += "; " + reason;
                    }
                }
            }
        }
    }
    
    std::vector<std::vector<int>> getParallelizableGroups() const {
        std::vector<std::vector<int>> groups;
        std::vector<bool> processed(functionCalls.size(), false);
        std::vector<int> inDegree(functionCalls.size());
        
        // Calculate in-degrees (number of dependencies)
        for (int i = 0; i < dependencyGraph.size(); ++i) {
            inDegree[i] = dependencyGraph[i].dependencies.size();
        }
        
        while (true) {
            // Find ALL nodes that are ready to execute (no unresolved dependencies)
            std::vector<int> readyNodes;
            for (int i = 0; i < functionCalls.size(); ++i) {
                if (!processed[i] && inDegree[i] == 0) {
                    readyNodes.push_back(i);
                }
            }
            
            if (readyNodes.empty()) {
                break; // No more nodes to process
            }
            
            // SIMPLE STRATEGY: All ready nodes can execute in parallel
            // The key insight: if a node has no dependencies, it can start immediately
            // Whether it has dependents doesn't matter - those dependents will wait
            groups.push_back(readyNodes);
            
            // Mark all ready nodes as processed and update dependents
            for (int nodeIdx : readyNodes) {
                processed[nodeIdx] = true;
                // Reduce in-degree for all nodes that depend on this one
                for (int dependent : dependencyGraph[nodeIdx].dependents) {
                    inDegree[dependent]--;
                }
            }
        }
        
        return groups;
    }
    
    // Public method to access dependency graph for debugging
    const std::vector<DependencyNode>& getDependencyGraph() const {
        return dependencyGraph;
    }
    
    // Public method to access local variables for debugging
    const std::map<std::string, LocalVariable>& getLocalVariables() const {
        return localVariables;
    }
    
    std::string generateMPICode() {
        auto parallelGroups = getParallelizableGroups();
        
        std::stringstream mpiCode;
        
        // Add MPI headers and initialization (simplified to avoid conflicts)
        mpiCode << "#include <mpi.h>\n";
        mpiCode << "#include <stdio.h>\n";
        mpiCode << "#include <iostream>\n\n";
        
        // Output the actual function definitions from the input file
        std::set<std::string> outputFunctions;
        for (const auto& call : functionCalls) {
            if (outputFunctions.find(call.functionName) == outputFunctions.end()) {
                outputFunctions.insert(call.functionName);
                
                if (functionDefinitions.count(call.functionName)) {
                    mpiCode << functionDefinitions[call.functionName] << "\n\n";
                } else {
                    // Fallback: generate a simple declaration based on return type
                    std::string returnType = "int"; // default
                    if (functionAnalysis.count(call.functionName)) {
                        returnType = normalizeType(functionAnalysis.at(call.functionName).returnType);
                    }
                    
                    mpiCode << "// Function definition not found for: " << call.functionName << "\n";
                    mpiCode << returnType << " " << call.functionName << "() {\n";
                    mpiCode << "    printf(\"Executing " << call.functionName << "\\n\");\n";
                    if (returnType != "void") {
                        mpiCode << "    return " << getDefaultValue(returnType) << ";\n";
                    }
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
        
        // Generate local variables from original main
        mpiCode << "    // Local variables from original main function\n";
        for (const auto& pair : localVariables) {
            const LocalVariable& localVar = pair.second;
            std::string defaultValue = getDefaultValue(localVar.type);
            mpiCode << "    " << localVar.type << " " << localVar.name << " = " << defaultValue << ";\n";
        }
        mpiCode << "\n";
        
        // Generate result variables for function calls
        for (int i = 0; i < functionCalls.size(); ++i) {
            if (functionCalls[i].hasReturnValue) {
                std::string returnType = normalizeType(functionCalls[i].returnType);
                std::string defaultValue = getDefaultValue(returnType);
                mpiCode << "    " << returnType << " result_" << i << " = " << defaultValue << ";\n";
            }
        }
        mpiCode << "\n";
        
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
                
                if (functionCalls[callIdx].hasReturnValue) {
                    std::string originalCall = functionCalls[callIdx].callExpression;
                    mpiCode << "        result_" << callIdx << " = " << extractFunctionCall(originalCall) << ";\n";
                    if (!functionCalls[callIdx].returnVariable.empty()) {
                        mpiCode << "        " << functionCalls[callIdx].returnVariable << " = result_" << callIdx << ";\n";
                    }
                } else {
                    std::string originalCall = functionCalls[callIdx].callExpression;
                    if (!originalCall.empty() && originalCall.back() == ';') {
                        originalCall.pop_back();
                    }
                    mpiCode << "        " << originalCall << ";\n";
                }
                mpiCode << "    }\n";
            } else {
                // Parallel execution for multiple functions
                for (int i = 0; i < group.size(); ++i) {
                    int callIdx = group[i];
                    mpiCode << "    if (rank == " << i << " && " << i << " < size) {\n";
                    
                    if (functionCalls[callIdx].hasReturnValue) {
                        std::string originalCall = functionCalls[callIdx].callExpression;
                        std::string funcCall = extractFunctionCall(originalCall);
                        mpiCode << "        result_" << callIdx << " = " << funcCall << ";\n";
                        
                        // Send result back to rank 0 if not rank 0
                        if (i != 0) {
                            std::string mpiType = getMPIDatatype(functionCalls[callIdx].returnType);
                            mpiCode << "        MPI_Send(&result_" << callIdx 
                                   << ", 1, " << mpiType << ", 0, " << callIdx << ", MPI_COMM_WORLD);\n";
                        }
                    } else {
                        // For void functions, use the original call expression
                        std::string originalCall = functionCalls[callIdx].callExpression;
                        if (!originalCall.empty() && originalCall.back() == ';') {
                            originalCall.pop_back();
                        }
                        mpiCode << "        " << originalCall << ";\n";
                    }
                    mpiCode << "    }\n";
                }
                
                // Collect results in rank 0
                mpiCode << "    if (rank == 0) {\n";
                for (int i = 1; i < group.size(); ++i) {
                    int callIdx = group[i];
                    if (functionCalls[callIdx].hasReturnValue) {
                        std::string mpiType = getMPIDatatype(functionCalls[callIdx].returnType);
                        mpiCode << "        MPI_Recv(&result_" << callIdx 
                               << ", 1, " << mpiType << ", " << i << ", " << callIdx 
                               << ", MPI_COMM_WORLD, MPI_STATUS_IGNORE);\n";
                    }
                }
                
                // Update local variables with results
                for (int i = 0; i < group.size(); ++i) {
                    int callIdx = group[i];
                    if (functionCalls[callIdx].hasReturnValue && !functionCalls[callIdx].returnVariable.empty()) {
                        mpiCode << "        " << functionCalls[callIdx].returnVariable << " = result_" << callIdx << ";\n";
                    }
                }
                mpiCode << "    }\n";
            }
            
            // Broadcast updated variables to all processes for subsequent groups
            mpiCode << "    // Broadcast updated variables to all processes\n";
            std::set<std::string> variablesToBroadcast;
            for (int i = 0; i < group.size(); ++i) {
                int callIdx = group[i];
                if (functionCalls[callIdx].hasReturnValue && !functionCalls[callIdx].returnVariable.empty()) {
                    variablesToBroadcast.insert(functionCalls[callIdx].returnVariable);
                }
            }
            
            for (const std::string& varName : variablesToBroadcast) {
                if (localVariables.count(varName)) {
                    std::string mpiType = getMPIDatatype(localVariables.at(varName).type);
                    mpiCode << "    MPI_Bcast(&" << varName << ", 1, " << mpiType << ", 0, MPI_COMM_WORLD);\n";
                }
            }
            
            mpiCode << "    MPI_Barrier(MPI_COMM_WORLD);\n\n";
            groupIndex++;
        }
        
        // Print results (mimic original main function output)
        mpiCode << "    if (rank == 0) {\n";
        mpiCode << "        std::cout << \"\\n=== Results ===\" << std::endl;\n";
        
        // Generate output for local variables
        for (const auto& pair : localVariables) {
            const LocalVariable& localVar = pair.second;
            if (localVar.definedAtCall >= 0) {
                mpiCode << "        std::cout << \"" << localVar.name << " = \" << " << localVar.name << " << std::endl;\n";
            }
        }
        
        // Generate output for function results
        for (int i = 0; i < functionCalls.size(); ++i) {
            if (functionCalls[i].hasReturnValue) {
                std::string varName = functionCalls[i].returnVariable;
                if (!varName.empty()) {
                    mpiCode << "        std::cout << \"" << varName << " = \" << " << varName << " << std::endl;\n";
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
    
private:
    std::string extractFunctionCall(const std::string& originalCall) {
        // Extract just the function call part if it's a variable assignment
        size_t equalPos = originalCall.find('=');
        if (equalPos != std::string::npos) {
            std::string funcCall = originalCall.substr(equalPos + 1);
            // Trim whitespace and semicolon
            size_t start = funcCall.find_first_not_of(" \t");
            size_t end = funcCall.find_last_not_of(" \t;");
            if (start != std::string::npos && end != std::string::npos) {
                return funcCall.substr(start, end - start + 1);
            }
        }
        // If no assignment found, return as is (minus trailing semicolon)
        std::string result = originalCall;
        if (!result.empty() && result.back() == ';') {
            result.pop_back();
        }
        return result;
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
        mainExtractor.setFunctionAnalysis(&functionAnalyzer.functionAnalysis);
        mainExtractor.TraverseDecl(Context.getTranslationUnitDecl());
        
        // Perform parallelization analysis
        MPIParallelizer parallelizer(mainExtractor.functionCalls, 
                                   functionAnalyzer.functionAnalysis,
                                   mainExtractor.getLocalVariables(),
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
        
        llvm::outs() << "\nLocal Variables Found:\n";
        const auto& localVars = parallelizer.getLocalVariables();
        for (const auto& pair : localVars) {
            const LocalVariable& localVar = pair.second;
            llvm::outs() << "  " << localVar.name << " (" << localVar.type << ")\n";
            llvm::outs() << "    Defined at call: " << localVar.definedAtCall << "\n";
            llvm::outs() << "    Used in calls: ";
            for (int callIdx : localVar.usedInCalls) {
                llvm::outs() << callIdx << " ";
            }
            llvm::outs() << "\n";
            llvm::outs() << "    Is parameter: " << (localVar.isParameter ? "yes" : "no") << "\n";
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
            llvm::outs() << "\n    Local Reads: ";
            for (const auto& var : pair.second.localReads) {
                llvm::outs() << var << " ";
            }
            llvm::outs() << "\n    Local Writes: ";
            for (const auto& var : pair.second.localWrites) {
                llvm::outs() << var << " ";
            }
            llvm::outs() << "\n";
        }
        
        llvm::outs() << "\nFunction Calls in main():\n";
        for (int i = 0; i < mainExtractor.functionCalls.size(); ++i) {
            const auto& call = mainExtractor.functionCalls[i];
            llvm::outs() << "  " << i << ": " << call.functionName 
                        << " (line " << call.lineNumber << ", returns " << call.returnType << ")\n";
            llvm::outs() << "    Parameters: ";
            for (const std::string& param : call.parameterVariables) {
                llvm::outs() << param << " ";
            }
            llvm::outs() << "\n    Used local vars: ";
            for (const std::string& var : call.usedLocalVariables) {
                llvm::outs() << var << " ";
            }
            llvm::outs() << "\n";
            if (call.hasReturnValue && !call.returnVariable.empty()) {
                llvm::outs() << "    Produces: " << call.returnVariable << " (" << call.returnType << ")\n";
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
            if (!depGraph[i].dependencyReason.empty()) {
                llvm::outs() << "    reason: " << depGraph[i].dependencyReason << "\n";
            }
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