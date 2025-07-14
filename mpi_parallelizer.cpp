// MPI Parallelizer with Parameter Independence Analysis

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>
#include <set>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <map>

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory MPIToolCategory("MPI Parallelization Tool");

// Command line options
static cl::opt<std::string> OutputFile("o",
    cl::desc("Output file for transformed code (default: parallel_<input>.cpp)"),
    cl::value_desc("filename"),
    cl::cat(MPIToolCategory));

static cl::opt<bool> SilentMode("silent", 
    cl::desc("Suppress debug output"), 
    cl::cat(MPIToolCategory));

static cl::opt<bool> VerboseMode("verbose", 
    cl::desc("Show detailed analysis"), 
    cl::cat(MPIToolCategory));

static cl::opt<bool> CompileAndRun("run",
    cl::desc("Automatically compile and run the generated code"),
    cl::cat(MPIToolCategory));

// Removed --allow-params flag - now always analyzes parameters for safety

// Global variables to store input filename for output generation
std::string InputFileName;

enum class ParameterType {
    LITERAL,        // Literal values (1, 2.5, "hello")
    CONSTANT,       // const variables or #define
    INDEPENDENT,    // Independent local variables
    SHARED_READONLY, // Shared but read-only variables
    SHARED_MUTABLE  // Shared mutable variables (blocks parallelization)
};

struct ParameterInfo {
    std::string name;
    ParameterType type;
    std::string sourceText;
    bool isModified;
};

struct AutoCallInfo {
    CallExpr* expr;
    std::string functionName;
    SourceLocation location;
    std::string sourceText;
    bool hasReturnType;
    bool hasParameters;
    std::vector<ParameterInfo> parameters;
    std::vector<std::string> parameterVars;  // Keep for backward compatibility
    int statementIndex;
    bool isAssignedToVariable;
    bool canBeParallelized;
    std::string parallelizationReason;
    std::set<std::string> requiredVariables; // Variables that must be declared before this call
};

class VariableAnalyzer : public RecursiveASTVisitor<VariableAnalyzer> {
private:
    ASTContext* context;
    std::map<std::string, bool> variableModifications;
    std::set<std::string> constVariables;
    std::set<std::string> globalVariables;
    std::map<std::string, VarDecl*> allVariables;
    std::set<std::string> functionsModifyingGlobals;

public:
    VariableAnalyzer(ASTContext* ctx) : context(ctx) {}

    bool VisitVarDecl(VarDecl* var) {
        std::string varName = var->getNameAsString();
        allVariables[varName] = var;
        
        // Check if it's a const variable
        if (var->getType().isConstQualified()) {
            constVariables.insert(varName);
        }
        
        // Check if it's a global variable (file scope)
        if (var->hasGlobalStorage() || var->isFileVarDecl()) {
            globalVariables.insert(varName);
        }
        
        return true;
    }

    bool VisitFunctionDecl(FunctionDecl* func) {
        // Analyze function bodies to see if they modify global variables
        if (func->hasBody()) {
            GlobalModificationChecker checker(globalVariables);
            checker.TraverseStmt(func->getBody());
            if (checker.modifiesGlobal()) {
                functionsModifyingGlobals.insert(func->getNameAsString());
            }
        }
        return true;
    }

    bool VisitBinaryOperator(BinaryOperator* binOp) {
        if (binOp->isAssignmentOp()) {
            if (auto* declRef = dyn_cast<DeclRefExpr>(binOp->getLHS())) {
                if (auto* varDecl = dyn_cast<VarDecl>(declRef->getDecl())) {
                    std::string varName = varDecl->getNameAsString();
                    variableModifications[varName] = true;
                }
            }
        }
        return true;
    }

    bool VisitUnaryOperator(UnaryOperator* unaryOp) {
        if (unaryOp->isIncrementDecrementOp()) {
            if (auto* declRef = dyn_cast<DeclRefExpr>(unaryOp->getSubExpr())) {
                if (auto* varDecl = dyn_cast<VarDecl>(declRef->getDecl())) {
                    std::string varName = varDecl->getNameAsString();
                    variableModifications[varName] = true;
                }
            }
        }
        return true;
    }

    bool isVariableModified(const std::string& varName) const {
        auto it = variableModifications.find(varName);
        return it != variableModifications.end() && it->second;
    }

    bool isConstVariable(const std::string& varName) const {
        return constVariables.find(varName) != constVariables.end();
    }

    bool isGlobalVariable(const std::string& varName) const {
        return globalVariables.find(varName) != globalVariables.end();
    }

    bool functionModifiesGlobals(const std::string& funcName) const {
        return functionsModifyingGlobals.find(funcName) != functionsModifyingGlobals.end();
    }

private:
    class GlobalModificationChecker : public RecursiveASTVisitor<GlobalModificationChecker> {
    private:
        const std::set<std::string>& globals;
        bool modifiesGlobalVar;

    public:
        GlobalModificationChecker(const std::set<std::string>& globalVars) 
            : globals(globalVars), modifiesGlobalVar(false) {}

        bool VisitBinaryOperator(BinaryOperator* binOp) {
            if (binOp->isAssignmentOp()) {
                if (auto* declRef = dyn_cast<DeclRefExpr>(binOp->getLHS())) {
                    if (auto* varDecl = dyn_cast<VarDecl>(declRef->getDecl())) {
                        std::string varName = varDecl->getNameAsString();
                        if (globals.find(varName) != globals.end()) {
                            modifiesGlobalVar = true;
                        }
                    }
                }
            }
            return true;
        }

        bool VisitUnaryOperator(UnaryOperator* unaryOp) {
            if (unaryOp->isIncrementDecrementOp()) {
                if (auto* declRef = dyn_cast<DeclRefExpr>(unaryOp->getSubExpr())) {
                    if (auto* varDecl = dyn_cast<VarDecl>(declRef->getDecl())) {
                        std::string varName = varDecl->getNameAsString();
                        if (globals.find(varName) != globals.end()) {
                            modifiesGlobalVar = true;
                        }
                    }
                }
            }
            return true;
        }

        bool modifiesGlobal() const { return modifiesGlobalVar; }
    };
};

class ParameterExtractor : public RecursiveASTVisitor<ParameterExtractor> {
private:
    std::vector<ParameterInfo>& parameters;
    VariableAnalyzer& analyzer;
    ASTContext* context;
    SourceManager& sourceManager;

public:
    ParameterExtractor(std::vector<ParameterInfo>& params, VariableAnalyzer& va, ASTContext* ctx) 
        : parameters(params), analyzer(va), context(ctx), sourceManager(ctx->getSourceManager()) {}

    bool VisitExpr(Expr* expr) {
        // Handle different types of expressions
        if (auto* intLit = dyn_cast<IntegerLiteral>(expr)) {
            ParameterInfo info;
            info.name = "literal_int";
            info.type = ParameterType::LITERAL;
            info.sourceText = std::to_string(intLit->getValue().getLimitedValue());
            info.isModified = false;
            parameters.push_back(info);
            return false; // Don't traverse children
        }
        
        if (auto* floatLit = dyn_cast<FloatingLiteral>(expr)) {
            ParameterInfo info;
            info.name = "literal_float";
            info.type = ParameterType::LITERAL;
            info.sourceText = std::to_string(floatLit->getValueAsApproximateDouble());
            info.isModified = false;
            parameters.push_back(info);
            return false; // Don't traverse children
        }
        
        if (auto* stringLit = dyn_cast<clang::StringLiteral>(expr)) {
            ParameterInfo info;
            info.name = "literal_string";
            info.type = ParameterType::LITERAL;
            info.sourceText = "\"" + stringLit->getString().str() + "\"";
            info.isModified = false;
            parameters.push_back(info);
            return false; // Don't traverse children
        }
        
        if (auto* declRef = dyn_cast<DeclRefExpr>(expr)) {
            if (auto* varDecl = dyn_cast<VarDecl>(declRef->getDecl())) {
                std::string varName = varDecl->getNameAsString();
                
                ParameterInfo info;
                info.name = varName;
                info.sourceText = varName;
                info.isModified = analyzer.isVariableModified(varName);
                
                // Determine parameter type with better logic
                if (analyzer.isConstVariable(varName)) {
                    info.type = ParameterType::CONSTANT;
                } else if (analyzer.isGlobalVariable(varName)) {
                    info.type = info.isModified ? ParameterType::SHARED_MUTABLE : ParameterType::SHARED_READONLY;
                } else {
                    // Local variable - check if it's used elsewhere
                    info.type = ParameterType::INDEPENDENT;
                }
                
                parameters.push_back(info);
                return false; // Don't traverse children
            }
        }
        
        return true; // Continue traversing for other expression types
    }

    // Keep these for backward compatibility, but they won't be called due to VisitExpr
    bool VisitDeclRefExpr(DeclRefExpr* expr) {
        return true; // Handled in VisitExpr
    }

    bool VisitIntegerLiteral(IntegerLiteral* literal) {
        return true; // Handled in VisitExpr
    }

    bool VisitFloatingLiteral(FloatingLiteral* literal) {
        return true; // Handled in VisitExpr
    }

    bool VisitStringLiteral(clang::StringLiteral* literal) {
        return true; // Handled in VisitExpr
    }
};

class AutoOutputMPIVisitor : public RecursiveASTVisitor<AutoOutputMPIVisitor> {
private:
    ASTContext* context;
    Rewriter& rewriter;
    std::vector<AutoCallInfo> calls;
    FunctionDecl* mainFunc;
    bool inMainFunction;
    int stmtCounter;
    VariableAnalyzer analyzer;
    std::map<std::string, int> variableDeclarationOrder; // Variable name -> statement index when declared

public:
    AutoOutputMPIVisitor(ASTContext* ctx, Rewriter& r) 
        : context(ctx), rewriter(r), mainFunc(nullptr), inMainFunction(false), stmtCounter(0), analyzer(ctx) {}

    bool VisitFunctionDecl(FunctionDecl* func) {
        if (func->getNameInfo().getName().getAsString() == "main") {
            mainFunc = func;
        }
        return true;
    }

    bool TraverseFunctionDecl(FunctionDecl* func) {
        bool wasInMain = inMainFunction;
        int oldCounter = stmtCounter;
        
        if (func && func->getNameInfo().getName().getAsString() == "main") {
            inMainFunction = true;
            stmtCounter = 0;
        }
        
        bool result = RecursiveASTVisitor<AutoOutputMPIVisitor>::TraverseFunctionDecl(func);
        
        inMainFunction = wasInMain;
        stmtCounter = oldCounter;
        return result;
    }

    bool VisitDeclStmt(DeclStmt* declStmt) {
        if (!inMainFunction) {
            return true;
        }

        // Track variable declarations and their order
        for (auto it = declStmt->decl_begin(); it != declStmt->decl_end(); ++it) {
            if (auto* varDecl = dyn_cast<VarDecl>(*it)) {
                std::string varName = varDecl->getNameAsString();
                variableDeclarationOrder[varName] = stmtCounter;
                
                if (VerboseMode) {
                    std::cerr << "  Variable declared: " << varName << " at statement " << stmtCounter << std::endl;
                }
            }
        }
        stmtCounter++;
        return true;
    }

    bool VisitCallExpr(CallExpr* call) {
        if (!inMainFunction) {
            return true;
        }

        if (auto* directCallee = call->getDirectCallee()) {
            std::string funcName = directCallee->getNameAsString();
            
            // Skip system functions and MPI functions
            if (funcName.find("operator") != std::string::npos ||
                funcName == "printf" || funcName == "cout" || funcName == "endl" ||
                funcName.find("MPI_") != std::string::npos ||
                funcName.find("std::") != std::string::npos) {
                stmtCounter++;
                return true;
            }

            AutoCallInfo info;
            info.expr = call;
            info.functionName = funcName;
            info.location = call->getBeginLoc();
            info.hasReturnType = !directCallee->getReturnType()->isVoidType();
            info.hasParameters = call->getNumArgs() > 0;
            info.statementIndex = stmtCounter++;
            info.isAssignedToVariable = false;
            info.canBeParallelized = false;
            
            getCallSourceText(call, info.sourceText);
            analyzeParameters(call, info);
            checkAssignmentContext(call, info);
            analyzeVariableDependencies(info);
            
            calls.push_back(info);
            
            if (VerboseMode) {
                std::cerr << "Found call: " << funcName 
                          << " (returns: " << (info.hasReturnType ? "yes" : "no") 
                          << ", params: " << (info.hasParameters ? "yes" : "no");
                
                if (!info.parameters.empty()) {
                    std::cerr << ", param types: ";
                    for (size_t i = 0; i < info.parameters.size(); ++i) {
                        std::cerr << getParameterTypeString(info.parameters[i].type);
                        if (i < info.parameters.size() - 1) std::cerr << ", ";
                    }
                }
                
                if (!info.requiredVariables.empty()) {
                    std::cerr << ", requires vars: ";
                    for (auto it = info.requiredVariables.begin(); it != info.requiredVariables.end(); ++it) {
                        std::cerr << *it;
                        if (std::next(it) != info.requiredVariables.end()) std::cerr << ", ";
                    }
                }
                
                std::cerr << ")" << std::endl;
            }
        } else {
            stmtCounter++;
        }
        return true;
    }

    bool VisitStmt(Stmt* stmt) {
        if (!inMainFunction) {
            return true;
        }

        // Handle statements that aren't function calls or declarations
        if (!isa<CallExpr>(stmt) && !isa<DeclStmt>(stmt) && !isa<CompoundStmt>(stmt)) {
            stmtCounter++;
        }
        
        return true;
    }

    void performTransformation() {
        if (!mainFunc || calls.empty()) {
            if (!SilentMode) {
                std::cerr << "Found " << calls.size() << " function calls in main()" << std::endl;
            }
            return;
        }

        // First pass: analyze all variables and functions in the entire translation unit
        analyzer.TraverseDecl(context->getTranslationUnitDecl());

        // Second pass: re-analyze calls with updated variable information
        for (auto& call : calls) {
            determineParallelizability(call);
            
            if (VerboseMode) {
                std::cerr << "  " << call.functionName << ": " 
                          << (call.canBeParallelized ? "PARALLELIZABLE" : "NOT_PARALLELIZABLE")
                          << " (" << call.parallelizationReason << ")" << std::endl;
            }
        }

        if (!SilentMode) {
            std::cerr << "Analyzing " << calls.size() << " function calls for parallelization (with intelligent parameter analysis)..." << std::endl;
        }

        auto groups = createGroups();
        
        if (!SilentMode) {
            std::cerr << "Created " << groups.size() << " parallel groups" << std::endl;
            
            for (size_t i = 0; i < groups.size(); ++i) {
                std::cerr << "Group " << i << " (" << groups[i].size() << " functions): ";
                for (size_t j = 0; j < groups[i].size(); ++j) {
                    std::cerr << groups[i][j].functionName;
                    if (j < groups[i].size() - 1) std::cerr << ", ";
                }
                std::cerr << std::endl;
            }
        }

        if (groups.empty()) {
            if (!SilentMode) {
                std::cerr << "No parallelizable groups found" << std::endl;
                if (VerboseMode) {
                    printAnalysisDetails();
                }
            }
            return;
        }

        addMPIHeaders();
        transformMainWithGroups(groups);
    }

private:
    std::string getParameterTypeString(ParameterType type) {
        switch (type) {
            case ParameterType::LITERAL: return "LITERAL";
            case ParameterType::CONSTANT: return "CONSTANT";
            case ParameterType::INDEPENDENT: return "INDEPENDENT";
            case ParameterType::SHARED_READONLY: return "SHARED_RO";
            case ParameterType::SHARED_MUTABLE: return "SHARED_MUT";
            default: return "UNKNOWN";
        }
    }

    void getCallSourceText(CallExpr* call, std::string& text) {
        SourceManager& sm = context->getSourceManager();
        SourceLocation start = call->getBeginLoc();
        SourceLocation end = call->getEndLoc();
        
        end = Lexer::getLocForEndOfToken(end, 0, sm, context->getLangOpts());
        
        if (start.isValid() && end.isValid()) {
            bool invalid = false;
            const char* startPtr = sm.getCharacterData(start, &invalid);
            if (!invalid) {
                const char* endPtr = sm.getCharacterData(end, &invalid);
                if (!invalid && endPtr >= startPtr) {
                    text = std::string(startPtr, endPtr - startPtr);
                }
            }
        }
        
        if (text.empty()) {
            text = call->getDirectCallee()->getNameAsString() + "()";
        }
    }

    void analyzeParameters(CallExpr* call, AutoCallInfo& info) {
        // Clear previous analysis
        info.parameters.clear();
        info.parameterVars.clear();
        
        if (VerboseMode) {
            std::cerr << "  Analyzing parameters for " << info.functionName << " with " << call->getNumArgs() << " args" << std::endl;
        }
        
        for (unsigned i = 0; i < call->getNumArgs(); ++i) {
            Expr* arg = call->getArg(i);
            
            // Extract parameter information
            std::vector<ParameterInfo> argParams;
            ParameterExtractor extractor(argParams, analyzer, context);
            extractor.TraverseStmt(arg);
            
            if (VerboseMode) {
                std::cerr << "    Arg " << i << " produced " << argParams.size() << " parameters" << std::endl;
            }
            
            // Merge into main parameter list
            for (const auto& param : argParams) {
                info.parameters.push_back(param);
                if (param.type != ParameterType::LITERAL) {
                    info.parameterVars.push_back(param.name);
                }
                
                if (VerboseMode) {
                    std::cerr << "      " << param.name << " (" << getParameterTypeString(param.type) << "): " << param.sourceText << std::endl;
                }
            }
        }
        
        if (VerboseMode) {
            std::cerr << "  Total parameters for " << info.functionName << ": " << info.parameters.size() << std::endl;
        }
    }

    void checkAssignmentContext(CallExpr* call, AutoCallInfo& info) {
        SourceManager& sm = context->getSourceManager();
        SourceLocation callStart = call->getBeginLoc();
        
        unsigned lineNum = sm.getExpansionLineNumber(callStart);
        SourceLocation lineStart = sm.translateLineCol(sm.getMainFileID(), lineNum, 1);
        SourceLocation lineEnd = sm.translateLineCol(sm.getMainFileID(), lineNum + 1, 1);
        
        if (lineStart.isValid() && lineEnd.isValid()) {
            bool invalid = false;
            const char* lineStartPtr = sm.getCharacterData(lineStart, &invalid);
            const char* lineEndPtr = sm.getCharacterData(lineEnd, &invalid);
            
            if (!invalid && lineEndPtr > lineStartPtr) {
                std::string line(lineStartPtr, lineEndPtr - lineStartPtr);
                
                size_t equalPos = line.find('=');
                if (equalPos != std::string::npos && 
                    line.find(info.functionName) > equalPos) {
                    info.isAssignedToVariable = true;
                }
            }
        }
    }

    void analyzeVariableDependencies(AutoCallInfo& info) {
        // Track which variables this function call depends on
        for (const auto& param : info.parameters) {
            if (param.type == ParameterType::INDEPENDENT) {
                info.requiredVariables.insert(param.name);
            }
        }
    }

    void determineParallelizability(AutoCallInfo& info) {
        // Check basic conditions
        if (info.hasReturnType) {
            info.canBeParallelized = false;
            info.parallelizationReason = "has return type";
            return;
        }

        if (info.isAssignedToVariable) {
            info.canBeParallelized = false;
            info.parallelizationReason = "assigned to variable";
            return;
        }

        // Check if function modifies global state
        if (analyzer.functionModifiesGlobals(info.functionName)) {
            info.canBeParallelized = false;
            info.parallelizationReason = "function modifies global variables";
            return;
        }

        // Check variable dependencies - ensure all required variables are declared before this call
        for (const auto& varName : info.requiredVariables) {
            auto it = variableDeclarationOrder.find(varName);
            if (it == variableDeclarationOrder.end()) {
                info.canBeParallelized = false;
                info.parallelizationReason = "uses undeclared variable: " + varName;
                return;
            }
            if (it->second >= info.statementIndex) {
                info.canBeParallelized = false;
                info.parallelizationReason = "uses variable declared after call: " + varName;
                return;
            }
        }

        // Analyze parameter safety (always enabled now)
        if (info.hasParameters) {
            for (const auto& param : info.parameters) {
                if (param.type == ParameterType::SHARED_MUTABLE) {
                    info.canBeParallelized = false;
                    info.parallelizationReason = "uses shared mutable variable: " + param.name;
                    return;
                }
            }
        }

        // If we get here, the function can be parallelized
        info.canBeParallelized = true;
        if (info.hasParameters) {
            info.parallelizationReason = "safe for parallelization (independent parameters)";
        } else {
            info.parallelizationReason = "safe for parallelization (no parameters)";
        }
    }

    std::vector<std::vector<AutoCallInfo>> createGroups() {
        std::vector<std::vector<AutoCallInfo>> groups;
        std::vector<bool> processed(calls.size(), false);
        
        for (size_t i = 0; i < calls.size(); ++i) {
            if (processed[i]) continue;

            if (!calls[i].canBeParallelized) {
                processed[i] = true;
                continue;
            }

            std::vector<AutoCallInfo> currentGroup;
            currentGroup.push_back(calls[i]);
            processed[i] = true;

            for (size_t j = i + 1; j < calls.size(); ++j) {
                if (processed[j]) continue;
                
                if (!calls[j].canBeParallelized) {
                    break;
                }

                bool independent = true;
                for (const auto& groupedCall : currentGroup) {
                    if (hasConflict(groupedCall, calls[j])) {
                        independent = false;
                        break;
                    }
                }

                if (independent) {
                    currentGroup.push_back(calls[j]);
                    processed[j] = true;
                } else {
                    break;
                }
            }

            if (currentGroup.size() > 1) {
                groups.push_back(currentGroup);
            }
        }

        return groups;
    }

    bool hasConflict(const AutoCallInfo& call1, const AutoCallInfo& call2) {
        // Check for shared mutable variables
        for (const auto& param1 : call1.parameters) {
            if (param1.type == ParameterType::SHARED_MUTABLE) {
                for (const auto& param2 : call2.parameters) {
                    if (param2.name == param1.name) {
                        return true;
                    }
                }
            }
        }

        // Check for same independent variables being used (potential conflict)
        std::set<std::string> vars1, vars2;
        for (const auto& param : call1.parameters) {
            if (param.type == ParameterType::INDEPENDENT) {
                vars1.insert(param.name);
            }
        }
        for (const auto& param : call2.parameters) {
            if (param.type == ParameterType::INDEPENDENT) {
                vars2.insert(param.name);
            }
        }
        
        // Check for intersection
        for (const auto& var : vars1) {
            if (vars2.find(var) != vars2.end()) {
                return true; // Same variable used in both calls
            }
        }

        // Check variable declaration dependencies
        // If call2 depends on variables that are declared between call1 and call2,
        // they cannot be in the same parallel group
        for (const auto& varName : call2.requiredVariables) {
            auto it = variableDeclarationOrder.find(varName);
            if (it != variableDeclarationOrder.end()) {
                int declOrder = it->second;
                // If variable is declared after call1 but before call2, there's a dependency
                if (declOrder > call1.statementIndex && declOrder < call2.statementIndex) {
                    return true;
                }
            }
        }

        return false;
    }

    void printAnalysisDetails() {
        std::cerr << "\nDetailed Analysis:" << std::endl;
        for (const auto& call : calls) {
            std::cerr << "  " << call.functionName << ": ";
            
            if (call.canBeParallelized) {
                std::cerr << "PARALLELIZABLE (" << call.parallelizationReason << ")";
            } else {
                std::cerr << "NOT_PARALLELIZABLE (" << call.parallelizationReason << ")";
            }
            
            if (!call.parameters.empty()) {
                std::cerr << " [params: ";
                for (size_t i = 0; i < call.parameters.size(); ++i) {
                    std::cerr << call.parameters[i].name << ":" << getParameterTypeString(call.parameters[i].type);
                    if (i < call.parameters.size() - 1) std::cerr << ", ";
                }
                std::cerr << "]";
            }
            
            if (!call.requiredVariables.empty()) {
                std::cerr << " [requires: ";
                for (auto it = call.requiredVariables.begin(); it != call.requiredVariables.end(); ++it) {
                    auto declIt = variableDeclarationOrder.find(*it);
                    std::cerr << *it << "@" << (declIt != variableDeclarationOrder.end() ? std::to_string(declIt->second) : "?");
                    if (std::next(it) != call.requiredVariables.end()) std::cerr << ", ";
                }
                std::cerr << "] [call@" << call.statementIndex << "]";
            }
            
            std::cerr << std::endl;
        }
    }

    void addMPIHeaders() {
        SourceManager& sm = context->getSourceManager();
        SourceLocation startLoc = sm.getLocForStartOfFile(sm.getMainFileID());
        rewriter.InsertText(startLoc, "#include <mpi.h>\n");
    }

    void transformMainWithGroups(const std::vector<std::vector<AutoCallInfo>>& groups) {
        if (!mainFunc->hasBody()) return;

        CompoundStmt* body = dyn_cast<CompoundStmt>(mainFunc->getBody());
        if (!body) return;

        SourceLocation bodyStart = body->getBeginLoc().getLocWithOffset(1);
        
        std::string mpiInit = 
            "\n    // MPI Initialization\n"
            "    int mpi_rank, mpi_size;\n"
            "    MPI_Init(NULL, NULL);\n"
            "    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);\n"
            "    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);\n\n";
        
        rewriter.InsertText(bodyStart, mpiInit);

        for (size_t groupIdx = 0; groupIdx < groups.size(); ++groupIdx) {
            transformSingleGroup(groups[groupIdx], groupIdx);
        }

        // Find the return statement and insert MPI_Finalize() before it
        SourceManager& sm = context->getSourceManager();
        SourceLocation bodyEnd = body->getEndLoc();
        
        // Look for return statements in the main function
        bool foundReturn = false;
        for (auto stmt : body->body()) {
            if (isa<ReturnStmt>(stmt)) {
                SourceLocation returnLoc = stmt->getBeginLoc();
                std::string mpiFinalize = "    MPI_Finalize();\n    ";
                rewriter.InsertText(returnLoc, mpiFinalize);
                foundReturn = true;
                break;
            }
        }
        
        // If no return statement found, add MPI_Finalize() before the closing brace
        if (!foundReturn) {
            std::string mpiFinalize = "\n    MPI_Finalize();\n";
            rewriter.InsertText(bodyEnd, mpiFinalize);
        }
    }
    
    void transformSingleGroup(const std::vector<AutoCallInfo>& group, size_t groupIdx) {
        if (group.empty()) return;

        if (!SilentMode) {
            std::cerr << "Transforming group " << groupIdx << " with " << group.size() << " calls" << std::endl;
        }

        std::stringstream parallelCode;
        parallelCode << "{\n";
        parallelCode << "        // Parallel execution block " << groupIdx << "\n";
        
        for (size_t i = 0; i < group.size(); ++i) {
            parallelCode << "        if (mpi_rank == " << i << " && " << i << " < mpi_size) {\n";
            parallelCode << "            " << group[i].sourceText << ";\n";
            parallelCode << "        }\n";
        }
        
        parallelCode << "        MPI_Barrier(MPI_COMM_WORLD);\n";
        parallelCode << "    }";

        SourceManager& sm = context->getSourceManager();
        SourceLocation start = group[0].location;
        SourceLocation end = Lexer::getLocForEndOfToken(
            group[0].expr->getEndLoc(), 0, sm, context->getLangOpts());
        end = end.getLocWithOffset(1);
        
        rewriter.ReplaceText(SourceRange(start, end), parallelCode.str());

        for (size_t i = 1; i < group.size(); ++i) {
            SourceLocation callStart = group[i].location;
            SourceLocation callEnd = Lexer::getLocForEndOfToken(
                group[i].expr->getEndLoc(), 0, sm, context->getLangOpts());
            callEnd = callEnd.getLocWithOffset(1);
            
            std::string comment = "// PARALLELIZED: " + group[i].sourceText;
            rewriter.ReplaceText(SourceRange(callStart, callEnd), comment);
        }
    }
};

class AutoOutputMPIConsumer : public ASTConsumer {
private:
    AutoOutputMPIVisitor visitor;

public:
    AutoOutputMPIConsumer(ASTContext* context, Rewriter& r) : visitor(context, r) {}

    void HandleTranslationUnit(ASTContext& context) override {
        visitor.TraverseDecl(context.getTranslationUnitDecl());
        visitor.performTransformation();
    }
};

class AutoOutputMPIAction : public ASTFrontendAction {
private:
    Rewriter rewriter;

public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& compiler,
                                                   StringRef file) override {
        rewriter.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
        return std::unique_ptr<ASTConsumer>(
            new AutoOutputMPIConsumer(&compiler.getASTContext(), rewriter));
    }

    void EndSourceFileAction() override {
        SourceManager& sm = rewriter.getSourceMgr();
        FileID mainFileID = sm.getMainFileID();
        
        // Generate output filename
        std::string outputFileName;
        if (!OutputFile.empty()) {
            outputFileName = OutputFile;
        } else {
            // Auto-generate output filename
            size_t lastSlash = InputFileName.find_last_of("/\\");
            std::string baseName = (lastSlash != std::string::npos) ? 
                InputFileName.substr(lastSlash + 1) : InputFileName;
            
            size_t lastDot = baseName.find_last_of('.');
            if (lastDot != std::string::npos) {
                baseName = baseName.substr(0, lastDot);
            }
            
            outputFileName = "parallel_" + baseName + ".cpp";
        }
        
        // Write to file
        std::error_code EC;
        raw_fd_ostream OutFile(outputFileName, EC);
        if (!EC) {
            rewriter.getEditBuffer(mainFileID).write(OutFile);
            OutFile.close();
            
            if (!SilentMode) {
                std::cerr << "✅ Transformed code written to: " << outputFileName << std::endl;
            }
            
            // Optionally compile and run
            if (CompileAndRun) {
                compileAndRun(outputFileName);
            }
        } else {
            std::cerr << "❌ Error writing to file " << outputFileName << ": " << EC.message() << std::endl;
            // Fallback to stdout
            rewriter.getEditBuffer(mainFileID).write(llvm::outs());
        }
    }

private:
    void compileAndRun(const std::string& filename) {
        std::string baseName = filename.substr(0, filename.find_last_of('.'));
        std::string compileCmd = "mpicxx -o " + baseName + " " + filename + " 2>/dev/null";
        std::string runCmd = "mpirun -np 4 ./" + baseName + " 2>/dev/null";
        
        if (!SilentMode) {
            std::cerr << "🔨 Compiling: " << compileCmd << std::endl;
        }
        
        if (system(compileCmd.c_str()) == 0) {
            if (!SilentMode) {
                std::cerr << "✅ Compilation successful" << std::endl;
                std::cerr << "🚀 Running: " << runCmd << std::endl;
                std::cerr << "--- Output ---" << std::endl;
            }
            system(runCmd.c_str());
            if (!SilentMode) {
                std::cerr << "--- End ---" << std::endl;
            }
        } else {
            std::cerr << "❌ Compilation failed" << std::endl;
        }
    }
};

int main(int argc, const char** argv) {
    #if LLVM_VERSION_MAJOR >= 10
    auto expectedParser = CommonOptionsParser::create(argc, argv, MPIToolCategory);
    if (!expectedParser) {
        llvm::errs() << expectedParser.takeError();
        return 1;
    }
    CommonOptionsParser& optionsParser = expectedParser.get();
    #else
    CommonOptionsParser optionsParser(argc, argv, MPIToolCategory);
    #endif
    
    // Store input filename for output generation
    auto sourcePathList = optionsParser.getSourcePathList();
    if (!sourcePathList.empty()) {
        InputFileName = sourcePathList[0];
    }
    
    ClangTool tool(optionsParser.getCompilations(), sourcePathList);
    return tool.run(newFrontendActionFactory<AutoOutputMPIAction>().get());
}