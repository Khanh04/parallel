// MPI Parallelizer with Automatic File Output

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

// Global variables to store input filename for output generation
std::string InputFileName;

struct AutoCallInfo {
    CallExpr* expr;
    std::string functionName;
    SourceLocation location;
    std::string sourceText;
    bool hasReturnType;
    bool hasParameters;
    std::vector<std::string> parameterVars;
    int statementIndex;
    bool isAssignedToVariable;
};

class VariableExtractor : public RecursiveASTVisitor<VariableExtractor> {
private:
    std::vector<std::string>& variables;

public:
    VariableExtractor(std::vector<std::string>& vars) : variables(vars) {}

    bool VisitDeclRefExpr(DeclRefExpr* expr) {
        if (auto* varDecl = dyn_cast<VarDecl>(expr->getDecl())) {
            variables.push_back(varDecl->getNameAsString());
        }
        return true;
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

public:
    AutoOutputMPIVisitor(ASTContext* ctx, Rewriter& r) 
        : context(ctx), rewriter(r), mainFunc(nullptr), inMainFunction(false), stmtCounter(0) {}

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
        
        bool result = RecursiveASTVisitor::TraverseFunctionDecl(func);
        
        inMainFunction = wasInMain;
        stmtCounter = oldCounter;
        return result;
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
            
            getCallSourceText(call, info.sourceText);
            analyzeParameters(call, info);
            checkAssignmentContext(call, info);
            
            calls.push_back(info);
            
            if (VerboseMode) {
                std::cerr << "Found call: " << funcName 
                          << " (returns: " << (info.hasReturnType ? "yes" : "no") 
                          << ", params: " << (info.hasParameters ? "yes" : "no");
                
                if (!info.parameterVars.empty()) {
                    std::cerr << ", uses vars: ";
                    for (size_t i = 0; i < info.parameterVars.size(); ++i) {
                        std::cerr << info.parameterVars[i];
                        if (i < info.parameterVars.size() - 1) std::cerr << ", ";
                    }
                }
                
                if (info.isAssignedToVariable) {
                    std::cerr << ", assigned to var";
                }
                
                std::cerr << ")" << std::endl;
            }
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

        if (!SilentMode) {
            std::cerr << "Analyzing " << calls.size() << " function calls for parallelization..." << std::endl;
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
        for (unsigned i = 0; i < call->getNumArgs(); ++i) {
            Expr* arg = call->getArg(i);
            VariableExtractor extractor(info.parameterVars);
            extractor.TraverseStmt(arg);
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

    std::vector<std::vector<AutoCallInfo>> createGroups() {
        std::vector<std::vector<AutoCallInfo>> groups;
        std::vector<bool> processed(calls.size(), false);
        
        for (size_t i = 0; i < calls.size(); ++i) {
            if (processed[i]) continue;

            if (!canBeParallelized(calls[i])) {
                processed[i] = true;
                continue;
            }

            std::vector<AutoCallInfo> currentGroup;
            currentGroup.push_back(calls[i]);
            processed[i] = true;

            for (size_t j = i + 1; j < calls.size(); ++j) {
                if (processed[j]) continue;
                
                if (!canBeParallelized(calls[j])) {
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

    bool canBeParallelized(const AutoCallInfo& call) {
        if (call.hasReturnType) {
            if (VerboseMode) {
                std::cerr << "  " << call.functionName << " cannot be parallelized: has return type" << std::endl;
            }
            return false;
        }

        if (call.hasParameters) {
            if (VerboseMode) {
                std::cerr << "  " << call.functionName << " cannot be parallelized: has parameters" << std::endl;
            }
            return false;
        }

        if (call.isAssignedToVariable) {
            if (VerboseMode) {
                std::cerr << "  " << call.functionName << " cannot be parallelized: assigned to variable" << std::endl;
            }
            return false;
        }

        return true;
    }

    bool hasConflict(const AutoCallInfo& call1, const AutoCallInfo& call2) {
        for (const std::string& var1 : call1.parameterVars) {
            for (const std::string& var2 : call2.parameterVars) {
                if (var1 == var2) {
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
            
            std::vector<std::string> reasons;
            if (call.hasReturnType) reasons.push_back("HAS_RETURN");
            if (call.hasParameters) reasons.push_back("HAS_PARAMS");
            if (call.isAssignedToVariable) reasons.push_back("ASSIGNED_TO_VAR");
            
            if (reasons.empty()) {
                std::cerr << "PARALLELIZABLE";
            } else {
                for (size_t i = 0; i < reasons.size(); ++i) {
                    std::cerr << reasons[i];
                    if (i < reasons.size() - 1) std::cerr << " ";
                }
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