#include "main_extractor.h"
#include "clang/Lex/Lexer.h"
#include <set>

using namespace clang;

MainFunctionExtractor::MainFunctionExtractor(SourceManager *sourceManager) 
    : SM(sourceManager), functionAnalysisPtr(nullptr), variableDeclarationCounter(0) {}

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
                localVar.declarationOrder = variableDeclarationCounter++;
                localVar.definedAtCall = -1;
                localVar.isParameter = false;
                
                // Extract initialization value if present
                if (VD->hasInit()) {
                    // Check if this is an explicit initialization in the source
                    SourceRange varRange = VD->getSourceRange();
                    SourceRange initRange = VD->getInit()->getSourceRange();
                    
                    // Get the source text for the entire variable declaration
                    std::string varDeclText = getSourceText(varRange);
                    
                    // Check if the declaration contains '=' (assignment) or explicit constructor args
                    if (varDeclText.find('=') != std::string::npos) {
                        // This is assignment initialization: "Type var = value"
                        size_t eqPos = varDeclText.find('=');
                        std::string initPart = varDeclText.substr(eqPos + 1);
                        // Remove trailing semicolon and whitespace
                        while (!initPart.empty() && (initPart.back() == ';' || isspace(initPart.back()))) {
                            initPart.pop_back();
                        }
                        // Remove leading whitespace
                        size_t start = initPart.find_first_not_of(" \t");
                        if (start != std::string::npos) {
                            initPart = initPart.substr(start);
                        }
                        localVar.initializationValue = initPart;
                    } else if (varDeclText.find('(') != std::string::npos && varDeclText.find(')') != std::string::npos) {
                        // This is constructor initialization: "Type var(args)"
                        size_t parenStart = varDeclText.find('(');
                        size_t parenEnd = varDeclText.rfind(')');
                        if (parenStart != std::string::npos && parenEnd != std::string::npos && parenEnd > parenStart) {
                            std::string constructorArgs = varDeclText.substr(parenStart, parenEnd - parenStart + 1);
                            localVar.initializationValue = localVar.name + constructorArgs;
                        } else {
                            localVar.initializationValue = ""; // Default constructor
                        }
                    } else {
                        // Default constructor or implicit initialization
                        localVar.initializationValue = ""; // No explicit initialization
                    }
                } else {
                    localVar.initializationValue = ""; // No initialization
                }
                
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
    // Standard C library functions (stdio.h, stdlib.h, string.h, math.h, time.h)
    static const std::set<std::string> standardLibFunctions = {
        // stdio.h
        "printf", "scanf", "fprintf", "fscanf", "sprintf", "sscanf",
        "fopen", "fclose", "fread", "fwrite", "fgetc", "fputc", "fgets", "fputs",
        "getchar", "putchar", "gets", "puts", "perror", "fflush", "fseek", "ftell",
        
        // stdlib.h  
        "malloc", "calloc", "realloc", "free", "exit", "abort", "atexit",
        "system", "getenv", "setenv", "rand", "srand", "abs", "labs", "div", "ldiv",
        "atoi", "atol", "atof", "strtol", "strtod", "qsort", "bsearch",
        
        // string.h
        "strlen", "strcpy", "strncpy", "strcat", "strncat", "strcmp", "strncmp",
        "strchr", "strrchr", "strstr", "strspn", "strcspn", "strpbrk", "strtok",
        "memcpy", "memmove", "memcmp", "memchr", "memset",
        
        // math.h
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh",
        "exp", "log", "log10", "pow", "sqrt", "ceil", "floor", "fabs", "fmod",
        "frexp", "ldexp", "modf",
        
        // time.h
        "time", "clock", "difftime", "mktime", "strftime", "localtime", "gmtime",
        "asctime", "ctime", "sleep", "usleep",
        
        // unistd.h
        "read", "write", "close", "lseek", "access", "unlink", "getpid", "fork",
        "exec", "execl", "execv", "execve", "wait", "waitpid",
        
        // Common system functions
        "open", "creat", "dup", "dup2", "pipe", "chdir", "getcwd", "mkdir", "rmdir",
        
        // C++ standard library internal functions 
        "now", "count", "size", "begin", "end", "data", "empty", "clear",
        "push_back", "pop_back", "insert", "erase", "find", "reserve", "resize",
        "at", "front", "back", "emplace", "emplace_back", "shrink_to_fit",
        
        // Compiler intrinsics and operators
        "operator", "__builtin", "__sync", "__atomic"
    };
    
    // Check if it's a known standard library function
    if (standardLibFunctions.count(funcName)) {
        return false;
    }
    
    // Filter out std:: namespace functions
    if (funcName.find("std::") != std::string::npos) {
        return false;
    }
    
    // Filter out iostream operators and functions
    if (funcName.find("cout") != std::string::npos || 
        funcName.find("endl") != std::string::npos ||
        funcName.find("cin") != std::string::npos) {
        return false;
    }
    
    // Filter out operator overloads
    if (funcName.find("operator") != std::string::npos) {
        return false;
    }
    
    // Filter out compiler/system internal functions (starting with __)
    if (funcName.find("__") == 0) {
        return false;
    }
    
    // Filter out templated function names (contain < or >)
    if (funcName.find("<") != std::string::npos || funcName.find(">") != std::string::npos) {
        return false;
    }
    
    return true;
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