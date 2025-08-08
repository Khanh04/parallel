#include "main_extractor.h"
#include "clang/Lex/Lexer.h"

using namespace clang;

MainFunctionExtractor::MainFunctionExtractor(SourceManager *sourceManager) 
    : SM(sourceManager), functionAnalysisPtr(nullptr) {}

void MainFunctionExtractor::setFunctionAnalysis(std::map<std::string, FunctionAnalysis>* analysis) {
    functionAnalysisPtr = analysis;
}

bool MainFunctionExtractor::VisitFunctionDecl(FunctionDecl *FD) {
    if (FD->getNameAsString() == "main" && FD->hasBody()) {
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

const std::vector<LoopInfo>& MainFunctionExtractor::getMainLoops() const { 
    return mainLoops; 
}

const std::map<std::string, LocalVariable>& MainFunctionExtractor::getLocalVariables() const {
    return localVariables;
}

void MainFunctionExtractor::collectLocalVariables(CompoundStmt *body) {
    for (auto *stmt : body->body()) {
        collectLocalVariablesInStmt(stmt);
    }
}

void MainFunctionExtractor::collectLocalVariablesInStmt(Stmt *stmt) {
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

void MainFunctionExtractor::processStatement(Stmt *stmt) {
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

void MainFunctionExtractor::analyzeLocalDependencies() {
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

bool MainFunctionExtractor::isUserFunction(const std::string& funcName) {
    return funcName != "printf" && funcName != "scanf" && 
           funcName != "malloc" && funcName != "free" &&
           funcName != "sleep" && funcName.find("std::") == std::string::npos &&
           funcName.find("cout") == std::string::npos &&
           funcName.find("endl") == std::string::npos &&
           funcName.find("__") != 0;
}

void MainFunctionExtractor::findUsedVariables(Expr *expr, std::set<std::string>& usedVars) {
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

std::string MainFunctionExtractor::getSourceText(SourceRange range) {
    if (range.isInvalid()) return "";
    return std::string(Lexer::getSourceText(
        CharSourceRange::getTokenRange(range), *SM, LangOptions()));
}