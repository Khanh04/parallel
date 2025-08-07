// Disable LLVM command line options before any includes
#define LLVM_DISABLE_ABI_BREAKING_CHECKS_ENFORCING 1

// Include system headers first
#include <map>
#include <set>
#include <vector>
#include <string>
#include <algorithm>
#include <queue>
#include <fstream>
#include <sstream>
#include <regex>

// Include LLVM/Clang headers with option disabling
#include "llvm/Support/CommandLine.h"

// Hack to prevent command line option conflicts
namespace {
    struct CommandLineDisabler {
        CommandLineDisabler() {
            // This prevents the problematic option from being registered
            static bool disabled = false;
            if (!disabled) {
                llvm::cl::ResetCommandLineParser();
                disabled = true;
            }
        }
    };
    static CommandLineDisabler disabler;
}

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

// Structure to hold loop information for OpenMP parallelization
struct LoopInfo {
    std::string type;                    // "for", "while", "do-while"
    std::string source_code;             // Original loop source
    std::string loop_variable;           // Loop iterator variable
    std::vector<std::string> read_vars;  // Variables read in loop
    std::vector<std::string> write_vars; // Variables written in loop
    std::vector<std::string> reduction_vars; // Reduction variables
    std::string reduction_op;            // Reduction operation (+, *, etc.)
    bool has_dependencies;               // Loop-carried dependencies
    bool has_function_calls;             // Contains function calls
    bool has_io_operations;              // Contains I/O operations
    bool is_nested;                      // Is a nested loop
    bool parallelizable;                 // Can be parallelized
    std::string schedule_type;           // Recommended schedule
    std::string analysis_notes;          // Analysis details
    unsigned start_line, end_line;       // Source location
    unsigned start_col, end_col;         // Column positions
};

// Structure to hold function call information
struct FunctionCall {
    std::string functionName;
    std::string callExpression;
    unsigned lineNumber;
    bool hasReturnValue;
    std::string returnVariable;
    std::string returnType;
    std::vector<std::string> parameterVariables;
    std::set<std::string> usedLocalVariables;
};

// Structure to hold local variable information
struct LocalVariable {
    std::string name;
    std::string type;
    int definedAtCall;
    std::set<int> usedInCalls;
    bool isParameter;
};

// Structure to hold function analysis results
struct FunctionAnalysis {
    std::set<std::string> readSet;
    std::set<std::string> writeSet;
    std::set<std::string> localReads;
    std::set<std::string> localWrites;
    bool isParallelizable = true;
    std::string returnType;
    std::vector<std::string> parameterTypes;
};

// Structure for dependency graph node
struct DependencyNode {
    std::string functionName;
    int callIndex;
    std::set<int> dependencies;
    std::set<int> dependents;
    std::string dependencyReason;
};

// Loop analyzer visitor
class LoopAnalyzer : public RecursiveASTVisitor<LoopAnalyzer> {
private:
    SourceManager *SM;
    std::vector<LoopInfo> loops;
    bool insideLoop = false;
    int loopDepth = 0;
    
public:
    LoopAnalyzer(SourceManager *sourceManager) : SM(sourceManager) {}
    
    bool VisitForStmt(ForStmt *FS) {
        if (!insideLoop) {
            insideLoop = true;
            processForLoop(FS);
            insideLoop = false;
        } else {
            // Mark parent loop as nested
            if (!loops.empty()) {
                loops.back().is_nested = true;
            }
        }
        return true;
    }
    
    bool VisitWhileStmt(WhileStmt *WS) {
        if (!insideLoop) {
            insideLoop = true;
            processWhileLoop(WS);
            insideLoop = false;
        }
        return true;
    }
    
    bool VisitDoStmt(DoStmt *DS) {
        if (!insideLoop) {
            insideLoop = true;
            processDoWhileLoop(DS);
            insideLoop = false;
        }
        return true;
    }
    
    const std::vector<LoopInfo>& getLoops() const { return loops; }
    
private:
    void processForLoop(ForStmt *FS) {
        LoopInfo loop;
        loop.type = "for";
        
        // Get source location
        SourceLocation startLoc = FS->getBeginLoc();
        SourceLocation endLoc = FS->getEndLoc();
        
        loop.start_line = SM->getSpellingLineNumber(startLoc);
        loop.end_line = SM->getSpellingLineNumber(endLoc);
        loop.start_col = SM->getSpellingColumnNumber(startLoc);
        loop.end_col = SM->getSpellingColumnNumber(endLoc);
        
        // Extract source code
        loop.source_code = getSourceText(FS->getSourceRange());
        
        // Extract loop variable from init statement
        if (Stmt *Init = FS->getInit()) {
            if (DeclStmt *DS = dyn_cast<DeclStmt>(Init)) {
                for (auto *D : DS->decls()) {
                    if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
                        loop.loop_variable = VD->getNameAsString();
                        break;
                    }
                }
            }
        }
        
        // Analyze loop body
        analyzeLoopBody(FS->getBody(), loop);
        
        // Perform dependency analysis
        performDependencyAnalysis(loop);
        
        loops.push_back(loop);
    }
    
    void processWhileLoop(WhileStmt *WS) {
        LoopInfo loop;
        loop.type = "while";
        
        SourceLocation startLoc = WS->getBeginLoc();
        SourceLocation endLoc = WS->getEndLoc();
        
        loop.start_line = SM->getSpellingLineNumber(startLoc);
        loop.end_line = SM->getSpellingLineNumber(endLoc);
        
        loop.source_code = getSourceText(WS->getSourceRange());
        loop.parallelizable = false; // While loops are harder to parallelize
        loop.analysis_notes = "While loops require manual analysis for parallelization.";
        
        analyzeLoopBody(WS->getBody(), loop);
        
        loops.push_back(loop);
    }
    
    void processDoWhileLoop(DoStmt *DS) {
        LoopInfo loop;
        loop.type = "do-while";
        
        SourceLocation startLoc = DS->getBeginLoc();
        SourceLocation endLoc = DS->getEndLoc();
        
        loop.start_line = SM->getSpellingLineNumber(startLoc);
        loop.end_line = SM->getSpellingLineNumber(endLoc);
        
        loop.source_code = getSourceText(DS->getSourceRange());
        loop.parallelizable = false;
        loop.analysis_notes = "Do-while loops require manual analysis for parallelization.";
        
        analyzeLoopBody(DS->getBody(), loop);
        
        loops.push_back(loop);
    }
    
    void analyzeLoopBody(Stmt *body, LoopInfo &loop) {
        if (!body) return;
        
        // Initialize flags to false
        loop.has_function_calls = false;
        loop.has_io_operations = false;
        loop.is_nested = false;
        
        class LoopBodyVisitor : public RecursiveASTVisitor<LoopBodyVisitor> {
        public:
            LoopInfo *loop;
            std::string loopVar;
            
            LoopBodyVisitor(LoopInfo *l, const std::string &var) 
                : loop(l), loopVar(var) {}
            
            bool VisitDeclRefExpr(DeclRefExpr *DRE) {
                if (VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                    std::string varName = VD->getNameAsString();
                    // Don't add cout/cin as regular variables
                    if (varName != "cout" && varName != "cin" && varName != "endl") {
                        loop->read_vars.push_back(varName);
                    }
                }
                return true;
            }
            
            bool VisitBinaryOperator(BinaryOperator *BO) {
                if (BO->isAssignmentOp()) {
                    if (DeclRefExpr *LHS = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreImpCasts())) {
                        if (VarDecl *VD = dyn_cast<VarDecl>(LHS->getDecl())) {
                            std::string varName = VD->getNameAsString();
                            loop->write_vars.push_back(varName);
                            
                            // Check for max/min patterns in if statements
                            // Pattern: if (data[i] > max) max = data[i];
                            if (IfStmt *parentIf = findParentIf(BO)) {
                                if (BinaryOperator *condBO = dyn_cast<BinaryOperator>(parentIf->getCond())) {
                                    if (condBO->getOpcode() == BO_GT || condBO->getOpcode() == BO_LT ||
                                        condBO->getOpcode() == BO_GE || condBO->getOpcode() == BO_LE) {
                                        // This could be a max/min reduction
                                        loop->reduction_vars.push_back(varName);
                                        if (condBO->getOpcode() == BO_GT || condBO->getOpcode() == BO_GE) {
                                            loop->reduction_op = "max";
                                        } else {
                                            loop->reduction_op = "min";
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                return true;
            }
            
            // Helper to find parent if statement (simplified check)
            IfStmt* findParentIf(Stmt *s) {
                // This is a simplified version - in real implementation would need proper parent tracking
                return nullptr;
            }
            
            bool VisitCompoundAssignOperator(CompoundAssignOperator *CAO) {
                if (DeclRefExpr *LHS = dyn_cast<DeclRefExpr>(CAO->getLHS()->IgnoreImpCasts())) {
                    if (VarDecl *VD = dyn_cast<VarDecl>(LHS->getDecl())) {
                        std::string varName = VD->getNameAsString();
                        loop->reduction_vars.push_back(varName);
                        
                        // Determine reduction operation
                        switch (CAO->getOpcode()) {
                            case BO_AddAssign: loop->reduction_op = "+"; break;
                            case BO_SubAssign: loop->reduction_op = "-"; break;
                            case BO_MulAssign: loop->reduction_op = "*"; break;
                            case BO_AndAssign: loop->reduction_op = "&"; break;
                            case BO_OrAssign: loop->reduction_op = "|"; break;
                            case BO_XorAssign: loop->reduction_op = "^"; break;
                            default: loop->reduction_op = "+"; break;
                        }
                    }
                }
                return true;
            }
            
            bool VisitCallExpr(CallExpr *CE) {
                if (FunctionDecl *FD = CE->getDirectCallee()) {
                    std::string funcName = FD->getNameAsString();
                    
                    // Check for I/O operations - be more specific
                    if (funcName == "printf" || funcName == "scanf" ||
                        funcName == "puts" || funcName == "gets" ||
                        funcName == "fprintf" || funcName == "fscanf" ||
                        funcName == "fread" || funcName == "fwrite") {
                        loop->has_io_operations = true;
                    } else if (funcName == "sin" || funcName == "cos" || 
                               funcName == "exp" || funcName == "sqrt" ||
                               funcName == "pow" || funcName == "log") {
                        // Math functions are safe for parallelization
                        loop->has_function_calls = true;
                    } else {
                        // Other function calls
                        loop->has_function_calls = true;
                    }
                }
                return true;
            }
            
            // Check for C++ stream I/O
            bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *CE) {
                if (FunctionDecl *FD = CE->getDirectCallee()) {
                    std::string opName = FD->getNameAsString();
                    // Check for << or >> operators
                    if (opName.find("operator<<") != std::string::npos ||
                        opName.find("operator>>") != std::string::npos) {
                        loop->has_io_operations = true;
                        return true;
                    }
                }
                return true;
            }
            
            // Also check for member calls like std::cout
            bool VisitCXXMemberCallExpr(CXXMemberCallExpr *CE) {
                if (CXXMethodDecl *MD = CE->getMethodDecl()) {
                    std::string className = "";
                    if (CXXRecordDecl *RD = MD->getParent()) {
                        className = RD->getNameAsString();
                    }
                    // Check if it's a stream class
                    if (className.find("stream") != std::string::npos ||
                        className.find("ostream") != std::string::npos ||
                        className.find("istream") != std::string::npos) {
                        loop->has_io_operations = true;
                    }
                }
                return true;
            }
            
            bool VisitForStmt(ForStmt *FS) {
                loop->is_nested = true;
                return true;
            }
            
            bool VisitWhileStmt(WhileStmt *WS) {
                loop->is_nested = true;
                return true;
            }
            
            bool VisitDoStmt(DoStmt *DS) {
                loop->is_nested = true;
                return true;
            }
        };
        
        LoopBodyVisitor visitor(&loop, loop.loop_variable);
        visitor.TraverseStmt(body);
    }
    
    void performDependencyAnalysis(LoopInfo &loop) {
        // Remove duplicates
        std::sort(loop.read_vars.begin(), loop.read_vars.end());
        loop.read_vars.erase(std::unique(loop.read_vars.begin(), loop.read_vars.end()), loop.read_vars.end());
        
        std::sort(loop.write_vars.begin(), loop.write_vars.end());
        loop.write_vars.erase(std::unique(loop.write_vars.begin(), loop.write_vars.end()), loop.write_vars.end());
        
        std::sort(loop.reduction_vars.begin(), loop.reduction_vars.end());
        loop.reduction_vars.erase(std::unique(loop.reduction_vars.begin(), loop.reduction_vars.end()), loop.reduction_vars.end());
        
        // Check for loop-carried dependencies
        loop.has_dependencies = false;
        std::string code = loop.source_code;
        
        // Look for array access patterns with loop variable offsets
        if (!loop.loop_variable.empty()) {
            // Pattern: array[i-k] or array[i+k] where k is a constant
            std::regex dependency_pattern(R"((\w+)\s*\[\s*)" + loop.loop_variable + R"(\s*[-+]\s*\d+\s*\])");
            if (std::regex_search(code, dependency_pattern)) {
                // Check if the same array is being written to in the loop
                std::smatch matches;
                std::string temp_code = code;
                while (std::regex_search(temp_code, matches, dependency_pattern)) {
                    std::string array_name = matches[1];
                    // Check if this array is also written to
                    std::regex write_pattern(array_name + R"(\s*\[\s*)" + loop.loop_variable + R"(\s*\]\s*=)");
                    if (std::regex_search(code, write_pattern)) {
                        loop.has_dependencies = true;
                        break;
                    }
                    temp_code = matches.suffix();
                }
            }
        }
        
        // Additional check for sum reduction pattern if not already detected
        if (loop.reduction_vars.empty() && !loop.loop_variable.empty()) {
            // Look for pattern: sum += ...
            std::regex sum_pattern(R"((\w+)\s*\+=)");
            std::smatch match;
            if (std::regex_search(code, match, sum_pattern)) {
                std::string var = match[1];
                // Make sure this variable is not an array element
                if (!std::regex_search(code, std::regex(var + R"(\s*\[)"))) {
                    loop.reduction_vars.push_back(var);
                    loop.reduction_op = "+";
                }
            }
        }
        
        // Determine if parallelizable
        if (loop.type == "for") {
            loop.parallelizable = true;
            loop.analysis_notes = "";
            
            if (loop.has_io_operations) {
                loop.parallelizable = false;
                loop.analysis_notes += "Contains I/O operations - not parallelizable. ";
            }
            
            if (loop.has_dependencies && loop.reduction_vars.empty()) {
                loop.parallelizable = false;
                loop.analysis_notes += "Has loop-carried dependencies - not parallelizable. ";
            }
            
            if (!loop.reduction_vars.empty()) {
                loop.parallelizable = true;
                loop.analysis_notes += "Contains reduction operations - parallelizable with reduction clause. ";
            }
            
            if (loop.is_nested && loop.parallelizable) {
                loop.analysis_notes += "Nested loop structure detected. ";
            }
            
            if (loop.has_function_calls && !loop.has_io_operations && loop.parallelizable) {
                loop.analysis_notes += "Contains function calls - verify they are thread-safe. ";
            }
            
            if (loop.parallelizable) {
                loop.analysis_notes += "PARALLELIZABLE - OpenMP pragma will be added. ";
                // Choose schedule type
                if (loop.is_nested) {
                    loop.schedule_type = "static";  // Better cache locality for nested loops
                } else if (loop.has_function_calls) {
                    loop.schedule_type = "dynamic"; // Better for variable workloads
                } else {
                    loop.schedule_type = "static";  // Default for simple loops
                }
            } else {
                loop.analysis_notes += "NOT PARALLELIZABLE - no pragma added. ";
            }
        } else {
            loop.parallelizable = false;
            loop.analysis_notes = "Only for-loops are automatically parallelizable. ";
        }
    }
    
    std::string getSourceText(SourceRange range) {
        if (!SM || range.isInvalid()) return "";
        return std::string(Lexer::getSourceText(
            CharSourceRange::getTokenRange(range), *SM, LangOptions()));
    }
};

class GlobalVariableCollector : public RecursiveASTVisitor<GlobalVariableCollector> {
public:
    std::set<std::string> globalVariables;
    
    bool VisitVarDecl(VarDecl *VD) {
        if (VD->hasGlobalStorage() && !VD->isStaticLocal()) {
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
    std::map<std::string, std::string> functionDefinitions;
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
            
            QualType returnQType = FD->getReturnType();
            std::string returnTypeStr = returnQType.getAsString();
            functionAnalysis[currentFunction].returnType = returnTypeStr;
            
            for (unsigned i = 0; i < FD->getNumParams(); ++i) {
                ParmVarDecl *param = FD->getParamDecl(i);
                std::string paramName = param->getNameAsString();
                std::string paramType = param->getType().getAsString();
                currentFunctionParams.insert(paramName);
                functionAnalysis[currentFunction].parameterTypes.push_back(paramType);
            }
            
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
    std::map<std::string, LocalVariable> localVariables;
    std::map<std::string, FunctionAnalysis>* functionAnalysisPtr;
    std::vector<LoopInfo> mainLoops; // Loops found in main function
    
    MainFunctionExtractor(SourceManager *sourceManager) : SM(sourceManager), functionAnalysisPtr(nullptr) {}
    
    void setFunctionAnalysis(std::map<std::string, FunctionAnalysis>* analysis) {
        functionAnalysisPtr = analysis;
    }
    
    bool VisitFunctionDecl(FunctionDecl *FD) {
        if (FD->getNameAsString() == "main" && FD->hasBody()) {
            // First analyze loops in main
            LoopAnalyzer loopAnalyzer(SM);
            loopAnalyzer.TraverseStmt(FD->getBody());
            mainLoops = loopAnalyzer.getLoops();
            
            CompoundStmt *body = dyn_cast<CompoundStmt>(FD->getBody());
            if (body) {
                collectLocalVariables(body);
                
                for (auto *stmt : body->body()) {
                    processStatement(stmt);
                }
                
                analyzeLocalDependencies();
                
                SourceRange bodyRange = body->getSourceRange();
                mainFunctionBody = getSourceText(bodyRange);
            }
        }
        return true;
    }
    
    const std::vector<LoopInfo>& getMainLoops() const { return mainLoops; }
    
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
                    localVar.definedAtCall = -1;
                    localVar.isParameter = false;
                    localVariables[localVar.name] = localVar;
                }
            }
        } else {
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
                                    
                                    if (functionAnalysisPtr && functionAnalysisPtr->count(funcName)) {
                                        call.returnType = (*functionAnalysisPtr)[funcName].returnType;
                                    } else {
                                        call.returnType = VD->getType().getAsString();
                                    }
                                    
                                    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
                                        std::set<std::string> argVars;
                                        findUsedVariables(CE->getArg(i), argVars);
                                        for (const std::string& var : argVars) {
                                            call.parameterVariables.push_back(var);
                                            call.usedLocalVariables.insert(var);
                                        }
                                    }
                                    
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
            if (FunctionDecl *FD = CE->getDirectCallee()) {
                std::string funcName = FD->getNameAsString();
                
                if (isUserFunction(funcName)) {
                    FunctionCall call;
                    call.functionName = funcName;
                    call.callExpression = getSourceText(CE->getSourceRange());
                    call.lineNumber = SM->getSpellingLineNumber(CE->getBeginLoc());
                    call.hasReturnValue = !CE->getType()->isVoidType();
                    
                    if (call.hasReturnValue) {
                        if (functionAnalysisPtr && functionAnalysisPtr->count(funcName)) {
                            call.returnType = (*functionAnalysisPtr)[funcName].returnType;
                        } else {
                            call.returnType = CE->getType().getAsString();
                        }
                    } else {
                        call.returnType = "void";
                    }
                    
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
            for (auto *child : stmt->children()) {
                if (child) {
                    processStatement(child);
                }
            }
        }
    }
    
    void analyzeLocalDependencies() {
        for (int i = 0; i < functionCalls.size(); ++i) {
            for (const std::string& usedVar : functionCalls[i].usedLocalVariables) {
                if (localVariables.count(usedVar)) {
                    localVariables[usedVar].usedInCalls.insert(i);
                }
            }
        }
        
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
               funcName.find("__") != 0;
    }
    
    void findUsedVariables(Expr *expr, std::set<std::string>& usedVars) {
        if (!expr) return;
        
        if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(expr)) {
            if (VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                usedVars.insert(VD->getNameAsString());
            }
        }
        
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
    const std::map<std::string, LocalVariable>& getLocalVariables() const {
        return localVariables;
    }
};

class HybridParallelizer {
private:
    std::vector<FunctionCall> functionCalls;
    std::map<std::string, FunctionAnalysis> functionAnalysis;
    std::vector<DependencyNode> dependencyGraph;
    std::map<std::string, LocalVariable> localVariables;
    std::map<std::string, std::string> functionDefinitions;
    std::vector<LoopInfo> mainLoops;
    
    std::string normalizeType(const std::string& cppType) {
        if (cppType == "_Bool") return "bool";
        return cppType;
    }
    
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
        return "MPI_INT";
    }
    
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
        return "0";
    }
    
public:
    HybridParallelizer(const std::vector<FunctionCall>& calls, 
                      const std::map<std::string, FunctionAnalysis>& analysis,
                      const std::map<std::string, LocalVariable>& localVars,
                      const std::map<std::string, std::string>& funcDefs,
                      const std::vector<LoopInfo>& loops)
        : functionCalls(calls), functionAnalysis(analysis), 
          localVariables(localVars), functionDefinitions(funcDefs),
          mainLoops(loops) {
        buildDependencyGraph();
    }
    
    void buildDependencyGraph() {
        dependencyGraph.clear();
        
        for (int i = 0; i < functionCalls.size(); ++i) {
            DependencyNode node;
            node.functionName = functionCalls[i].functionName;
            node.callIndex = i;
            dependencyGraph.push_back(node);
        }
        
        for (int i = 0; i < functionCalls.size(); ++i) {
            for (int j = i + 1; j < functionCalls.size(); ++j) {
                std::string reason = "";
                bool hasDependency = false;
                
                if (functionCalls[i].hasReturnValue && !functionCalls[i].returnVariable.empty()) {
                    const std::string& producedVar = functionCalls[i].returnVariable;
                    if (functionCalls[j].usedLocalVariables.count(producedVar)) {
                        hasDependency = true;
                        reason = "Local variable data flow: " + producedVar;
                    }
                }
                
                if (!hasDependency && functionAnalysis.count(functionCalls[i].functionName) && 
                    functionAnalysis.count(functionCalls[j].functionName)) {
                    
                    const auto& analysisA = functionAnalysis[functionCalls[i].functionName];
                    const auto& analysisB = functionAnalysis[functionCalls[j].functionName];
                    
                    for (const auto& writeVar : analysisA.writeSet) {
                        if (analysisB.readSet.count(writeVar)) {
                            hasDependency = true;
                            reason = "Global variable WAR: " + writeVar;
                            break;
                        }
                    }
                    
                    if (!hasDependency) {
                        for (const auto& writeVar : analysisA.writeSet) {
                            if (analysisB.writeSet.count(writeVar)) {
                                hasDependency = true;
                                reason = "Global variable WAW: " + writeVar;
                                break;
                            }
                        }
                    }
                    
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
        
        for (int i = 0; i < dependencyGraph.size(); ++i) {
            inDegree[i] = dependencyGraph[i].dependencies.size();
        }
        
        while (true) {
            std::vector<int> readyNodes;
            for (int i = 0; i < functionCalls.size(); ++i) {
                if (!processed[i] && inDegree[i] == 0) {
                    readyNodes.push_back(i);
                }
            }
            
            if (readyNodes.empty()) {
                break;
            }
            
            groups.push_back(readyNodes);
            
            for (int nodeIdx : readyNodes) {
                processed[nodeIdx] = true;
                for (int dependent : dependencyGraph[nodeIdx].dependents) {
                    inDegree[dependent]--;
                }
            }
        }
        
        return groups;
    }
    
    const std::vector<DependencyNode>& getDependencyGraph() const {
        return dependencyGraph;
    }
    
    const std::map<std::string, LocalVariable>& getLocalVariables() const {
        return localVariables;
    }
    
    std::string generateOpenMPPragma(const LoopInfo& loop) {
        std::stringstream pragma;
        pragma << "#pragma omp parallel for";
        
        if (!loop.reduction_vars.empty()) {
            // Group reduction variables by operation type
            std::map<std::string, std::vector<std::string>> reductionGroups;
            
            // Default to the stored reduction_op if available
            std::string op = loop.reduction_op.empty() ? "+" : loop.reduction_op;
            
            for (const auto& var : loop.reduction_vars) {
                reductionGroups[op].push_back(var);
            }
            
            // Add reduction clauses for each operation type
            for (const auto& group : reductionGroups) {
                pragma << " reduction(" << group.first << ":";
                for (size_t i = 0; i < group.second.size(); i++) {
                    if (i > 0) pragma << ",";
                    pragma << group.second[i];
                }
                pragma << ")";
            }
        }
        
        if (!loop.loop_variable.empty()) {
            pragma << " private(" << loop.loop_variable << ")";
        }
        
        pragma << " schedule(" << loop.schedule_type;
        if (loop.schedule_type == "dynamic") {
            pragma << ",100";  // Smaller chunk size for better load balancing
        }
        pragma << ")";
        
        return pragma.str();
    }
    
    std::string generateHybridMPIOpenMPCode() {
        auto parallelGroups = getParallelizableGroups();
        
        std::stringstream mpiCode;
        
        // Headers
        mpiCode << "#include <mpi.h>\n";
        mpiCode << "#include <omp.h>\n";
        mpiCode << "#include <stdio.h>\n";
        mpiCode << "#include <iostream>\n";
        mpiCode << "#include <vector>\n";
        mpiCode << "#include <cmath>\n\n";
        
        // Output function definitions
        std::set<std::string> outputFunctions;
        for (const auto& call : functionCalls) {
            if (outputFunctions.find(call.functionName) == outputFunctions.end()) {
                outputFunctions.insert(call.functionName);
                
                if (functionDefinitions.count(call.functionName)) {
                    mpiCode << functionDefinitions[call.functionName] << "\n\n";
                } else {
                    std::string returnType = "int";
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
        
        // Generate main function with MPI and OpenMP
        mpiCode << "int main(int argc, char* argv[]) {\n";
        mpiCode << "    int rank, size, provided;\n";
        mpiCode << "    \n";
        mpiCode << "    // Initialize MPI with thread support\n";
        mpiCode << "    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);\n";
        mpiCode << "    MPI_Comm_rank(MPI_COMM_WORLD, &rank);\n";
        mpiCode << "    MPI_Comm_size(MPI_COMM_WORLD, &size);\n\n";
        
        mpiCode << "    if (rank == 0) {\n";
        mpiCode << "        std::cout << \"=== Hybrid MPI/OpenMP Parallelized Program ===\" << std::endl;\n";
        mpiCode << "        std::cout << \"MPI processes: \" << size << std::endl;\n";
        mpiCode << "        std::cout << \"OpenMP threads per process: \" << omp_get_max_threads() << std::endl;\n";
        mpiCode << "    }\n\n";
        
        // Generate local variables
        mpiCode << "    // Local variables from original main function\n";
        for (const auto& pair : localVariables) {
            const LocalVariable& localVar = pair.second;
            std::string defaultValue = getDefaultValue(localVar.type);
            mpiCode << "    " << localVar.type << " " << localVar.name << " = " << defaultValue << ";\n";
        }
        mpiCode << "\n";
        
        // Generate result variables
        for (int i = 0; i < functionCalls.size(); ++i) {
            if (functionCalls[i].hasReturnValue) {
                std::string returnType = normalizeType(functionCalls[i].returnType);
                std::string defaultValue = getDefaultValue(returnType);
                mpiCode << "    " << returnType << " result_" << i << " = " << defaultValue << ";\n";
            }
        }
        mpiCode << "\n";
        
        // Track which lines have been processed (for loop integration)
        std::set<unsigned> processedLines;
        
        // Generate parallel execution logic with integrated OpenMP loops
        int groupIndex = 0;
        for (const auto& group : parallelGroups) {
            mpiCode << "    // === Parallel group " << groupIndex << " ===\n";
            mpiCode << "    if (rank == 0) {\n";
            mpiCode << "        std::cout << \"\\n--- Executing Group " << groupIndex << " ---\" << std::endl;\n";
            mpiCode << "    }\n";
            
            // Check if any loops should be inserted before this group
            for (const auto& loop : mainLoops) {
                if (processedLines.find(loop.start_line) == processedLines.end()) {
                    bool shouldInsertLoop = true;
                    
                    // Check if loop should come before any function in this group
                    for (int callIdx : group) {
                        if (loop.start_line > functionCalls[callIdx].lineNumber) {
                            shouldInsertLoop = false;
                            break;
                        }
                    }
                    
                    if (shouldInsertLoop) {
                        mpiCode << "\n    // OpenMP parallelized loop from line " << loop.start_line << "\n";
                        if (loop.parallelizable) {
                            mpiCode << "    " << generateOpenMPPragma(loop) << "\n";
                        }
                        mpiCode << "    " << loop.source_code << "\n\n";
                        processedLines.insert(loop.start_line);
                    }
                }
            }
            
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
                // Parallel execution with MPI for multiple functions
                for (int i = 0; i < group.size(); ++i) {
                    int callIdx = group[i];
                    mpiCode << "    if (rank == " << i << " && " << i << " < size) {\n";
                    
                    if (functionCalls[callIdx].hasReturnValue) {
                        std::string originalCall = functionCalls[callIdx].callExpression;
                        std::string funcCall = extractFunctionCall(originalCall);
                        mpiCode << "        result_" << callIdx << " = " << funcCall << ";\n";
                        
                        if (i != 0) {
                            std::string mpiType = getMPIDatatype(functionCalls[callIdx].returnType);
                            mpiCode << "        MPI_Send(&result_" << callIdx 
                                   << ", 1, " << mpiType << ", 0, " << callIdx << ", MPI_COMM_WORLD);\n";
                        }
                    } else {
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
                
                for (int i = 0; i < group.size(); ++i) {
                    int callIdx = group[i];
                    if (functionCalls[callIdx].hasReturnValue && !functionCalls[callIdx].returnVariable.empty()) {
                        mpiCode << "        " << functionCalls[callIdx].returnVariable << " = result_" << callIdx << ";\n";
                    }
                }
                mpiCode << "    }\n";
            }
            
            // Broadcast updated variables
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
        
        // Insert any remaining loops that haven't been processed
        for (const auto& loop : mainLoops) {
            if (processedLines.find(loop.start_line) == processedLines.end()) {
                mpiCode << "\n    // OpenMP parallelized loop from line " << loop.start_line << "\n";
                if (loop.parallelizable) {
                    mpiCode << "    " << generateOpenMPPragma(loop) << "\n";
                }
                mpiCode << "    " << loop.source_code << "\n\n";
                processedLines.insert(loop.start_line);
            }
        }
        
        // Print results
        mpiCode << "    if (rank == 0) {\n";
        mpiCode << "        std::cout << \"\\n=== Results ===\" << std::endl;\n";
        
        for (const auto& pair : localVariables) {
            const LocalVariable& localVar = pair.second;
            if (localVar.definedAtCall >= 0) {
                mpiCode << "        std::cout << \"" << localVar.name << " = \" << " << localVar.name << " << std::endl;\n";
            }
        }
        
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
        
        mpiCode << "        std::cout << \"\\n=== Hybrid MPI/OpenMP Execution Complete ===\" << std::endl;\n";
        mpiCode << "    }\n\n";
        
        mpiCode << "    MPI_Finalize();\n";
        mpiCode << "    return 0;\n";
        mpiCode << "}\n";
        
        return mpiCode.str();
    }
    
private:
    std::string extractFunctionCall(const std::string& originalCall) {
        size_t equalPos = originalCall.find('=');
        if (equalPos != std::string::npos) {
            std::string funcCall = originalCall.substr(equalPos + 1);
            size_t start = funcCall.find_first_not_of(" \t");
            size_t end = funcCall.find_last_not_of(" \t;");
            if (start != std::string::npos && end != std::string::npos) {
                return funcCall.substr(start, end - start + 1);
            }
        }
        std::string result = originalCall;
        if (!result.empty() && result.back() == ';') {
            result.pop_back();
        }
        return result;
    }
};

class HybridParallelizerConsumer : public ASTConsumer {
private:
    CompilerInstance &CI;
    GlobalVariableCollector globalCollector;
    FunctionAnalyzer functionAnalyzer;
    MainFunctionExtractor mainExtractor;
    
public:
    HybridParallelizerConsumer(CompilerInstance &CI) 
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
        
        // Third pass: extract main function calls and loops
        mainExtractor.setFunctionAnalysis(&functionAnalyzer.functionAnalysis);
        mainExtractor.TraverseDecl(Context.getTranslationUnitDecl());
        
        // Perform parallelization analysis
        HybridParallelizer parallelizer(mainExtractor.functionCalls, 
                                       functionAnalyzer.functionAnalysis,
                                       mainExtractor.getLocalVariables(),
                                       functionAnalyzer.functionDefinitions,
                                       mainExtractor.getMainLoops());
        
        // Generate output
        std::string hybridCode = parallelizer.generateHybridMPIOpenMPCode();
        
        // Write to output file
        std::ofstream outFile("hybrid_mpi_openmp_output.cpp");
        if (outFile.is_open()) {
            outFile << hybridCode;
            outFile.close();
            llvm::outs() << "Hybrid MPI/OpenMP parallelized code generated: hybrid_mpi_openmp_output.cpp\n";
        } else {
            llvm::errs() << "Error: Could not create output file\n";
        }
        
        // Print analysis results
        printAnalysisResults(parallelizer);
    }
    
private:
    void printAnalysisResults(const HybridParallelizer& parallelizer) {
        llvm::outs() << "\n=== Hybrid MPI/OpenMP Analysis Results ===\n";
        
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
        
        llvm::outs() << "\nLoops Found in main():\n";
        for (const auto& loop : mainExtractor.getMainLoops()) {
            llvm::outs() << "  Loop at lines " << loop.start_line << "-" << loop.end_line << ":\n";
            llvm::outs() << "    Type: " << loop.type << "\n";
            llvm::outs() << "    Parallelizable: " << (loop.parallelizable ? "YES" : "NO") << "\n";
            if (loop.parallelizable) {
                llvm::outs() << "    OpenMP Schedule: " << loop.schedule_type << "\n";
                if (!loop.reduction_vars.empty()) {
                    llvm::outs() << "    Reduction variables: ";
                    for (const auto& var : loop.reduction_vars) {
                        llvm::outs() << var << " ";
                    }
                    llvm::outs() << "\n";
                }
            }
            llvm::outs() << "    Analysis: " << loop.analysis_notes << "\n";
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
        
        llvm::outs() << "\n=== Hybrid parallelization complete! ===\n";
        llvm::outs() << "The generated code combines:\n";
        llvm::outs() << "  - MPI for function-level parallelism\n";
        llvm::outs() << "  - OpenMP for loop-level parallelism\n";
    }
};

class HybridParallelizerAction : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef file) override {
        return std::make_unique<HybridParallelizerConsumer>(CI);
    }
};

int main(int argc, const char **argv) {
    if (argc < 2) {
        llvm::errs() << "Usage: " << argv[0] << " <source-file>\n";
        llvm::errs() << "\nThis tool generates hybrid MPI/OpenMP parallelized code:\n";
        llvm::errs() << "  - MPI for parallelizing independent function calls\n";
        llvm::errs() << "  - OpenMP for parallelizing for loops\n";
        return 1;
    }

    std::vector<std::string> sources;
    for (int i = 1; i < argc; ++i) {
        sources.push_back(argv[i]);
    }

    std::vector<std::string> compileCommands = {
        "clang++", "-fsyntax-only", "-std=c++17"
    };
    
    auto Compilations = std::make_unique<clang::tooling::FixedCompilationDatabase>(
        ".", compileCommands);
    
    ClangTool Tool(*Compilations, sources);
    
    return Tool.run(newFrontendActionFactory<HybridParallelizerAction>().get());
}