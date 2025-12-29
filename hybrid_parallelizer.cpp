#include "hybrid_parallelizer.h"
#include "type_mapping.h"
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
                                     const std::string& includes,
                                     bool enableLoops,
                                     const SourceCodeContext& context,
                                     const std::string& mainBody)
    : functionCalls(calls), functionAnalysis(analysis), 
      localVariables(localVars), functionInfo(funcInfo),
      mainLoops(loops), globalVariables(globals),
      originalIncludes(includes), enableLoopParallelization(enableLoops),
      sourceContext(context), mainFunctionBody(mainBody) {  // NEW: Store main body
    buildDependencyGraph();
}

// Type mapping functions moved to TypeMapper utility class

bool HybridParallelizer::isTypePrintable(const std::string& cppType) {
    std::string normalizedType = TypeMapper::normalizeType(cppType);
    
    // Basic types that are printable
    if (normalizedType == "int" || normalizedType == "double" || normalizedType == "float" ||
        normalizedType == "bool" || normalizedType == "char" || normalizedType == "long" ||
        normalizedType == "unsigned int" || normalizedType == "long long") {
        return true;
    }
    
    // std::string is printable
    if (normalizedType.find("std::string") != std::string::npos) {
        return true;
    }
    
    // Complex types like std::chrono are not directly printable
    if (normalizedType.find("std::chrono") != std::string::npos ||
        normalizedType.find("std::") != std::string::npos ||
        normalizedType.find("::") != std::string::npos) {
        return false;
    }
    
    return true; // Assume unknown simple types are printable
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

std::string HybridParallelizer::extractIncludesOnly(const std::string& source) {
    std::stringstream result;
    std::istringstream sourceStream(source);
    std::string line;
    
    while (std::getline(sourceStream, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos) {
            std::string trimmed = line.substr(start);
            
            // Include #include, #define, #pragma, typedef, using statements, and empty lines
            if (trimmed.substr(0, 8) == "#include" || 
                trimmed.substr(0, 7) == "#define" ||
                trimmed.substr(0, 7) == "#pragma" ||
                trimmed.substr(0, 7) == "typedef" ||
                trimmed.substr(0, 5) == "using" ||
                trimmed.substr(0, 2) == "//" ||
                trimmed.empty()) {
                result << line << "\n";
            } else {
                // Stop at first function definition or other code
                break;
            }
        } else {
            // Empty line, keep it
            result << line << "\n";
        }
    }
    
    return result.str();
}

// Helper to find matching brace
static size_t findMatchingBrace(const std::string& str, size_t startPos) {
    int depth = 0;
    for (size_t i = startPos; i < str.length(); ++i) {
        if (str[i] == '{') depth++;
        else if (str[i] == '}') {
            depth--;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

static std::string getMPIOp(const std::string& op) {
    if (op == "+") return "MPI_SUM";
    if (op == "*") return "MPI_PROD";
    if (op == "min") return "MPI_MIN";
    if (op == "max") return "MPI_MAX";
    if (op == "&") return "MPI_BAND";
    if (op == "|") return "MPI_BOR";
    if (op == "^") return "MPI_BXOR";
    if (op == "&&") return "MPI_LAND";
    if (op == "||") return "MPI_LOR";
    return "MPI_SUM"; // Default
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
        // Use static thread_local to ensure proper per-thread initialization
        size_t bracePos = parallelizedBody.find('{');
        if (bracePos != std::string::npos) {
            // Use thread_local with lazy initialization inside the parallel region
            // The seed will be initialized properly when first accessed by each thread
            std::string seedDecl = "\n    static thread_local unsigned int __thread_seed = 0;\n"
                                   "    static thread_local bool __seed_initialized = false;";
            parallelizedBody.insert(bracePos + 1, seedDecl);
            
            // Add initialization inside each parallel loop
            // Find all #pragma omp parallel for and add seed init after loop start
            size_t pragmaPos = 0;
            while ((pragmaPos = parallelizedBody.find("#pragma omp parallel for", pragmaPos)) != std::string::npos) {
                // Find the loop body's opening brace
                size_t forPos = parallelizedBody.find("for", pragmaPos + 24);
                if (forPos != std::string::npos) {
                    size_t loopBrace = parallelizedBody.find('{', forPos);
                    if (loopBrace != std::string::npos) {
                        std::string seedInit = "\n        if (!__seed_initialized) { __thread_seed = (unsigned int)time(NULL) ^ omp_get_thread_num(); __seed_initialized = true; }";
                        parallelizedBody.insert(loopBrace + 1, seedInit);
                        pragmaPos = loopBrace + seedInit.length() + 1;
                    } else {
                        pragmaPos += 24;
                    }
                } else {
                    pragmaPos += 24;
                }
            }
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
        
        // NEW: MPI Loop Parallelization
        if (loop.is_mpi_parallelizable) {
             size_t bodyStart = parallelizedBody.find('{', loopPos);
             if (bodyStart != std::string::npos) {
                 size_t loopEnd = findMatchingBrace(parallelizedBody, bodyStart);
                 if (loopEnd != std::string::npos) {
                     std::string existingBody = parallelizedBody.substr(bodyStart, loopEnd - bodyStart + 1);
                     
                     std::stringstream mpiCode;
                     mpiCode << "{\n";
                     mpiCode << "    // Hybrid MPI+OpenMP Parallel Loop\n";
                     mpiCode << "    int _mpi_rank, _mpi_size;\n";
                     mpiCode << "    MPI_Comm_rank(MPI_COMM_WORLD, &_mpi_rank);\n";
                     mpiCode << "    MPI_Comm_size(MPI_COMM_WORLD, &_mpi_size);\n";
                     
                     mpiCode << "    long _loop_start = " << loop.start_expr << ";\n";
                     mpiCode << "    long _loop_end = " << loop.end_expr << ";\n";
                     mpiCode << "    long _loop_step = " << loop.step_expr << ";\n";
                     // Handle both positive and negative step loops
                     mpiCode << "    bool _is_negative_step = (_loop_step < 0);\n";
                     mpiCode << "    long _abs_step = _is_negative_step ? -_loop_step : _loop_step;\n";
                     mpiCode << "    long _total_iters = _is_negative_step ? (_loop_start - _loop_end) / _abs_step : (_loop_end - _loop_start) / _loop_step;\n";
                     mpiCode << "    long _chunk_size = _total_iters / _mpi_size;\n";
                     mpiCode << "    long _remainder = _total_iters % _mpi_size;\n";
                     mpiCode << "    long _my_start_iter = _mpi_rank * _chunk_size + (_mpi_rank < _remainder ? _mpi_rank : _remainder);\n";
                     mpiCode << "    long _my_count = _chunk_size + (_mpi_rank < _remainder ? 1 : 0);\n";
                     mpiCode << "    long _my_start = _loop_start + _my_start_iter * _loop_step;\n";
                     mpiCode << "    long _my_end = _my_start + _my_count * _loop_step;\n";
                     
                     // Generate two separate loops for positive/negative step (OpenMP doesn't allow ternary in loop condition)
                     mpiCode << "    if (_is_negative_step) {\n";
                     mpiCode << "        " << loop.pragma_text << "\n";
                     mpiCode << "        for (";
                     if (!loop.loop_variable_type.empty()) {
                         mpiCode << loop.loop_variable_type << " ";
                     }
                     mpiCode << loop.loop_variable << " = _my_start; "
                             << loop.loop_variable << " > _my_end; "
                             << loop.loop_variable << " += " << loop.step_expr << ") ";
                     mpiCode << existingBody << "\n";
                     mpiCode << "    } else {\n";
                     mpiCode << "        " << loop.pragma_text << "\n";
                     mpiCode << "        for (";
                     if (!loop.loop_variable_type.empty()) {
                         mpiCode << loop.loop_variable_type << " ";
                     }
                     mpiCode << loop.loop_variable << " = _my_start; "
                             << loop.loop_variable << " < _my_end; "
                             << loop.loop_variable << " += " << loop.step_expr << ") ";
                     mpiCode << existingBody << "\n";
                     mpiCode << "    }\n";
                     
                     for (const auto& var : loop.reduction_vars) {
                         std::string varType = "double"; // Default
                         if (localVariables.count(var)) {
                             varType = localVariables.at(var).type;
                         }
                         
                         std::string mpiType = TypeMapper::getMPIDatatype(varType);
                         if (mpiType.empty()) mpiType = "MPI_DOUBLE"; // Fallback
                         
                         std::string mpiOp = getMPIOp(loop.reduction_op);
                         
                         // Use separate local/global buffers to avoid double-counting with MPI_IN_PLACE
                         mpiCode << "    " << varType << " _local_" << var << " = " << var << ";\n";
                         mpiCode << "    " << varType << " _global_" << var << ";\n";
                         mpiCode << "    MPI_Allreduce(&_local_" << var << ", &_global_" << var << ", 1, " 
                                 << mpiType << ", " << mpiOp << ", MPI_COMM_WORLD);\n";
                         mpiCode << "    " << var << " = _global_" << var << ";\n";
                     }
                     
                     mpiCode << "    }\n";
                     
                     parallelizedBody.replace(loopPos, loopEnd - loopPos + 1, mpiCode.str());
                     continue; 
                 }
             }
        }

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

// NEW: Generate main body preserving original structure
std::string HybridParallelizer::generatePreservedMainBody() {
    if (mainFunctionBody.empty()) {
        return ""; // No original body to preserve
    }
    
    std::stringstream result;
    
    // Start from the original main body (without the outer braces)
    std::string body = mainFunctionBody;
    
    // Remove outer braces if present
    size_t firstBrace = body.find('{');
    size_t lastBrace = body.rfind('}');
    if (firstBrace != std::string::npos && lastBrace != std::string::npos && lastBrace > firstBrace) {
        body = body.substr(firstBrace + 1, lastBrace - firstBrace - 1);
    }
    
    // Build set of functions that have internal MPI parallelization (should run on ALL ranks)
    std::set<std::string> mpiParallelizedFunctions;
    for (const auto& pair : functionInfo) {
        if (pair.first != "main") {
            for (const auto& loop : pair.second.loops) {
                if (loop.is_mpi_parallelizable) {
                    mpiParallelizedFunctions.insert(pair.first);
                    break;
                }
            }
        }
    }
    
    // Build a map from byte offset to function call index for replacement
    // Sort by offset in reverse order to avoid position shifts during replacement
    std::vector<std::pair<unsigned, int>> offsetToCallIndex;
    for (int i = 0; i < functionCalls.size(); ++i) {
        if (functionCalls[i].statementStartOffset > 0) {
            offsetToCallIndex.push_back({functionCalls[i].statementStartOffset, i});
        }
    }
    std::sort(offsetToCallIndex.begin(), offsetToCallIndex.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    // Build variable name map for MPI reserved name conflicts
    std::map<std::string, std::string> variableNameMap;
    for (const auto& pair : localVariables) {
        std::string resolvedName = resolveVariableNameConflict(pair.first);
        variableNameMap[pair.first] = resolvedName;
    }
    
    // Get parallel groups for determining execution strategy
    auto parallelGroups = getParallelizableGroups();
    
    // Build a map from call index to group info
    std::map<int, std::pair<int, int>> callToGroup; // callIdx -> (groupIdx, positionInGroup)
    for (int gIdx = 0; gIdx < parallelGroups.size(); ++gIdx) {
        for (int pos = 0; pos < parallelGroups[gIdx].size(); ++pos) {
            callToGroup[parallelGroups[gIdx][pos]] = {gIdx, pos};
        }
    }
    
    // Replace each function call with parallelized version (in reverse order)
    for (const auto& offsetPair : offsetToCallIndex) {
        unsigned offset = offsetPair.first;
        int callIdx = offsetPair.second;
        const FunctionCall& call = functionCalls[callIdx];
        
        // Adjust offset since we removed the opening brace
        unsigned adjustedOffset = offset - 1;
        
        // Find the end of the statement (look for semicolon)
        size_t stmtEnd = body.find(';', adjustedOffset);
        if (stmtEnd == std::string::npos) {
            continue; // Can't find statement end
        }
        stmtEnd++; // Include the semicolon
        
        // Find the start of the line for proper indentation
        size_t lineStart = body.rfind('\n', adjustedOffset);
        if (lineStart == std::string::npos) lineStart = 0;
        else lineStart++;
        
        // Extract indentation
        std::string indentation;
        for (size_t i = lineStart; i < adjustedOffset && (body[i] == ' ' || body[i] == '\t'); ++i) {
            indentation += body[i];
        }
        
        // Check if this function has internal MPI parallelization
        bool hasMpiParallelization = mpiParallelizedFunctions.count(call.functionName) > 0;
        
        // Generate replacement code based on parallelization strategy
        std::stringstream replacement;
        
        if (hasMpiParallelization) {
            // Function has internal MPI loops - run on ALL ranks
            if (call.hasReturnValue) {
                replacement << indentation << "// MPI-parallelized: " << call.functionName << " (all ranks)\n";
                replacement << indentation << call.fullStatementText;
            } else {
                replacement << indentation << "// MPI-parallelized: " << call.functionName << " (all ranks)\n";
                std::string funcCall = call.callExpression;
                if (!funcCall.empty() && funcCall.back() != ';') funcCall += ";";
                replacement << indentation << funcCall;
            }
        } else if (call.hasReturnValue) {
            // No MPI parallelization - execute on rank 0 and broadcast
            replacement << indentation << "// Parallelized: " << call.functionName << " (rank 0 only)\n";
            replacement << indentation << call.returnType << " " << call.returnVariable << ";\n";
            replacement << indentation << "if (rank == 0) {\n";
            replacement << indentation << "    " << call.returnVariable << " = " << extractFunctionCall(call.callExpression) << ";\n";
            replacement << indentation << "}\n";
            // Broadcast result to all ranks
            std::string mpiType = TypeMapper::getMPIDatatype(call.returnType);
            if (!mpiType.empty()) {
                replacement << indentation << "MPI_Bcast(&" << call.returnVariable << ", 1, " << mpiType << ", 0, MPI_COMM_WORLD);";
            } else {
                replacement << indentation << "// Note: Cannot broadcast type " << call.returnType;
            }
        } else {
            // Void function without MPI parallelization - wrap in rank check
            replacement << indentation << "// Parallelized: " << call.functionName << " (rank 0 only)\n";
            replacement << indentation << "if (rank == 0) {\n";
            std::string funcCall = call.callExpression;
            if (!funcCall.empty() && funcCall.back() == ';') funcCall.pop_back();
            replacement << indentation << "    " << funcCall << ";\n";
            replacement << indentation << "}";
        }
        
        // Replace the original statement with the parallelized version
        body.replace(lineStart, stmtEnd - lineStart, replacement.str());
    }
    
    // Wrap output statements (cout, printf) in rank 0 checks
    // This is done with simple pattern matching
    std::string wrappedBody;
    std::istringstream bodyStream(body);
    std::string line;
    
    while (std::getline(bodyStream, line)) {
        // Check if this line contains output (cout or printf) and is not already in a rank check
        bool hasOutput = (line.find("std::cout") != std::string::npos || 
                         line.find("cout <<") != std::string::npos ||
                         line.find("printf") != std::string::npos);
        bool alreadyWrapped = (line.find("if (rank == 0)") != std::string::npos ||
                              line.find("// Parallelized:") != std::string::npos ||
                              line.find("// MPI-parallelized:") != std::string::npos);
        
        // Check if this is a return statement - we'll handle it specially
        bool isReturn = false;
        size_t returnPos = line.find("return");
        if (returnPos != std::string::npos) {
            // Make sure it's not part of a word (e.g., "returnValue")
            if (returnPos == 0 || !std::isalnum(line[returnPos - 1])) {
                size_t afterReturn = returnPos + 6;
                if (afterReturn >= line.length() || !std::isalnum(line[afterReturn])) {
                    isReturn = true;
                }
            }
        }
        
        if (isReturn) {
            // Replace return statement with MPI_Finalize + return
            std::string indent;
            for (char c : line) {
                if (c == ' ' || c == '\t') indent += c;
                else break;
            }
            wrappedBody += indent + "MPI_Finalize();\n";
            wrappedBody += line + "\n";
        } else if (hasOutput && !alreadyWrapped) {
            // Wrap output in rank 0 check
            std::string indent;
            for (char c : line) {
                if (c == ' ' || c == '\t') indent += c;
                else break;
            }
            wrappedBody += indent + "if (rank == 0) {\n";
            wrappedBody += indent + "    " + line.substr(indent.length()) + "\n";
            wrappedBody += indent + "}\n";
        } else {
            wrappedBody += line + "\n";
        }
    }
    
    // Replace variable names that conflict with MPI reserved names
    for (const auto& pair : variableNameMap) {
        if (pair.first != pair.second) {
            wrappedBody = substituteVariableNames(wrappedBody, variableNameMap);
            break; // substituteVariableNames handles all at once
        }
    }
    
    return wrappedBody;
}

std::string HybridParallelizer::generateHybridMPIOpenMPCode() {
    auto parallelGroups = getParallelizableGroups();
    
    std::stringstream mpiCode;
    
    // Headers - use original includes and add required MPI/OpenMP headers
    mpiCode << "#include <mpi.h>\n";
    mpiCode << "#include <omp.h>\n";
    if (!originalIncludes.empty()) {
        // PHASE 2 FIX: Extract only #include statements, skip function definitions
        std::string cleanedIncludes = extractIncludesOnly(originalIncludes);
        mpiCode << cleanedIncludes;
        if (!cleanedIncludes.empty() && cleanedIncludes.back() != '\n') {
            mpiCode << "\n";
        }
    } else {
        // Fallback headers if no original includes provided
        mpiCode << "#include <stdio.h>\n";
    }
    
    // NEW: Add typedefs from source context
    if (!sourceContext.typedefs.empty()) {
        mpiCode << "\n// Type definitions from original source\n";
        for (const auto& typedefInfo : sourceContext.typedefs) {
            std::string typedef_def = typedefInfo.definition;
            // PHASE 2 FIX: Ensure typedef ends with semicolon
            if (!typedef_def.empty() && typedef_def.back() != ';') {
                typedef_def += ";";
            }
            mpiCode << typedef_def << "\n";
        }
    }
    
    // Fallback headers if no original includes provided (continued)
    if (originalIncludes.empty()) {
        mpiCode << "#include <iostream>\n";
        mpiCode << "#include <vector>\n";
        mpiCode << "#include <cmath>\n";
        mpiCode << "#include <time.h>\n";
        mpiCode << "#include <chrono>\n";
        mpiCode << "#include <string>\n";
    }
    mpiCode << "\n";
    
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
    
    // First, include all functions directly called in main
    for (const auto& call : functionCalls) {
        outputFunctions.insert(call.functionName);
    }
    
    // Then, include all other analyzed functions (like utility functions used as parameters)
    for (const auto& funcPair : functionInfo) {
        const std::string& funcName = funcPair.first;
        if (funcName != "main" && !funcName.empty()) { // Skip main function and empty names
            outputFunctions.insert(funcName);
        }
    }
    
    // PHASE 2 FIX: Generate enhanced functions (not duplicates) using complete source  
    for (const std::string& funcName : outputFunctions) {
        if (functionInfo.count(funcName)) {
            const FunctionInfo& info = functionInfo.at(funcName);
            
            // PHASE 2: Use complete function source if available, otherwise build from parts
            if (!info.complete_function_source.empty()) {
                // Enhanced version with loop parallelization if enabled
                if (enableLoopParallelization && info.has_parallelizable_loops) {
                    mpiCode << "// Enhanced function with OpenMP pragmas: " << info.name << "\n";
                    
                    // Extract function signature and generate body with pragmas
                    if (!info.function_signature.empty()) {
                        mpiCode << info.function_signature << " ";
                        mpiCode << generateParallelizedFunctionBody(info);
                    } else {
                        // Fallback: use complete source but mark as enhanced
                        mpiCode << "// Original function (pragma enhancement failed): " << info.name << "\n";
                        mpiCode << info.complete_function_source;
                    }
                } else {
                    // Use original complete source
                    mpiCode << "// Original function: " << info.name << "\n";
                    mpiCode << info.complete_function_source;
                }
            } else {
                // Fallback: build function from parts (legacy mode)
                mpiCode << "// Reconstructed function: " << info.name << "\n";
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
                    if (i < info.parameter_names.size() && !info.parameter_names[i].empty()) {
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
            }
            mpiCode << "\n\n";
        } else {
            std::string returnType = "int";
            if (functionAnalysis.count(funcName)) {
                returnType = TypeMapper::normalizeType(functionAnalysis.at(funcName).returnType);
            }
            
            mpiCode << "// Function definition not found for: " << funcName << "\n";
            mpiCode << returnType << " " << funcName << "() {\n";
            mpiCode << "    printf(\"Executing " << funcName << "\\n\");\n";
            if (returnType != "void") {
                mpiCode << "    return " << TypeMapper::getDefaultValue(returnType) << ";\n";
            }
            mpiCode << "}\n\n";
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
    
    // Check if we can preserve the original main body structure
    if (!mainFunctionBody.empty() && !functionCalls.empty() && 
        functionCalls[0].statementStartOffset > 0) {
        // NEW: Use preserved main body structure
        mpiCode << "    // === Original main() structure preserved with MPI parallelization ===\n\n";
        mpiCode << generatePreservedMainBody();
        mpiCode << "}\n";
        
        return mpiCode.str();
    }
    
    // Fallback: Original reconstruction approach (when offset info not available)
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
    
    // Generate local variables in original declaration order
    mpiCode << "    // Local variables from original main function (ordered by source)\n";
    std::map<std::string, std::string> variableNameMap; // Track renamed variables
    
    // Convert map to vector and sort by declaration order
    std::vector<std::pair<std::string, LocalVariable>> orderedVariables;
    for (const auto& pair : localVariables) {
        orderedVariables.push_back(pair);
    }
    std::sort(orderedVariables.begin(), orderedVariables.end(),
              [](const auto& a, const auto& b) {
                  return a.second.declarationOrder < b.second.declarationOrder;
              });
    
    for (const auto& pair : orderedVariables) {
        const LocalVariable& localVar = pair.second;
        std::string resolvedName = resolveVariableNameConflict(localVar.name);
        
        // Use original initialization value if available, otherwise use default
        if (!localVar.initializationValue.empty()) {
            // Substitute any variable references within the initialization value
            std::string initValue = substituteVariableNames(localVar.initializationValue, variableNameMap);
            
            // PHASE 3 FIX: Improved constructor vs assignment detection
            // Check if this is constructor syntax by looking for specific patterns
            bool isConstructorSyntax = false;
            
            // Pattern 1: Starts with parentheses (args) - constructor call  
            if (initValue.front() == '(' && initValue.back() == ')') {
                isConstructorSyntax = true;
            }
            // Pattern 2: Initializer list {args} - constructor call
            else if (initValue.front() == '{' && initValue.back() == '}') {
                isConstructorSyntax = true;
            }
            // Pattern 3: Function call without assignment operator
            else if (initValue.find("(") != std::string::npos && initValue.find("=") == std::string::npos) {
                // Check if it looks like a function call that should be constructor
                if (localVar.type.find("std::") != std::string::npos || localVar.type.find("vector") != std::string::npos) {
                    isConstructorSyntax = true;
                }
            }
            
            if (isConstructorSyntax) {
                // Constructor syntax - remove outer parentheses if present
                std::string constructorArgs = initValue;
                if (constructorArgs.front() == '(' && constructorArgs.back() == ')') {
                    constructorArgs = constructorArgs.substr(1, constructorArgs.length() - 2);
                }
                mpiCode << "    " << localVar.type << " " << resolvedName << "(" << constructorArgs << ");\n";
            } else {
                // Regular assignment syntax
                mpiCode << "    " << localVar.type << " " << resolvedName << " = " << initValue << ";\n";
            }
        } else {
            // No explicit initialization - use default constructor
            mpiCode << "    " << localVar.type << " " << resolvedName << ";\n";
        }
        variableNameMap[localVar.name] = resolvedName; // Store the mapping
    }
    mpiCode << "\n";
    
    // Generate result variables
    for (int i = 0; i < functionCalls.size(); ++i) {
        if (functionCalls[i].hasReturnValue) {
            std::string returnType = TypeMapper::normalizeType(functionCalls[i].returnType);
            std::string defaultValue = TypeMapper::getDefaultValue(returnType);
            mpiCode << "    " << returnType << " result_" << i << " = " << defaultValue << ";\n";
        }
    }
    mpiCode << "\n";
    
    // Generate parallel execution logic
    int groupIndex = 0;
    for (const auto& group : parallelGroups) {
        // NEW: Check for MPI loops in this group
        bool hasMpiLoops = false;
        if (enableLoopParallelization) {
            for (int callIdx : group) {
                std::string funcName = functionCalls[callIdx].functionName;
                if (functionInfo.count(funcName)) {
                    const auto& info = functionInfo.at(funcName);
                    for (const auto& loop : info.loops) {
                        if (loop.is_mpi_parallelizable) {
                            hasMpiLoops = true;
                            break;
                        }
                    }
                }
                if (hasMpiLoops) break;
            }
        }

        if (hasMpiLoops) {
            mpiCode << "    // === Parallel group " << groupIndex << " (Contains MPI-parallelized loops) ===\n";
            mpiCode << "    // Executing functions sequentially on all ranks to allow full MPI utilization\n";
            
            for (int callIdx : group) {
                std::string funcName = functionCalls[callIdx].functionName;
                mpiCode << "    // Call " << funcName << "\n";
                
                if (functionCalls[callIdx].hasReturnValue) {
                    std::string originalCall = functionCalls[callIdx].callExpression;
                    std::string substitutedCall = substituteVariableNames(originalCall, variableNameMap);
                    mpiCode << "    result_" << callIdx << " = " << extractFunctionCall(substitutedCall) << ";\n";
                    
                    if (!functionCalls[callIdx].returnVariable.empty()) {
                        std::string resolvedReturnVar = resolveVariableNameConflict(functionCalls[callIdx].returnVariable);
                        mpiCode << "    " << resolvedReturnVar << " = result_" << callIdx << ";\n";
                    }
                } else {
                    std::string originalCall = functionCalls[callIdx].callExpression;
                    std::string substitutedCall = substituteVariableNames(originalCall, variableNameMap);
                    if (!substitutedCall.empty() && substitutedCall.back() == ';') {
                        substitutedCall.pop_back();
                    }
                    mpiCode << "    " << substitutedCall << ";\n";
                }
                
                // Add barrier to synchronize
                mpiCode << "    MPI_Barrier(MPI_COMM_WORLD);\n";
            }
            
            groupIndex++;
            continue;
        }

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
                std::string substitutedCall = substituteVariableNames(originalCall, variableNameMap);
                mpiCode << "        result_" << callIdx << " = " << extractFunctionCall(substitutedCall) << ";\n";
                if (!functionCalls[callIdx].returnVariable.empty()) {
                    std::string resolvedReturnVar = resolveVariableNameConflict(functionCalls[callIdx].returnVariable);
                    mpiCode << "        " << resolvedReturnVar << " = result_" << callIdx << ";\n";
                }
            } else {
                std::string originalCall = functionCalls[callIdx].callExpression;
                std::string substitutedCall = substituteVariableNames(originalCall, variableNameMap);
                if (!substitutedCall.empty() && substitutedCall.back() == ';') {
                    substitutedCall.pop_back();
                }
                mpiCode << "        " << substitutedCall << ";\n";
            }
            mpiCode << "    }\n";
        } else {
            // Robust parallel execution with dynamic process assignment
            mpiCode << "    // Dynamic process assignment to avoid deadlocks\n";
            mpiCode << "    int effective_processes = std::min(size, (int)" << group.size() << ");\n";
            
            // Calculate all process assignments upfront
            for (int i = 0; i < group.size(); ++i) {
                int callIdx = group[i];
                mpiCode << "    int assigned_rank_" << callIdx << " = " << i << " % effective_processes;\n";
            }
            
            for (int i = 0; i < group.size(); ++i) {
                int callIdx = group[i];
                mpiCode << "    if (rank == assigned_rank_" << callIdx << ") {\n";
                
                if (functionCalls[callIdx].hasReturnValue) {
                    std::string originalCall = functionCalls[callIdx].callExpression;
                    std::string substitutedCall = substituteVariableNames(originalCall, variableNameMap);
                    std::string funcCall = extractFunctionCall(substitutedCall);
                    mpiCode << "        result_" << callIdx << " = " << funcCall << ";\n";
                    
                    // Send result to rank 0 if this process is not rank 0 (non-blocking to avoid deadlock)
                    mpiCode << "        if (assigned_rank_" << callIdx << " != 0) {\n";
                    std::string mpiType = TypeMapper::getMPIDatatype(functionCalls[callIdx].returnType);
                    if (!mpiType.empty()) {
                        mpiCode << "            MPI_Request _send_req_" << callIdx << ";\n";
                        mpiCode << "            MPI_Isend(&result_" << callIdx 
                               << ", 1, " << mpiType << ", 0, " << callIdx << ", MPI_COMM_WORLD, &_send_req_" << callIdx << ");\n";
                        mpiCode << "            MPI_Wait(&_send_req_" << callIdx << ", MPI_STATUS_IGNORE);\n";
                    } else {
                        mpiCode << "            // Skipping MPI_Send for unsupported type: " << functionCalls[callIdx].returnType << "\n";
                    }
                    mpiCode << "        }\n";
                } else {
                    std::string originalCall = functionCalls[callIdx].callExpression;
                    std::string substitutedCall = substituteVariableNames(originalCall, variableNameMap);
                    if (!substitutedCall.empty() && substitutedCall.back() == ';') {
                        substitutedCall.pop_back();
                    }
                    mpiCode << "        " << substitutedCall << ";\n";
                }
                mpiCode << "    }\n";
            }
            
            // Collect results in rank 0 using dynamic assignment
            mpiCode << "    if (rank == 0) {\n";
            // Use non-blocking receives to avoid deadlock
            mpiCode << "        std::vector<MPI_Request> _recv_requests;\n";
            for (int i = 0; i < group.size(); ++i) {
                int callIdx = group[i];
                if (functionCalls[callIdx].hasReturnValue) {
                    std::string mpiType = TypeMapper::getMPIDatatype(functionCalls[callIdx].returnType);
                    if (!mpiType.empty()) {
                        // Only receive if function is assigned to a different rank
                        mpiCode << "        if (assigned_rank_" << callIdx << " != 0) {\n";
                        mpiCode << "            MPI_Request _recv_req_" << callIdx << ";\n";
                        mpiCode << "            MPI_Irecv(&result_" << callIdx 
                               << ", 1, " << mpiType << ", assigned_rank_" << callIdx << ", " << callIdx 
                               << ", MPI_COMM_WORLD, &_recv_req_" << callIdx << ");\n";
                        mpiCode << "            _recv_requests.push_back(_recv_req_" << callIdx << ");\n";
                        mpiCode << "        }\n";
                    } else {
                        mpiCode << "        // Skipping MPI_Recv for unsupported type: " << functionCalls[callIdx].returnType << "\n";
                    }
                }
            }
            // Wait for all receives to complete
            mpiCode << "        if (!_recv_requests.empty()) {\n";
            mpiCode << "            MPI_Waitall(_recv_requests.size(), _recv_requests.data(), MPI_STATUSES_IGNORE);\n";
            mpiCode << "        }\n";
            
            for (int i = 0; i < group.size(); ++i) {
                int callIdx = group[i];
                if (functionCalls[callIdx].hasReturnValue && !functionCalls[callIdx].returnVariable.empty()) {
                    std::string resolvedReturnVar = resolveVariableNameConflict(functionCalls[callIdx].returnVariable);
                    mpiCode << "        " << resolvedReturnVar << " = result_" << callIdx << ";\n";
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
                std::string resolvedReturnVar = resolveVariableNameConflict(functionCalls[callIdx].returnVariable);
                variablesToBroadcast.insert(resolvedReturnVar);
            }
        }
        
        for (const std::string& varName : variablesToBroadcast) {
            // Find the original variable name to get its type
            std::string originalVarName = varName;
            for (const auto& mapping : variableNameMap) {
                if (mapping.second == varName) {
                    originalVarName = mapping.first;
                    break;
                }
            }
            
            if (localVariables.count(originalVarName)) {
                std::string mpiType = TypeMapper::getMPIDatatype(localVariables.at(originalVarName).type);
                if (!mpiType.empty()) {
                    mpiCode << "    MPI_Bcast(&" << varName << ", 1, " << mpiType << ", 0, MPI_COMM_WORLD);\n";
                } else {
                    mpiCode << "    // Skipping MPI_Bcast for unsupported type: " << localVariables.at(originalVarName).type << "\n";
                }
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
    
    // NOTE: Variable result printing removed to avoid issues with non-printable types
    // Users can add their own output statements as needed
    
    mpiCode << "        std::cout << \"\\n=== Enhanced Hybrid MPI/OpenMP Execution Complete ===\" << std::endl;\n";
    mpiCode << "    }\n\n";

    mpiCode << "    MPI_Finalize();\n";
    mpiCode << "    return 0;\n";
    mpiCode << "}\n";
    
    return mpiCode.str();
}

std::string HybridParallelizer::resolveVariableNameConflict(const std::string& originalName) const {
    // List of MPI reserved variable names
    static const std::set<std::string> mpiReservedNames = {
        "rank", "size", "provided", "argc", "argv", "status", "request",
        "comm", "tag", "source", "dest", "count", "datatype"
    };
    
    // Check if the variable name conflicts with MPI reserved names
    if (mpiReservedNames.count(originalName)) {
        return "user_" + originalName;  // Prefix with "user_" to avoid conflict
    }
    
    return originalName;  // No conflict, return original name
}

std::string HybridParallelizer::substituteVariableNames(const std::string& originalCall, const std::map<std::string, std::string>& variableNameMap) const {
    std::string result = originalCall;
    
    // Replace each variable name with its renamed version
    for (const auto& pair : variableNameMap) {
        const std::string& oldName = pair.first;
        const std::string& newName = pair.second;
        
        if (oldName != newName) {  // Only substitute if name actually changed
            // Use word boundary matching to avoid partial replacements
            // Replace standalone occurrences of the variable name
            size_t pos = 0;
            while ((pos = result.find(oldName, pos)) != std::string::npos) {
                // Check if this is a complete word (not part of another identifier)
                bool isWordBoundary = true;
                if (pos > 0 && (std::isalnum(result[pos - 1]) || result[pos - 1] == '_')) {
                    isWordBoundary = false;
                }
                if (pos + oldName.length() < result.length() && 
                    (std::isalnum(result[pos + oldName.length()]) || result[pos + oldName.length()] == '_')) {
                    isWordBoundary = false;
                }
                
                if (isWordBoundary) {
                    result.replace(pos, oldName.length(), newName);
                    pos += newName.length();
                } else {
                    pos += 1;
                }
            }
        }
    }
    
    return result;
}