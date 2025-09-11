#ifndef HYBRID_PARALLELIZER_H
#define HYBRID_PARALLELIZER_H

#include "data_structures.h"
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
    
    std::string normalizeType(const std::string& cppType);
    std::string getMPIDatatype(const std::string& cppType);
    std::string getDefaultValue(const std::string& cppType);
    bool isTypePrintable(const std::string& cppType);
    std::string extractFunctionCall(const std::string& originalCall);
    std::string generateParallelizedFunctionBody(const FunctionInfo& info);
    
public:
    HybridParallelizer(const std::vector<FunctionCall>& calls, 
                      const std::map<std::string, FunctionAnalysis>& analysis,
                      const std::map<std::string, LocalVariable>& localVars,
                      const std::map<std::string, FunctionInfo>& funcInfo,
                      const std::vector<LoopInfo>& loops,
                      const std::set<std::string>& globals,
                      const std::string& includes = "",
                      bool enableLoops = true);
    
    void buildDependencyGraph();
    std::vector<std::vector<int>> getParallelizableGroups() const;
    const std::vector<DependencyNode>& getDependencyGraph() const;
    const std::map<std::string, LocalVariable>& getLocalVariables() const;
    std::string generateHybridMPIOpenMPCode();
};

#endif // HYBRID_PARALLELIZER_H