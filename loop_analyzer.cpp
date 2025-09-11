#include "loop_analyzer.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <regex>
#include <sstream>
#include <cctype>

using namespace clang;

ComprehensiveLoopAnalyzer::ComprehensiveLoopAnalyzer(SourceManager *sourceManager, const std::set<std::string>& globals) 
    : SM(sourceManager), globalVariables(globals) {}

bool ComprehensiveLoopAnalyzer::VisitFunctionDecl(FunctionDecl *FD) {
    if (FD->hasBody()) {
        std::string funcName = FD->getNameAsString();
        
        // Skip functions from system headers - only analyze user code
        SourceLocation loc = FD->getBeginLoc();
        if (SM->isInSystemHeader(loc)) {
            return true;
        }
        
        // Skip if we've already processed this function
        if (functionLoops.count(funcName)) {
            return true;
        }
        
        currentFunction = funcName;
        functionLoops[currentFunction].clear();
        
        // Traverse the function body only once
        TraverseStmt(FD->getBody());
        currentFunction = "";
    }
    return true;
}

bool ComprehensiveLoopAnalyzer::VisitForStmt(ForStmt *FS) {
    if (!currentFunction.empty()) {
        bool wasInLoop = insideLoop;
        int previousDepth = loopDepth;
        
        // Track loop nesting
        insideLoop = true;
        loopDepth++;
        
        // Process this loop (will check depth for parallelization)
        processForLoop(FS);
        
        // Continue traversal to find nested loops, but don't call base class
        // The base class would bypass our depth tracking
        RecursiveASTVisitor::TraverseStmt(FS->getBody());
        
        // Restore previous state
        loopDepth = previousDepth;
        insideLoop = wasInLoop;
        
        return true;
    }
    return true;
}

bool ComprehensiveLoopAnalyzer::VisitWhileStmt(WhileStmt *WS) {
    if (!currentFunction.empty()) {
        processWhileLoop(WS);
    }
    return true;
}

bool ComprehensiveLoopAnalyzer::VisitDoStmt(DoStmt *DS) {
    if (!currentFunction.empty()) {
        processDoWhileLoop(DS);
    }
    return true;
}

const std::map<std::string, std::vector<LoopInfo>>& ComprehensiveLoopAnalyzer::getAllFunctionLoops() const { 
    return functionLoops; 
}

void ComprehensiveLoopAnalyzer::processForLoop(ForStmt *FS) {
    LoopInfo loop;
    loop.type = "for";
    loop.function_name = currentFunction;
    
    // Get source location
    SourceLocation startLoc = FS->getBeginLoc();
    SourceLocation endLoc = FS->getEndLoc();
    
    loop.start_line = SM->getSpellingLineNumber(startLoc);
    loop.end_line = SM->getSpellingLineNumber(endLoc);
    loop.start_col = SM->getSpellingColumnNumber(startLoc);
    loop.end_col = SM->getSpellingColumnNumber(endLoc);
    
    // Initialize thread-safety fields
    loop.has_thread_unsafe_calls = false;
    
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
    
    // Check for complex loop condition (&&, ||, etc.)
    if (Expr *Cond = FS->getCond()) {
        std::string conditionText = getSourceText(Cond->getSourceRange());
        // Remove whitespace for better matching
        std::string trimmedCondition = conditionText;
        trimmedCondition.erase(std::remove_if(trimmedCondition.begin(), trimmedCondition.end(), ::isspace), trimmedCondition.end());
        
        if (trimmedCondition.find("&&") != std::string::npos || 
            trimmedCondition.find("||") != std::string::npos) {
            loop.has_complex_condition = true;
        }
    }
    
    // Analyze loop body (includes dependency analysis)
    analyzeLoopBody(FS->getBody(), loop);
    
    // Only parallelize outermost loops (depth 1) in nested structures
    if (loopDepth > 1) {
        loop.parallelizable = false;
        loop.analysis_notes = "Inner loop in nested structure - not parallelized to avoid race conditions.";
    }
    
    // Generate OpenMP pragma
    if (loop.parallelizable) {
        loop.pragma_text = generateOpenMPPragma(loop);
    }
    
    functionLoops[currentFunction].push_back(loop);
}

void ComprehensiveLoopAnalyzer::processWhileLoop(WhileStmt *WS) {
    LoopInfo loop;
    loop.type = "while";
    loop.function_name = currentFunction;
    
    SourceLocation startLoc = WS->getBeginLoc();
    SourceLocation endLoc = WS->getEndLoc();
    
    loop.start_line = SM->getSpellingLineNumber(startLoc);
    loop.end_line = SM->getSpellingLineNumber(endLoc);
    
    loop.source_code = getSourceText(WS->getSourceRange());
    loop.parallelizable = false; // While loops are harder to parallelize
    loop.analysis_notes = "While loops require manual analysis for parallelization.";
    
    analyzeLoopBody(WS->getBody(), loop);
    
    functionLoops[currentFunction].push_back(loop);
}

void ComprehensiveLoopAnalyzer::processDoWhileLoop(DoStmt *DS) {
    LoopInfo loop;
    loop.type = "do-while";
    loop.function_name = currentFunction;
    
    SourceLocation startLoc = DS->getBeginLoc();
    SourceLocation endLoc = DS->getEndLoc();
    
    loop.start_line = SM->getSpellingLineNumber(startLoc);
    loop.end_line = SM->getSpellingLineNumber(endLoc);
    
    loop.source_code = getSourceText(DS->getSourceRange());
    loop.parallelizable = false;
    loop.analysis_notes = "Do-while loops require manual analysis for parallelization.";
    
    analyzeLoopBody(DS->getBody(), loop);
    
    functionLoops[currentFunction].push_back(loop);
}

void ComprehensiveLoopAnalyzer::analyzeLoopBody(Stmt *body, LoopInfo &loop) {
    if (!body) return;
    
    // Initialize flags to false (preserve has_complex_condition if already set)
    loop.has_function_calls = false;
    loop.has_io_operations = false;
    loop.has_break_continue = false;
    // Don't reset has_complex_condition - it may have been set during condition analysis
    loop.is_nested = false;
    
    class LoopBodyVisitor : public RecursiveASTVisitor<LoopBodyVisitor> {
    public:
        LoopInfo *loop;
        std::string loopVar;
        std::set<std::string> *globals;
        std::set<std::string> localVars;  // Track variables declared inside loop
        
        LoopBodyVisitor(LoopInfo *l, const std::string &var, std::set<std::string> *g) 
            : loop(l), loopVar(var), globals(g) {}
        
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
                    }
                }
            }
            return true;
        }
        
        bool VisitDeclStmt(DeclStmt *DS) {
            // Track local variable declarations within the loop
            for (auto *D : DS->decls()) {
                if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
                    localVars.insert(VD->getNameAsString());
                }
            }
            return true;
        }
        
        bool VisitCompoundAssignOperator(CompoundAssignOperator *CAO) {
            if (DeclRefExpr *LHS = dyn_cast<DeclRefExpr>(CAO->getLHS()->IgnoreImpCasts())) {
                if (VarDecl *VD = dyn_cast<VarDecl>(LHS->getDecl())) {
                    std::string varName = VD->getNameAsString();
                    // Only add to reduction if it's NOT a local variable declared inside the loop
                    if (localVars.find(varName) == localVars.end()) {
                        loop->reduction_vars.push_back(varName);
                    }
                    
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
                
                // Check for thread-unsafe functions
                if (funcName == "rand" || funcName == "srand" ||
                    funcName == "strtok" || funcName == "asctime" ||
                    funcName == "ctime" || funcName == "gmtime" ||
                    funcName == "localtime" || funcName == "strerror") {
                    loop->has_thread_unsafe_calls = true;
                    loop->unsafe_functions.push_back(funcName);
                    
                    // For rand(), we need a thread-local seed variable
                    if (funcName == "rand") {
                        loop->thread_local_vars.insert("__thread_seed");
                    }
                }
                // Check for I/O operations - be more specific
                else if (funcName == "printf" || funcName == "scanf" ||
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
        
        bool VisitBreakStmt(BreakStmt *BS) {
            loop->has_break_continue = true;
            return true;
        }
        
        bool VisitContinueStmt(ContinueStmt *CS) {
            loop->has_break_continue = true;
            return true;
        }
    };
    
    LoopBodyVisitor visitor(&loop, loop.loop_variable, &globalVariables);
    visitor.TraverseStmt(body);
    
    // Pass local variables to dependency analysis
    performDependencyAnalysis(loop, visitor.localVars);
}

void ComprehensiveLoopAnalyzer::performDependencyAnalysis(LoopInfo &loop, const std::set<std::string> &localVars) {
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
            // Make sure this variable is not an array element and not a local variable
            if (!std::regex_search(code, std::regex(var + R"(\s*\[)")) && 
                localVars.find(var) == localVars.end()) {
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
        
        if (loop.has_break_continue) {
            loop.parallelizable = false;
            loop.analysis_notes += "Contains break/continue statements - not parallelizable. ";
        }
        
        if (loop.has_complex_condition) {
            loop.parallelizable = false;
            loop.analysis_notes += "Complex loop condition - not parallelizable. ";
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
        
        if (loop.has_thread_unsafe_calls && loop.parallelizable) {
            loop.analysis_notes += "Thread-unsafe functions detected - replacing with thread-safe alternatives. ";
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

std::string ComprehensiveLoopAnalyzer::generateOpenMPPragma(const LoopInfo& loop) {
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
    
    // Add firstprivate clause for thread-local variables (like thread seeds)
    if (!loop.thread_local_vars.empty()) {
        pragma << " firstprivate(";
        bool first = true;
        for (const auto& var : loop.thread_local_vars) {
            if (!first) pragma << ",";
            pragma << var;
            first = false;
        }
        pragma << ")";
    }
    
    // Note: Loop variables declared in for-loop are automatically private
    // Only add private clause for variables declared outside the loop
    
    pragma << " schedule(" << loop.schedule_type;
    if (loop.schedule_type == "dynamic") {
        pragma << ",100";  // Smaller chunk size for better load balancing
    }
    pragma << ")";
    
    return pragma.str();
}

std::string ComprehensiveLoopAnalyzer::getSourceText(SourceRange range) {
    if (!SM || range.isInvalid()) return "";
    return std::string(Lexer::getSourceText(
        CharSourceRange::getTokenRange(range), *SM, LangOptions()));
}