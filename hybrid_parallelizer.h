#ifndef HYBRID_PARALLELIZER_H
#define HYBRID_PARALLELIZER_H

#include "data_structures.h"
#include "type_mapping.h"
#include <map>
#include <string>
#include <vector>

class HybridParallelizer {
private:
    std::vector<FunctionCall> functionCalls;
    std::map<std::string, FunctionAnalysis> functionAnalysis;
    std::vector<DependencyNode> dependencyGraph;
    std::map<std::string, LocalVariable> localVariables;
    std::map<std::string, FunctionInfo> functionInfo;
    std::vector<LoopInfo> mainLoops;
    std::set<std::string> globalVariables;
    bool enableLoopParallelization;
    std::string originalIncludes;
    SourceCodeContext sourceContext;  // NEW: Complete source context including typedefs
    std::string mainFunctionBody;     // NEW: Original main() body for preservation
    
    // Type mapping functions moved to TypeMapper utility class
    bool isTypePrintable(const std::string& cppType);
    std::string extractFunctionCall(const std::string& originalCall);
    std::string generateParallelizedFunctionBody(const FunctionInfo& info);
    std::string resolveVariableNameConflict(const std::string& originalName) const;
    std::string substituteVariableNames(const std::string& originalCall, const std::map<std::string, std::string>& variableNameMap) const;
    std::string extractIncludesOnly(const std::string& source);  // PHASE 2: Extract only include statements
    std::string generatePreservedMainBody();  // NEW: Generate main body preserving original structure
    
public:
    HybridParallelizer(const std::vector<FunctionCall>& calls, 
                      const std::map<std::string, FunctionAnalysis>& analysis,
                      const std::map<std::string, LocalVariable>& localVars,
                      const std::map<std::string, FunctionInfo>& funcInfo,
                      const std::vector<LoopInfo>& loops,
                      const std::set<std::string>& globals,
                      const std::string& includes = "",
                      bool enableLoops = true,
                      const SourceCodeContext& context = SourceCodeContext(),
                      const std::string& mainBody = "");  // NEW: Add main body parameter
    
    void buildDependencyGraph();
    std::vector<std::vector<int>> getParallelizableGroups() const;
    const std::vector<DependencyNode>& getDependencyGraph() const;
    const std::map<std::string, LocalVariable>& getLocalVariables() const;
    std::string generateHybridMPIOpenMPCode();
};

#endif // HYBRID_PARALLELIZER_H