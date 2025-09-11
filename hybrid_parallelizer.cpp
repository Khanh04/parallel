#include "hybrid_parallelizer.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <set>

HybridParallelizer::HybridParallelizer(const std::vector<FunctionCall>& calls, 
                                     const std::map<std::string, FunctionAnalysis>& analysis,
                                     const std::map<std::string, LocalVariable>& localVars,
                                     const std::map<std::string, FunctionInfo>& funcInfo,
                                     const std::vector<LoopInfo>& loops,
                                     const std::set<std::string>& globals,
                                     bool enableLoops)
    : functionCalls(calls), functionAnalysis(analysis), 
      localVariables(localVars), functionInfo(funcInfo),
      mainLoops(loops), globalVariables(globals),
      enableLoopParallelization(enableLoops) {
    buildDependencyGraph();
}

std::string HybridParallelizer::normalizeType(const std::string& cppType) {
    if (cppType == "_Bool") return "bool";
    return cppType;
}

std::string HybridParallelizer::getMPIDatatype(const std::string& cppType) {
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

std::string HybridParallelizer::getDefaultValue(const std::string& cppType) {
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

void HybridParallelizer::buildDependencyGraph() {
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
                
                const auto& analysisA = functionAnalysis.at(functionCalls[i].functionName);
                const auto& analysisB = functionAnalysis.at(functionCalls[j].functionName);
                
                for (const auto& writeVar : analysisA.writeSet) {
                    if (analysisB.readSet.count(writeVar)) {
                        hasDependency = true;
                        reason = "Global variable RAW: " + writeVar;
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
                            reason = "Global variable WAR: " + readVar;
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

std::vector<std::vector<int>> HybridParallelizer::getParallelizableGroups() const {
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

const std::vector<DependencyNode>& HybridParallelizer::getDependencyGraph() const {
    return dependencyGraph;
}

const std::map<std::string, LocalVariable>& HybridParallelizer::getLocalVariables() const {
    return localVariables;
}

std::string HybridParallelizer::extractFunctionCall(const std::string& originalCall) {
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

std::string HybridParallelizer::generateParallelizedFunctionBody(const FunctionInfo& info) {
    std::string parallelizedBody = info.original_body;
    
    // If loop parallelization is disabled, return original body
    if (!enableLoopParallelization || info.loops.empty()) {
        return parallelizedBody;
    }
    
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
    
    // Process loops from last to first to avoid position shifts when inserting
    std::vector<LoopInfo> sortedLoops = info.loops;
    std::sort(sortedLoops.begin(), sortedLoops.end(), 
              [](const LoopInfo& a, const LoopInfo& b) {
                  if (a.start_line != b.start_line) return a.start_line > b.start_line;
                  return a.start_col > b.start_col;
              });
    
    // Remove duplicates based on source code content
    std::set<std::string> processedSourceCode;
    
    for (const auto& loop : sortedLoops) {
        if (!loop.parallelizable || loop.pragma_text.empty()) {
            continue;
        }
        
        // Skip if we've already processed this exact loop
        if (processedSourceCode.count(loop.source_code)) {
            continue;
        }
        processedSourceCode.insert(loop.source_code);
        
        // Find the loop in the body - be more flexible since thread-safe replacements may have modified the exact source
        // Try exact match first, then fall back to pattern matching
        size_t loopPos = parallelizedBody.find(loop.source_code);
        if (loopPos == std::string::npos && !loop.loop_variable.empty()) {
            // Try to find by loop variable pattern: "for (type var = ..."
            std::string loopPattern = "for (" + loop.loop_variable;
            loopPos = parallelizedBody.find(loopPattern);
            if (loopPos == std::string::npos) {
                // Try more general pattern: "for (int " + loop_variable
                loopPattern = "for (int " + loop.loop_variable;
                loopPos = parallelizedBody.find(loopPattern);
            }
        }
        if (loopPos == std::string::npos) {
            continue;
        }
        
        // Check if there's already a pragma right before this loop
        size_t lineStart = parallelizedBody.rfind('\n', loopPos);
        if (lineStart == std::string::npos) lineStart = 0;
        else lineStart++;
        
        // Look for existing pragma in the 5 lines before the loop
        size_t searchStart = lineStart > 200 ? lineStart - 200 : 0;
        std::string beforeLoop = parallelizedBody.substr(searchStart, loopPos - searchStart);
        if (beforeLoop.find("#pragma omp parallel for") != std::string::npos) {
            continue; // Already has a pragma
        }
        
        // Find the actual "for" keyword position to get correct indentation
        size_t forPos = parallelizedBody.find("for", loopPos);
        if (forPos == std::string::npos || forPos > loopPos + loop.source_code.length()) {
            continue;
        }
        
        // Find the line start for the "for" statement
        size_t forLineStart = parallelizedBody.rfind('\n', forPos);
        if (forLineStart == std::string::npos) forLineStart = 0;
        else forLineStart++;
        
        // Get indentation from the "for" line (not the loop body)
        std::string indentation = "";
        for (size_t i = forLineStart; i < forPos && (parallelizedBody[i] == ' ' || parallelizedBody[i] == '\t'); i++) {
            indentation += parallelizedBody[i];
        }
        
        // Insert the pragma with the same indentation as the for loop
        std::string pragmaLine = indentation + loop.pragma_text + "\n";
        parallelizedBody.insert(forLineStart, pragmaLine);
    }
    
    return parallelizedBody;
}

std::string HybridParallelizer::generateHybridMPIOpenMPCode() {
    auto parallelGroups = getParallelizableGroups();
    
    std::stringstream mpiCode;
    
    // Headers
    mpiCode << "#include <mpi.h>\n";
    mpiCode << "#include <omp.h>\n";
    mpiCode << "#include <stdio.h>\n";
    mpiCode << "#include <iostream>\n";
    mpiCode << "#include <vector>\n";
    mpiCode << "#include <cmath>\n";
    mpiCode << "#include <time.h>\n\n";
    
    // Add global variables if any functions use them
    bool hasGlobalReads = false;
    for (const auto& pair : functionAnalysis) {
        if (!pair.second.readSet.empty() || !pair.second.writeSet.empty()) {
            hasGlobalReads = true;
            break;
        }
    }
    
    if (hasGlobalReads && !globalVariables.empty()) {
        mpiCode << "// Global variables\n";
        for (const auto& globalVar : globalVariables) {
            // Infer type from variable name (basic heuristic)
            std::string varType = "int"; // default
            std::string defaultValue = "0";
            
            if (globalVar.find("sum") != std::string::npos || globalVar.find("result") != std::string::npos) {
                varType = "double";
                defaultValue = "0.0";
            } else if (globalVar.find("flag") != std::string::npos) {
                varType = "bool";
                defaultValue = "false";
            } else if (globalVar.find("array") != std::string::npos) {
                varType = "int";
                defaultValue = "[1000]"; // Array declaration
                mpiCode << varType << " " << globalVar << defaultValue << ";\n";
                continue;
            }
            
            mpiCode << varType << " " << globalVar << " = " << defaultValue << ";\n";
        }
        mpiCode << "\n";
    }
    
    // Output parallelized function definitions
    std::set<std::string> outputFunctions;
    for (const auto& call : functionCalls) {
        if (outputFunctions.find(call.functionName) == outputFunctions.end()) {
            outputFunctions.insert(call.functionName);
            
            if (functionInfo.count(call.functionName)) {
                const FunctionInfo& info = functionInfo.at(call.functionName);
                
                mpiCode << "// Parallelized function: " << info.name << "\n";
                if (enableLoopParallelization && info.has_parallelizable_loops) {
                    mpiCode << "// Contains parallelizable loops - OpenMP pragmas added\n";
                } else if (info.has_parallelizable_loops) {
                    mpiCode << "// Contains loops (OpenMP disabled by --no-loops flag)\n";
                }
                
                // Function signature
                mpiCode << info.return_type << " " << info.name << "(";
                for (size_t i = 0; i < info.parameter_types.size(); i++) {
                    if (i > 0) mpiCode << ", ";
                    mpiCode << info.parameter_types[i];
                    if (i < info.parameter_names.size()) {
                        mpiCode << " " << info.parameter_names[i];
                    }
                }
                mpiCode << ") ";
                
                // Function body with OpenMP pragmas
                if (enableLoopParallelization && info.has_parallelizable_loops) {
                    mpiCode << generateParallelizedFunctionBody(info);
                } else {
                    mpiCode << info.original_body;
                }
                mpiCode << "\n\n";
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
    mpiCode << "        std::cout << \"=== Enhanced Hybrid MPI/OpenMP Parallelized Program ===\" << std::endl;\n";
    mpiCode << "        std::cout << \"MPI processes: \" << size << std::endl;\n";
    mpiCode << "        std::cout << \"OpenMP threads per process: \" << omp_get_max_threads() << std::endl;\n";
    mpiCode << "        std::cout << \"Functions with parallelized loops: \";\n";
    
    std::set<std::string> functionsWithLoops;
    for (const auto& pair : functionInfo) {
        if (enableLoopParallelization && pair.second.has_parallelizable_loops && pair.first != "main") {
            functionsWithLoops.insert(pair.first);
        }
    }
    
    for (const std::string& funcName : functionsWithLoops) {
        mpiCode << "        std::cout << \"  " << funcName << "\" << std::endl;\n";
    }
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
    
    // Generate parallel execution logic
    int groupIndex = 0;
    for (const auto& group : parallelGroups) {
        mpiCode << "    // === Parallel group " << groupIndex << " ===\n";
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
                    mpiCode << "        if (" << i << " < size) {\n";
                    mpiCode << "            MPI_Recv(&result_" << callIdx 
                           << ", 1, " << mpiType << ", " << i << ", " << callIdx 
                           << ", MPI_COMM_WORLD, MPI_STATUS_IGNORE);\n";
                    mpiCode << "        }\n";
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
    
    // Print results (avoiding duplicates)
    mpiCode << "    if (rank == 0) {\n";
    mpiCode << "        std::cout << \"\\n=== Results ===\" << std::endl;\n";
    
    // Print loop parallelization summary
    mpiCode << "        std::cout << \"\\n=== Loop Parallelization Summary ===\" << std::endl;\n";
    if (!enableLoopParallelization) {
        mpiCode << "        std::cout << \"Loop parallelization DISABLED (--no-loops flag)\" << std::endl;\n";
    }
    
    // Create a map to track unique loops per function
    std::map<std::string, std::vector<LoopInfo>> uniqueFunctionLoops;
    for (const auto& pair : functionInfo) {
        const FunctionInfo& info = pair.second;
        if (!info.loops.empty()) {
            std::vector<LoopInfo> uniqueLoops;
            std::set<std::string> processedLoops;
            
            for (const auto& loop : info.loops) {
                std::string loopKey = std::to_string(loop.start_line) + "_" + std::to_string(loop.start_col);
                if (processedLoops.find(loopKey) == processedLoops.end()) {
                    uniqueLoops.push_back(loop);
                    processedLoops.insert(loopKey);
                }
            }
            uniqueFunctionLoops[info.name] = uniqueLoops;
        }
    }
    
    for (const auto& pair : uniqueFunctionLoops) {
        const std::string& funcName = pair.first;
        const std::vector<LoopInfo>& loops = pair.second;
        
        // Skip main function loops since we're not parallelizing them in the generated MPI code
        if (funcName == "main") {
            continue;
        }
        
        mpiCode << "        std::cout << \"Function " << funcName << ": \" << " << loops.size() << " << \" loops found\" << std::endl;\n";
        for (const auto& loop : loops) {
            if (loop.parallelizable) {
                mpiCode << "        std::cout << \"  - Line " << loop.start_line << ": PARALLELIZED (" << loop.type << ")\" << std::endl;\n";
            } else {
                mpiCode << "        std::cout << \"  - Line " << loop.start_line << ": not parallelized (" << loop.type << ")\" << std::endl;\n";
            }
        }
    }
    
    // Print variable results (avoiding duplicates)
    std::set<std::string> printedVars;
    
    for (const auto& pair : localVariables) {
        const LocalVariable& localVar = pair.second;
        if (localVar.definedAtCall >= 0 && printedVars.find(localVar.name) == printedVars.end()) {
            mpiCode << "        std::cout << \"" << localVar.name << " = \" << " << localVar.name << " << std::endl;\n";
            printedVars.insert(localVar.name);
        }
    }
    
    // Print function call results (avoiding duplicates)
    for (int i = 0; i < functionCalls.size(); ++i) {
        if (functionCalls[i].hasReturnValue) {
            std::string varName = functionCalls[i].returnVariable;
            if (!varName.empty() && printedVars.find(varName) == printedVars.end()) {
                mpiCode << "        std::cout << \"" << varName << " = \" << " << varName << " << std::endl;\n";
                printedVars.insert(varName);
            } else if (varName.empty()) {
                mpiCode << "        std::cout << \"" << functionCalls[i].functionName 
                       << " result: \" << result_" << i << " << std::endl;\n";
            }
        }
    }
    
    mpiCode << "        std::cout << \"\\n=== Enhanced Hybrid MPI/OpenMP Execution Complete ===\" << std::endl;\n";
    mpiCode << "    }\n\n";
    
    mpiCode << "    MPI_Finalize();\n";
    mpiCode << "    return 0;\n";
    mpiCode << "}\n";
    
    return mpiCode.str();
}