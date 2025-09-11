#include "function_analyzer.h"
#include "clang/Lex/Lexer.h"
#include <set>
#include <algorithm>

using namespace clang;

bool GlobalVariableCollector::VisitVarDecl(VarDecl *VD) {
    if (VD->hasGlobalStorage() && !VD->isStaticLocal()) {
        SourceManager &SM = VD->getASTContext().getSourceManager();
        if (SM.isInMainFile(VD->getLocation())) {
            globalVariables.insert(VD->getNameAsString());
        }
    }
    return true;
}

ComprehensiveFunctionAnalyzer::ComprehensiveFunctionAnalyzer() : SM(nullptr) {}

ComprehensiveFunctionAnalyzer::ComprehensiveFunctionAnalyzer(const std::set<std::string>& globals) 
    : globalVars(globals), SM(nullptr) {}

void ComprehensiveFunctionAnalyzer::setSourceManager(SourceManager *sourceManager) {
    SM = sourceManager;
}

void ComprehensiveFunctionAnalyzer::setFunctionLoops(const std::map<std::string, std::vector<LoopInfo>>& functionLoops) {
    // Add loop information to function info, removing duplicates
    for (const auto& pair : functionLoops) {
        const std::string& funcName = pair.first;
        const std::vector<LoopInfo>& loops = pair.second;
        
        if (functionInfo.count(funcName)) {
            // Remove duplicates based on start line and column
            std::vector<LoopInfo> uniqueLoops;
            std::set<std::string> processedLoops;
            
            for (const auto& loop : loops) {
                std::string loopKey = std::to_string(loop.start_line) + "_" + std::to_string(loop.start_col) + "_" + loop.type;
                if (processedLoops.find(loopKey) == processedLoops.end()) {
                    uniqueLoops.push_back(loop);
                    processedLoops.insert(loopKey);
                }
            }
            
            functionInfo[funcName].loops = uniqueLoops;
            functionInfo[funcName].has_parallelizable_loops = false;
            
            for (const auto& loop : uniqueLoops) {
                if (loop.parallelizable) {
                    functionInfo[funcName].has_parallelizable_loops = true;
                    break;
                }
            }
        }
    }
}

bool ComprehensiveFunctionAnalyzer::VisitFunctionDecl(FunctionDecl *FD) {
    if (FD->hasBody()) {
        std::string funcName = FD->getNameAsString();
        
        // Skip functions from system headers - only analyze user code
        SourceLocation loc = FD->getBeginLoc();
        if (SM->isInSystemHeader(loc)) {
            return true;
        }
        
        // Skip if we've already processed this function
        if (functionInfo.count(funcName)) {
            return true;
        }
        
        currentFunction = funcName;
        functionAnalysis[currentFunction] = FunctionAnalysis();
        currentFunctionParams.clear();
        
        // Create function info
        FunctionInfo info;
        info.name = currentFunction;
        
        QualType returnQType = FD->getReturnType();
        std::string returnTypeStr = returnQType.getAsString();
        info.return_type = returnTypeStr;
        functionAnalysis[currentFunction].returnType = returnTypeStr;
        
        // Get parameters
        for (unsigned i = 0; i < FD->getNumParams(); ++i) {
            ParmVarDecl *param = FD->getParamDecl(i);
            std::string paramName = param->getNameAsString();
            std::string paramType = param->getType().getAsString();
            currentFunctionParams.insert(paramName);
            info.parameter_names.push_back(paramName);
            info.parameter_types.push_back(paramType);
            functionAnalysis[currentFunction].parameterTypes.push_back(paramType);
        }
        
        // Get source location
        if (SM) {
            SourceLocation startLoc = FD->getBeginLoc();
            SourceLocation endLoc = FD->getEndLoc();
            info.start_line = SM->getSpellingLineNumber(startLoc);
            info.end_line = SM->getSpellingLineNumber(endLoc);
            
            // Get function body source
            if (CompoundStmt *body = dyn_cast<CompoundStmt>(FD->getBody())) {
                SourceRange bodyRange = body->getSourceRange();
                info.original_body = getSourceText(bodyRange);
            }
        }
        
        functionInfo[currentFunction] = info;
        
        TraverseStmt(FD->getBody());
    }
    return true;
}

bool ComprehensiveFunctionAnalyzer::VisitDeclRefExpr(DeclRefExpr *DRE) {
    if (VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        std::string varName = VD->getNameAsString();
        if (globalVars.count(varName)) {
            functionAnalysis[currentFunction].readSet.insert(varName);
            functionInfo[currentFunction].global_reads.insert(varName);
        } else if (VD->hasLocalStorage() && !currentFunctionParams.count(varName)) {
            functionAnalysis[currentFunction].localReads.insert(varName);
            functionInfo[currentFunction].local_vars.insert(varName);
        }
    }
    return true;
}

bool ComprehensiveFunctionAnalyzer::VisitBinaryOperator(BinaryOperator *BO) {
    if (BO->isAssignmentOp()) {
        if (DeclRefExpr *LHS = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreImpCasts())) {
            if (VarDecl *VD = dyn_cast<VarDecl>(LHS->getDecl())) {
                std::string varName = VD->getNameAsString();
                if (globalVars.count(varName)) {
                    functionAnalysis[currentFunction].writeSet.insert(varName);
                    functionInfo[currentFunction].global_writes.insert(varName);
                } else if (VD->hasLocalStorage() && !currentFunctionParams.count(varName)) {
                    functionAnalysis[currentFunction].localWrites.insert(varName);
                    functionInfo[currentFunction].local_vars.insert(varName);
                }
            }
        }
    }
    return true;
}

bool ComprehensiveFunctionAnalyzer::VisitUnaryOperator(UnaryOperator *UO) {
    if (UO->isIncrementDecrementOp()) {
        if (DeclRefExpr *operand = dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreImpCasts())) {
            if (VarDecl *VD = dyn_cast<VarDecl>(operand->getDecl())) {
                std::string varName = VD->getNameAsString();
                if (globalVars.count(varName)) {
                    functionAnalysis[currentFunction].writeSet.insert(varName);
                    functionInfo[currentFunction].global_writes.insert(varName);
                } else if (VD->hasLocalStorage() && !currentFunctionParams.count(varName)) {
                    functionAnalysis[currentFunction].localWrites.insert(varName);
                    functionInfo[currentFunction].local_vars.insert(varName);
                }
            }
        }
    }
    return true;
}

std::string ComprehensiveFunctionAnalyzer::generateParallelizedFunction(const std::string& funcName) {
    if (!functionInfo.count(funcName)) {
        return "";
    }
    
    const FunctionInfo& info = functionInfo[funcName];
    if (!info.has_parallelizable_loops) {
        return ""; // No parallelizable loops, return original
    }
    
    std::string parallelizedBody = info.original_body;
    
    // First, replace thread-unsafe function calls with thread-safe alternatives
    for (const auto& loop : info.loops) {
        if (loop.has_thread_unsafe_calls) {
            for (const auto& unsafeFunc : loop.unsafe_functions) {
                if (unsafeFunc == "rand") {
                    // Replace rand() with rand_r(&__thread_seed)
                    size_t pos = 0;
                    while ((pos = parallelizedBody.find("rand()", pos)) != std::string::npos) {
                        parallelizedBody.replace(pos, 6, "rand_r(&__thread_seed)");
                        pos += 20; // Length of "rand_r(&__thread_seed)"
                    }
                }
            }
        }
    }
    
    // Add thread-local variable declarations at the beginning of function
    bool needsThreadSeed = false;
    for (const auto& loop : info.loops) {
        if (loop.thread_local_vars.count("__thread_seed")) {
            needsThreadSeed = true;
            break;
        }
    }
    
    if (needsThreadSeed) {
        // Insert thread-local seed declaration after opening brace
        size_t bracePos = parallelizedBody.find('{');
        if (bracePos != std::string::npos) {
            std::string seedDecl = "\n    unsigned int __thread_seed = (unsigned int)time(NULL) ^ omp_get_thread_num();";
            parallelizedBody.insert(bracePos + 1, seedDecl);
        }
    }
    
    // Insert OpenMP pragmas before parallelizable loops
    for (const auto& loop : info.loops) {
        if (loop.parallelizable && !loop.pragma_text.empty()) {
            // Find the loop in the body and insert pragma before it
            size_t loopPos = parallelizedBody.find(loop.source_code);
            if (loopPos != std::string::npos) {
                // Find the beginning of the line
                size_t lineStart = parallelizedBody.rfind('\n', loopPos);
                if (lineStart == std::string::npos) lineStart = 0;
                else lineStart++;
                
                // Get indentation
                std::string indentation = "";
                for (size_t i = lineStart; i < loopPos && parallelizedBody[i] == ' '; i++) {
                    indentation += " ";
                }
                
                // Insert pragma
                std::string pragmaLine = indentation + loop.pragma_text + "\n";
                parallelizedBody.insert(loopPos, pragmaLine);
            }
        }
    }
    
    return parallelizedBody;
}

std::string ComprehensiveFunctionAnalyzer::getSourceText(SourceRange range) {
    if (!SM || range.isInvalid()) return "";
    return std::string(Lexer::getSourceText(
        CharSourceRange::getTokenRange(range), *SM, LangOptions()));
}