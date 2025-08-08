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

// Include our modular headers
#include "data_structures.h"
#include "ast_consumer.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

int main(int argc, const char **argv) {
    if (argc < 2) {
        llvm::errs() << "Usage: " << argv[0] << " <source-file>\n";
        llvm::errs() << "\nThis enhanced tool generates comprehensive hybrid MPI/OpenMP parallelized code:\n";
        llvm::errs() << "  - MPI for parallelizing independent function calls across processes\n";
        llvm::errs() << "  - OpenMP for parallelizing ALL loops in ALL functions\n";
        llvm::errs() << "  - Automatic dependency analysis and pragma generation\n";
        llvm::errs() << "  - Comprehensive loop analysis with detailed reporting\n";
        llvm::errs() << "  - Thread-safe nested parallelism support\n";
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