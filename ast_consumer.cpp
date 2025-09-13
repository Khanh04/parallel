#include "ast_consumer.h"
#include "clang/AST/ASTContext.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/Lex/Preprocessor.h"
#include <memory>
#include <fstream>
#include <algorithm>
#include <sstream>

// External declaration for global flag
extern bool enableLoopParallelization;

using namespace clang;

HybridParallelizerConsumer::HybridParallelizerConsumer(CompilerInstance &CI, const std::string &inputFile) 
    : CI(CI), inputFileName(inputFile), functionAnalyzer(globalCollector.globalVariables),
      mainExtractor(&CI.getSourceManager()),
      loopAnalyzer(&CI.getSourceManager(), globalCollector.globalVariables),
      typedefCollector(&CI.getSourceManager()) {}  // NEW: Initialize typedef collector

void HybridParallelizerConsumer::HandleTranslationUnit(ASTContext &Context) {
    TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
    
    // First pass: collect global variables
    globalCollector.TraverseDecl(TU);
    
    // NEW: First pass: collect typedefs and type aliases
    typedefCollector.TraverseDecl(TU);
    
    // Update analyzers with global variables
    functionAnalyzer.globalVars = globalCollector.globalVariables;
    functionAnalyzer.setSourceManager(&CI.getSourceManager());
    
    // Second pass: analyze all functions (this will also find loops)
    functionAnalyzer.TraverseDecl(TU);
    
    // Third pass: analyze loops across all functions
    loopAnalyzer.TraverseDecl(TU);
    
    // Fourth pass: set loop information in function analyzer
    functionAnalyzer.setFunctionLoops(loopAnalyzer.getAllFunctionLoops());
    
    // Fifth pass: extract main function calls (only visits main function)
    mainExtractor.setFunctionAnalysis(&functionAnalyzer.functionAnalysis);
    mainExtractor.TraverseDecl(TU);
    
    // Extract original includes from source file
    std::string originalIncludes = extractOriginalIncludes(Context);
    
    // Perform parallelization analysis
    HybridParallelizer parallelizer(mainExtractor.functionCalls, 
                                   functionAnalyzer.functionAnalysis,
                                   mainExtractor.getLocalVariables(),
                                   functionAnalyzer.functionInfo,
                                   mainExtractor.getMainLoops(),
                                   globalCollector.globalVariables,
                                   originalIncludes,
                                   enableLoopParallelization,
                                   typedefCollector.sourceContext);  // NEW: Pass typedef information
    
    // Generate output
    std::string hybridCode = parallelizer.generateHybridMPIOpenMPCode();
    
    // Write to output file
    std::string outputFileName = generateOutputFileName();
    std::ofstream outFile(outputFileName);
    if (outFile.is_open()) {
        outFile << hybridCode;
        outFile.close();
        llvm::outs() << "Enhanced Hybrid MPI/OpenMP parallelized code generated: " << outputFileName << "\n";
    } else {
        llvm::errs() << "Error: Could not create output file: " << outputFileName << "\n";
    }
    
    // Generate dependency graph visualizations
    generateDependencyGraphVisualization(parallelizer);
    generateGraphvizDependencyGraph(parallelizer);
    
    // Print comprehensive analysis results
    printEnhancedAnalysisResults(parallelizer);
}

void HybridParallelizerConsumer::printEnhancedAnalysisResults(const HybridParallelizer& parallelizer) {
    llvm::outs() << "\n=== Enhanced Hybrid MPI/OpenMP Analysis Results ===\n";
    
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
    
    llvm::outs() << "\n=== Comprehensive Loop Analysis (All Functions) ===\n";
    const auto& allFunctionLoops = loopAnalyzer.getAllFunctionLoops();
    int totalLoops = 0;
    int parallelizableLoops = 0;
    
    for (const auto& pair : allFunctionLoops) {
        const std::string& funcName = pair.first;
        const std::vector<LoopInfo>& loops = pair.second;
        
        if (!loops.empty()) {
            llvm::outs() << "\nFunction: " << funcName << "\n";
            for (const auto& loop : loops) {
                totalLoops++;
                llvm::outs() << "  Loop at lines " << loop.start_line << "-" << loop.end_line << ":\n";
                llvm::outs() << "    Type: " << loop.type << "\n";
                llvm::outs() << "    Parallelizable: " << (loop.parallelizable ? "YES" : "NO") << "\n";
                if (loop.parallelizable) {
                    parallelizableLoops++;
                    llvm::outs() << "    OpenMP Schedule: " << loop.schedule_type << "\n";
                    llvm::outs() << "    Generated Pragma: " << loop.pragma_text << "\n";
                    if (!loop.reduction_vars.empty()) {
                        llvm::outs() << "    Reduction variables: ";
                        for (const auto& var : loop.reduction_vars) {
                            llvm::outs() << var << " ";
                        }
                        llvm::outs() << "(" << loop.reduction_op << ")\n";
                    }
                    if (!loop.loop_variable.empty()) {
                        llvm::outs() << "    Loop variable: " << loop.loop_variable << "\n";
                    }
                }
                llvm::outs() << "    Analysis: " << loop.analysis_notes << "\n";
                
                if (!loop.read_vars.empty()) {
                    llvm::outs() << "    Variables read: ";
                    for (const auto& var : loop.read_vars) {
                        llvm::outs() << var << " ";
                    }
                    llvm::outs() << "\n";
                }
                
                if (!loop.write_vars.empty()) {
                    llvm::outs() << "    Variables written: ";
                    for (const auto& var : loop.write_vars) {
                        llvm::outs() << var << " ";
                    }
                    llvm::outs() << "\n";
                }
            }
        }
    }
    
    llvm::outs() << "\n=== Loop Parallelization Summary ===\n";
    llvm::outs() << "Total loops found: " << totalLoops << "\n";
    llvm::outs() << "Parallelizable loops: " << parallelizableLoops << "\n";
    llvm::outs() << "Parallelization rate: " << (totalLoops > 0 ? (100.0 * parallelizableLoops / totalLoops) : 0) << "%\n";
    
    llvm::outs() << "\nFunctions with parallelizable loops:\n";
    for (const auto& pair : functionAnalyzer.functionInfo) {
        const FunctionInfo& info = pair.second;
        if (info.has_parallelizable_loops) {
            int funcParallelizable = 0;
            for (const auto& loop : info.loops) {
                if (loop.parallelizable) funcParallelizable++;
            }
            llvm::outs() << "  " << info.name << ": " << funcParallelizable 
                        << "/" << info.loops.size() << " loops parallelized\n";
        }
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
    
    llvm::outs() << "\n=== Enhanced Hybrid Parallelization Complete! ===\n";
    llvm::outs() << "The generated code combines:\n";
    llvm::outs() << "  - MPI for function-level parallelism across processes\n";
    llvm::outs() << "  - OpenMP for loop-level parallelism within each process\n";
    llvm::outs() << "  - Comprehensive loop analysis across ALL functions\n";
    llvm::outs() << "  - Automatic pragma generation for parallelizable loops\n";
    llvm::outs() << "  - Thread-safe function execution with nested parallelism\n";
}

void HybridParallelizerConsumer::generateDependencyGraphVisualization(const HybridParallelizer& parallelizer) {
    std::ofstream htmlFile("dependency_graph_visualization.html");
    if (!htmlFile.is_open()) {
        llvm::errs() << "Error: Could not create dependency graph visualization file\n";
        return;
    }

    const auto& functionCalls = mainExtractor.functionCalls;
    const auto& dependencyGraph = parallelizer.getDependencyGraph();
    auto parallelGroups = parallelizer.getParallelizableGroups();

    // Write a simple HTML visualization
    htmlFile << "<!DOCTYPE html>\n<html>\n<head>\n";
    htmlFile << "<title>MPI Parallelizer - Dependency Graph</title>\n";
    htmlFile << "<script src=\"https://d3js.org/d3.v7.min.js\"></script>\n";
    htmlFile << "<style>\n";
    htmlFile << "body { font-family: Arial, sans-serif; margin: 20px; }\n";
    htmlFile << ".node { cursor: pointer; }\n";
    htmlFile << ".node circle { stroke: #fff; stroke-width: 3px; }\n";
    htmlFile << ".link { stroke: #666; stroke-width: 2px; fill: none; }\n";
    htmlFile << ".group-0 { fill: #3498db; }\n";
    htmlFile << ".group-1 { fill: #2ecc71; }\n";
    htmlFile << ".group-2 { fill: #f39c12; }\n";
    htmlFile << "</style>\n</head>\n<body>\n";
    
    htmlFile << "<h1>Dependency Graph Visualization</h1>\n";
    htmlFile << "<p>Total Functions: " << functionCalls.size() << "</p>\n";
    htmlFile << "<p>Parallel Groups: " << parallelGroups.size() << "</p>\n";
    htmlFile << "<svg id=\"graph\" width=\"1200\" height=\"600\"></svg>\n";
    
    htmlFile << "<script>\n";
    htmlFile << "const nodes = [";

    // Generate nodes data
    for (size_t i = 0; i < functionCalls.size(); ++i) {
        const auto& call = functionCalls[i];
        
        // Determine which group this function belongs to
        int groupId = -1;
        for (size_t g = 0; g < parallelGroups.size(); ++g) {
            if (std::find(parallelGroups[g].begin(), parallelGroups[g].end(), i) != parallelGroups[g].end()) {
                groupId = g;
                break;
            }
        }

        htmlFile << "{";
        htmlFile << "id: " << i << ", ";
        htmlFile << "name: \"" << call.functionName << "\", ";
        htmlFile << "group: " << groupId << "";
        htmlFile << "}";
        if (i < functionCalls.size() - 1) htmlFile << ",";
        htmlFile << "\n";
    }

    htmlFile << "];\n\n";
    htmlFile << "const links = [";

    // Generate links data for dependencies
    bool firstLink = true;
    for (size_t i = 0; i < dependencyGraph.size(); ++i) {
        const auto& node = dependencyGraph[i];
        for (int dep : node.dependencies) {
            if (!firstLink) htmlFile << ",";
            htmlFile << "{source: " << dep << ", target: " << i << "}";
            firstLink = false;
        }
    }

    htmlFile << "];\n\n";
    
    htmlFile << "const svg = d3.select('#graph');\n";
    htmlFile << "const width = +svg.attr('width');\n";
    htmlFile << "const height = +svg.attr('height');\n";
    htmlFile << "const simulation = d3.forceSimulation(nodes)\n";
    htmlFile << "  .force('link', d3.forceLink(links).id(d => d.id).distance(100))\n";
    htmlFile << "  .force('charge', d3.forceManyBody().strength(-300))\n";
    htmlFile << "  .force('center', d3.forceCenter(width / 2, height / 2));\n";
    
    htmlFile << "const link = svg.append('g').selectAll('line').data(links).enter().append('line').attr('class', 'link');\n";
    htmlFile << "const node = svg.append('g').selectAll('circle').data(nodes).enter().append('circle')\n";
    htmlFile << "  .attr('r', 15).attr('class', d => 'group-' + (d.group >= 0 ? d.group : 'default'));\n";
    
    htmlFile << "simulation.on('tick', () => {\n";
    htmlFile << "  link.attr('x1', d => d.source.x).attr('y1', d => d.source.y)\n";
    htmlFile << "      .attr('x2', d => d.target.x).attr('y2', d => d.target.y);\n";
    htmlFile << "  node.attr('cx', d => d.x).attr('cy', d => d.y);\n";
    htmlFile << "});\n";
    
    htmlFile << "</script>\n</body>\n</html>\n";

    htmlFile.close();
    llvm::outs() << "Dependency graph visualization generated: dependency_graph_visualization.html\n";
}

void HybridParallelizerConsumer::generateGraphvizDependencyGraph(const HybridParallelizer& parallelizer) {
    std::ofstream dotFile("dependency_graph.dot");
    if (!dotFile.is_open()) {
        llvm::errs() << "Error: Could not create Graphviz DOT file\n";
        return;
    }

    const auto& functionCalls = mainExtractor.functionCalls;
    const auto& dependencyGraph = parallelizer.getDependencyGraph();
    auto parallelGroups = parallelizer.getParallelizableGroups();
    const auto& functionAnalysis = functionAnalyzer.functionAnalysis;

    // DOT file header with graph attributes
    dotFile << "digraph DependencyGraph {\n";
    dotFile << "    // Graph attributes\n";
    dotFile << "    rankdir=TB;\n";
    dotFile << "    node [shape=box, style=filled, fontname=\"Arial\", fontsize=10];\n";
    dotFile << "    edge [fontname=\"Arial\", fontsize=8];\n";
    dotFile << "    bgcolor=white;\n";
    dotFile << "    label=\"MPI Parallelizer - Function Dependency Graph\\n";
    dotFile << "Total Functions: " << functionCalls.size() << "\\n";
    dotFile << "Parallel Groups: " << parallelGroups.size() << "\";\n";
    dotFile << "    labelloc=t;\n";
    dotFile << "    fontsize=16;\n\n";

    // Define color schemes for parallel groups
    const std::vector<std::string> groupColors = {
        "#E3F2FD", "#E8F5E8", "#FFF3E0", "#F3E5F5", "#FFEBEE", "#E0F2F1"
    };
    const std::vector<std::string> borderColors = {
        "#1976D2", "#388E3C", "#F57C00", "#7B1FA2", "#D32F2F", "#00796B"
    };

    // Generate subgraphs for each parallel group
    for (size_t g = 0; g < parallelGroups.size(); ++g) {
        dotFile << "    // Parallel Group " << g << "\n";
        dotFile << "    subgraph cluster_group" << g << " {\n";
        dotFile << "        label=\"Parallel Group " << g << " (" << parallelGroups[g].size() << " functions)\";\n";
        dotFile << "        style=filled;\n";
        dotFile << "        fillcolor=\"" << (g < groupColors.size() ? groupColors[g] : "#F5F5F5") << "\";\n";
        dotFile << "        color=\"" << (g < borderColors.size() ? borderColors[g] : "#666666") << "\";\n";
        dotFile << "        fontcolor=\"" << (g < borderColors.size() ? borderColors[g] : "#666666") << "\";\n";

        for (int funcIdx : parallelGroups[g]) {
            const auto& call = functionCalls[funcIdx];
            dotFile << "        func" << funcIdx;
        }
        dotFile << ";\n    }\n\n";
    }

    // Generate nodes with detailed information
    dotFile << "    // Function nodes\n";
    for (size_t i = 0; i < functionCalls.size(); ++i) {
        const auto& call = functionCalls[i];
        
        // Determine group for coloring
        int groupId = -1;
        for (size_t g = 0; g < parallelGroups.size(); ++g) {
            if (std::find(parallelGroups[g].begin(), parallelGroups[g].end(), i) != parallelGroups[g].end()) {
                groupId = g;
                break;
            }
        }

        // Get function analysis info
        std::string funcDetails = call.functionName;
        if (functionAnalysis.count(call.functionName)) {
            const auto& analysis = functionAnalysis.at(call.functionName);
            funcDetails += "\\n(" + analysis.returnType + ")";
            
            if (!analysis.readSet.empty()) {
                funcDetails += "\\nReads: ";
                bool first = true;
                for (const auto& var : analysis.readSet) {
                    if (!first) funcDetails += ", ";
                    funcDetails += var;
                    first = false;
                    if (funcDetails.length() > 100) { // Limit length
                        funcDetails += "...";
                        break;
                    }
                }
            }
            
            if (!analysis.writeSet.empty()) {
                funcDetails += "\\nWrites: ";
                bool first = true;
                for (const auto& var : analysis.writeSet) {
                    if (!first) funcDetails += ", ";
                    funcDetails += var;
                    first = false;
                    if (funcDetails.length() > 150) { // Limit length
                        funcDetails += "...";
                        break;
                    }
                }
            }
        }

        dotFile << "    func" << i << " [";
        dotFile << "label=\"" << funcDetails << "\"";
        
        if (call.hasReturnValue) {
            dotFile << ", shape=box, style=\"filled,bold\"";
        } else {
            dotFile << ", shape=ellipse, style=filled";
        }
        
        if (groupId >= 0 && groupId < (int)groupColors.size()) {
            dotFile << ", fillcolor=\"" << groupColors[groupId] << "\"";
            dotFile << ", color=\"" << borderColors[groupId] << "\"";
        } else {
            dotFile << ", fillcolor=\"#F5F5F5\", color=\"#666666\"";
        }
        
        dotFile << "];\n";
    }

    dotFile << "\n    // Dependencies\n";
    
    // Generate dependency edges with labels
    for (size_t i = 0; i < dependencyGraph.size(); ++i) {
        const auto& node = dependencyGraph[i];
        for (int dep : node.dependencies) {
            dotFile << "    func" << dep << " -> func" << i;
            
            // Add edge label with dependency reason
            if (!node.dependencyReason.empty()) {
                std::string reason = node.dependencyReason;
                // Simplify long reasons
                if (reason.length() > 30) {
                    size_t colonPos = reason.find(':');
                    if (colonPos != std::string::npos && colonPos < 25) {
                        reason = reason.substr(0, colonPos + 1) + "...";
                    } else {
                        reason = reason.substr(0, 27) + "...";
                    }
                }
                dotFile << " [label=\"" << reason << "\", color=red, fontcolor=red]";
            } else {
                dotFile << " [color=red]";
            }
            
            dotFile << ";\n";
        }
    }

    // Add legend
    dotFile << "\n    // Legend\n";
    dotFile << "    subgraph cluster_legend {\n";
    dotFile << "        label=\"Legend\";\n";
    dotFile << "        style=filled;\n";
    dotFile << "        fillcolor=\"#FFFFFE\";\n";
    dotFile << "        color=\"#CCCCCC\";\n";
    dotFile << "        rank=sink;\n";
    dotFile << "        legend_return [label=\"Function with\\nReturn Value\", shape=box, style=\"filled,bold\", fillcolor=\"#E3F2FD\"];\n";
    dotFile << "        legend_void [label=\"Void Function\", shape=ellipse, style=filled, fillcolor=\"#E8F5E8\"];\n";
    dotFile << "        legend_dep [label=\"Dependency\", shape=plaintext];\n";
    dotFile << "        legend_return -> legend_void [style=invis];\n";
    dotFile << "        legend_void -> legend_dep [color=red, label=\"depends on\"];\n";
    dotFile << "    }\n";

    dotFile << "}\n";

    dotFile.close();
    llvm::outs() << "Graphviz DOT file generated: dependency_graph.dot\n";
    llvm::outs() << "To generate PNG: dot -Tpng dependency_graph.dot -o dependency_graph.png\n";
    llvm::outs() << "To generate SVG: dot -Tsvg dependency_graph.dot -o dependency_graph.svg\n";
}

std::string HybridParallelizerConsumer::extractOriginalIncludes(ASTContext &Context) {
    SourceManager &SM = Context.getSourceManager();
    const FileEntry *MainFileEntry = SM.getFileEntryForID(SM.getMainFileID());
    
    if (!MainFileEntry) {
        return "";
    }
    
    // Get the buffer for the main file
    auto Buffer = SM.getBufferOrNone(SM.getMainFileID());
    if (!Buffer) {
        return "";
    }
    
    std::string FileContent = Buffer->getBuffer().str();
    std::stringstream includes;
    std::istringstream stream(FileContent);
    std::string line;
    
    // Extract all #include lines from the beginning of the file
    while (std::getline(stream, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (line.empty()) {
            continue; // Skip empty lines
        } else if (line.find("#include") == 0) {
            includes << line << "\n";
        } else if (line.find("#") != 0 && !line.empty()) {
            // If we hit non-preprocessor directive (except comments), stop looking for includes
            break;
        }
        // Skip comments and other preprocessor directives but continue looking
    }
    
    return includes.str();
}

// NEW: TypedefCollector implementation
std::string TypedefCollector::getSourceText(SourceRange range) {
    if (range.isInvalid() || !SM) {
        return "";
    }
    
    bool invalid = false;
    StringRef text = Lexer::getSourceText(CharSourceRange::getTokenRange(range), 
                                         *SM, LangOptions(), &invalid);
    if (invalid) {
        return "";
    }
    
    return text.str();
}

bool TypedefCollector::VisitTypedefDecl(TypedefDecl *TD) {
    // Only collect typedefs from the main file (not system headers)
    if (SM && !SM->isInSystemHeader(TD->getLocation()) && SM->isInMainFile(TD->getLocation())) {
        TypedefInfo typedefInfo;
        typedefInfo.name = TD->getNameAsString();
        typedefInfo.definition = getSourceText(TD->getSourceRange());
        typedefInfo.underlyingType = TD->getUnderlyingType().getAsString();
        typedefInfo.line = SM->getSpellingLineNumber(TD->getLocation());
        
        sourceContext.typedefs.push_back(typedefInfo);
    }
    return true;
}

bool TypedefCollector::VisitTypeAliasDecl(TypeAliasDecl *TAD) {
    // Handle C++11 'using' type aliases
    if (SM && !SM->isInSystemHeader(TAD->getLocation()) && SM->isInMainFile(TAD->getLocation())) {
        TypedefInfo typedefInfo;
        typedefInfo.name = TAD->getNameAsString();
        typedefInfo.definition = getSourceText(TAD->getSourceRange());
        typedefInfo.underlyingType = TAD->getUnderlyingType().getAsString();
        typedefInfo.line = SM->getSpellingLineNumber(TAD->getLocation());
        
        sourceContext.typedefs.push_back(typedefInfo);
    }
    return true;
}

std::string HybridParallelizerConsumer::generateOutputFileName() const {
    // Extract base filename without extension
    std::string basename = inputFileName;
    
    // Find the last directory separator
    size_t lastSlash = basename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        basename = basename.substr(lastSlash + 1);
    }
    
    // Remove the .cpp extension if present
    size_t lastDot = basename.find_last_of('.');
    if (lastDot != std::string::npos) {
        basename = basename.substr(0, lastDot);
    }
    
    // Return the new filename with _parallelized suffix
    return basename + "_parallelized.cpp";
}

std::unique_ptr<ASTConsumer> HybridParallelizerAction::CreateASTConsumer(CompilerInstance &CI,
                                                                        llvm::StringRef file) {
    return std::make_unique<HybridParallelizerConsumer>(CI, file.str());
}