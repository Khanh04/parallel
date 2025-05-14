// OpenMPI Code Parallelizer
// This program analyzes C++ code and generates parallelized versions using OpenMPI

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <regex>
#include <algorithm>
#include <filesystem>

// Forward declarations
class Variable;
class Loop;
class Function;
class CodeAnalyzer;
class MPICodeGenerator;

// Variable class to track variables and their access patterns
class Variable {
public:
    std::string name;
    std::set<int> readLines;
    std::set<int> writeLines;
    bool isArray;
    bool isGlobal;
    
    // Default constructor needed for std::map
    Variable() : name(""), isArray(false), isGlobal(false) {}
    
    Variable(const std::string& n, bool array = false, bool global = false) 
        : name(n), isArray(array), isGlobal(global) {}
    
    void addReadAccess(int line) {
        readLines.insert(line);
    }
    
    void addWriteAccess(int line) {
        writeLines.insert(line);
    }
};

// Loop class to represent loops in the code
class Loop {
public:
    int startLine;
    int endLine;
    std::string iterationVariable;
    std::string startValue;
    std::string endValue;
    std::string increment;
    std::string condition;
    bool isParallelizable;
    std::vector<std::string> accessedVariables;
    std::set<std::string> writtenVariables;
    std::vector<Loop> nestedLoops;
    
    Loop(int start, int end) : startLine(start), endLine(end), isParallelizable(false) {}
    
    bool hasLoopCarriedDependency(const std::map<std::string, Variable>& variables) {
        // Check for loop-carried dependencies by analyzing variable access patterns
        for (const auto& varName : accessedVariables) {
            if (variables.find(varName) != variables.end()) {
                const Variable& var = variables.at(varName);
                
                // Check if the variable is written to in the loop
                bool isWritten = false;
                for (int line : var.writeLines) {
                    if (line >= startLine && line <= endLine) {
                        isWritten = true;
                        break;
                    }
                }
                
                if (isWritten) {
                    // Check if the variable is also read in the loop
                    bool isRead = false;
                    for (int line : var.readLines) {
                        if (line >= startLine && line <= endLine) {
                            isRead = true;
                            break;
                        }
                    }
                    
                    // If the variable is both read and written in the loop, it may have a loop-carried dependency
                    if (isRead && var.isArray) {
                        // For arrays, we need more sophisticated analysis
                        // For now, conservatively mark as having dependency
                        return true;
                    } else if (isRead && !var.isArray && varName != iterationVariable) {
                        // For scalar variables that are not the loop iterator, mark as having dependency
                        return true;
                    }
                }
            }
        }
        
        return false;
    }
};

// Function class to represent functions in the code
class Function {
public:
    std::string name;
    int startLine;
    int endLine;
    std::vector<std::string> parameters;
    std::vector<Loop> loops;
    std::map<std::string, Variable> localVariables;
    std::string returnType; // Added: Function return type
    bool hasReturnValue;    // Added: Does this function return a value?
    
    // Default constructor needed for std::map
    Function() : name(""), startLine(-1), endLine(-1), returnType("void"), hasReturnValue(false) {}
    
    Function(const std::string& n, int start, const std::string& retType = "void") 
        : name(n), startLine(start), endLine(-1), returnType(retType), hasReturnValue(retType != "void") {}
};

// Code analyzer class to parse and analyze C++ code
class CodeAnalyzer {
private:
    std::vector<std::string> lines;
    std::map<std::string, Variable> globalVariables;
    std::map<std::string, Function> functions;
    int currentLine;
    
    // Helper methods for parsing
    std::string extractVariableName(const std::string& line) {
        std::regex varName("\\b(int|float|double|char|long|short|bool|auto|std::vector)\\s+(\\w+)(\\s*\\[.*\\])?\\s*(=.*)?;");
        std::smatch match;
        if (std::regex_search(line, match, varName) && match.size() > 2) {
            return match[2].str();
        }
        return "";
    }
    
    bool isArrayDeclaration(const std::string& line) {
        std::regex arrayDecl("\\b(int|float|double|char|long|short|bool)\\s+\\w+\\s*\\[.*\\]");
        return std::regex_search(line, arrayDecl);
    }
    
    bool isFunctionDeclaration(const std::string& line) {
        // Check for function declaration or definition
        std::regex funcDecl("\\b(void|int|float|double|char|long|short|bool|auto|std::string|std::vector)\\s+(\\w+)\\s*\\([^)]*\\)\\s*(const)?\\s*\\{?");
        return std::regex_search(line, funcDecl);
    }
    
    std::string extractFunctionName(const std::string& line) {
        std::regex funcName("\\b(void|int|float|double|char|long|short|bool|auto|std::string|std::vector)\\s+(\\w+)\\s*\\(");
        std::smatch match;
        if (std::regex_search(line, match, funcName) && match.size() > 2) {
            return match[2].str();
        }
        return "";
    }
    
    std::string extractFunctionReturnType(const std::string& line) {
        std::regex returnType("\\b(void|int|float|double|char|long|short|bool|auto|std::string|std::vector(\\s*<[^>]*>)?)\\s+\\w+\\s*\\(");
        std::smatch match;
        if (std::regex_search(line, match, returnType) && match.size() > 1) {
            return match[1].str();
        }
        return "void";
    }
    
    bool isForLoop(const std::string& line) {
        std::regex forLoop("\\s*for\\s*\\(.*\\)\\s*\\{?");
        return std::regex_search(line, forLoop);
    }
    
    bool isWhileLoop(const std::string& line) {
        std::regex whileLoop("\\s*while\\s*\\(.*\\)\\s*\\{?");
        return std::regex_search(line, whileLoop);
    }
    
    void extractForLoopInfo(const std::string& line, Loop& loop) {
        // Extract for loop details: initialization, condition, increment
        std::regex forDetails("for\\s*\\(([^;]*);([^;]*);([^\\)]*)\\)");
        std::smatch match;
        if (std::regex_search(line, match, forDetails) && match.size() > 3) {
            std::string init = match[1].str();
            loop.condition = match[2].str();
            loop.increment = match[3].str();
            
            // Extract iteration variable from initialization
            std::regex initVar("(\\w+)\\s*=\\s*(.*)");
            std::smatch initMatch;
            if (std::regex_search(init, initMatch, initVar) && initMatch.size() > 2) {
                loop.iterationVariable = initMatch[1].str();
                loop.startValue = initMatch[2].str();
                
                // Try to extract end value from condition
                std::regex endValPattern(loop.iterationVariable + "\\s*<\\s*(.*)");
                std::smatch endMatch;
                if (std::regex_search(loop.condition, endMatch, endValPattern) && endMatch.size() > 1) {
                    loop.endValue = endMatch[1].str();
                }
            }
        }
    }
    
    void extractWhileLoopInfo(const std::string& line, Loop& loop) {
        std::regex whileDetails("while\\s*\\((.*)\\)");
        std::smatch match;
        if (std::regex_search(line, match, whileDetails) && match.size() > 1) {
            loop.condition = match[1].str();
        }
    }
    
    std::vector<std::string> findVariableAccesses(const std::string& line) {
        std::vector<std::string> accesses;
        // Find variable names in the line (this is a simplified approach)
        std::regex varAccess("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b");
        std::sregex_iterator it(line.begin(), line.end(), varAccess);
        std::sregex_iterator end;
        
        std::set<std::string> keywords = {
            "if", "else", "for", "while", "do", "switch", "case", "break", 
            "continue", "return", "int", "float", "double", "char", "void", 
            "bool", "auto", "const", "static", "struct", "class", "namespace",
            "MPI_Init", "MPI_Finalize", "MPI_Comm_rank", "MPI_Comm_size", "MPI_Barrier",
            "MPI_Send", "MPI_Recv", "MPI_Bcast", "MPI_Reduce", "MPI_Allreduce", "MPI_Gather",
            "MPI_Allgather", "MPI_Scatter"
        };
        
        while (it != end) {
            std::string varName = (*it)[1].str();
            if (keywords.find(varName) == keywords.end()) {
                accesses.push_back(varName);
            }
            ++it;
        }
        
        return accesses;
    }
    
    bool isVariableWrite(const std::string& line, const std::string& varName) {
        // Check for assignment to the variable
        std::regex writePattern("\\b" + varName + "\\s*(\\[.*\\])?\\s*(=|\\+=|-=|\\*=|/=|%=|<<=|>>=|&=|\\^=|\\|=)");
        return std::regex_search(line, writePattern);
    }
    
    void findClosingBrace(int startLine, int& endLine) {
        int braceCount = 1;  // Start with 1 for the opening brace
        
        for (int i = startLine + 1; i < lines.size(); ++i) {
            for (char c : lines[i]) {
                if (c == '{') braceCount++;
                else if (c == '}') braceCount--;
                
                if (braceCount == 0) {
                    endLine = i;
                    return;
                }
            }
        }
    }

public:
    // Public parsing utility methods
    bool isVariableDeclaration(const std::string& line) const {
        // Basic check for variable declaration
        std::regex varDecl("\\b(int|float|double|char|long|short|bool|auto|std::vector)\\s+\\w+(\\s*\\[.*\\])?\\s*(=.*)?;");
        return std::regex_search(line, varDecl);
    }
    
    bool isReturnStatement(const std::string& line) const {
        // Check for return statement
        std::regex returnStmt("\\s*return\\s+.*;");
        return std::regex_search(line, returnStmt);
    }
    
    std::string extractReturnValue(const std::string& line) const {
        // Extract return value from "return xyz;"
        std::regex returnVal("return\\s+([^;]+);");
        std::smatch match;
        if (std::regex_search(line, match, returnVal) && match.size() > 1) {
            return match[1].str();
        }
        return "";
    }
    
    CodeAnalyzer(const std::string& filename) {
        std::ifstream file(filename);
        std::string line;
        
        if (!file.is_open()) {
            throw std::runtime_error("Unable to open file: " + filename);
        }
        
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        
        file.close();
    }
    
    void analyze() {
        Function* currentFunction = nullptr;
        
        for (currentLine = 0; currentLine < lines.size(); ++currentLine) {
            const std::string& line = lines[currentLine];
            
            // Check for variable declarations
            if (isVariableDeclaration(line)) {
                std::string varName = extractVariableName(line);
                bool isArray = isArrayDeclaration(line);
                
                if (!varName.empty()) {
                    if (currentFunction == nullptr) {
                        // Global variable
                        globalVariables[varName] = Variable(varName, isArray, true);
                    } else {
                        // Local variable
                        currentFunction->localVariables[varName] = Variable(varName, isArray, false);
                    }
                }
            }
            
            // Check for function declarations
            else if (isFunctionDeclaration(line)) {
                std::string funcName = extractFunctionName(line);
                std::string returnType = extractFunctionReturnType(line);
                
                if (!funcName.empty()) {
                    currentFunction = &functions[funcName];
                    currentFunction->name = funcName;
                    currentFunction->startLine = currentLine;
                    currentFunction->returnType = returnType;
                    currentFunction->hasReturnValue = (returnType != "void");
                    
                    // Find closing brace of the function
                    int endLine = -1;
                    findClosingBrace(currentLine, endLine);
                    if (endLine != -1) {
                        currentFunction->endLine = endLine;
                    }
                    
                    // Parse function parameters (simplified)
                    size_t openParen = line.find('(');
                    size_t closeParen = line.find(')', openParen);
                    if (openParen != std::string::npos && closeParen != std::string::npos) {
                        std::string params = line.substr(openParen + 1, closeParen - openParen - 1);
                        // Split by commas and extract parameter names
                        // This is a simplified approach and may miss complex declarations
                        std::regex paramRegex("(\\w+)\\s+(\\w+)");
                        std::sregex_iterator it(params.begin(), params.end(), paramRegex);
                        std::sregex_iterator end;
                        
                        while (it != end) {
                            std::string paramName = (*it)[2].str();
                            currentFunction->parameters.push_back(paramName);
                            // Add as local variable
                            currentFunction->localVariables[paramName] = Variable(paramName, false, false);
                            ++it;
                        }
                    }
                }
            }
            
            // Check for loops
            else if ((isForLoop(line) || isWhileLoop(line)) && currentFunction != nullptr) {
                Loop loop(currentLine, -1);
                
                if (isForLoop(line)) {
                    extractForLoopInfo(line, loop);
                } else {
                    extractWhileLoopInfo(line, loop);
                }
                
                // Find closing brace of the loop
                int endLine = -1;
                findClosingBrace(currentLine, endLine);
                if (endLine != -1) {
                    loop.endLine = endLine;
                    
                    // Analyze loop body for variable accesses
                    for (int i = currentLine + 1; i < endLine; ++i) {
                        auto accesses = findVariableAccesses(lines[i]);
                        for (const auto& varName : accesses) {
                            // Add to loop's accessed variables
                            if (std::find(loop.accessedVariables.begin(), loop.accessedVariables.end(), varName) == loop.accessedVariables.end()) {
                                loop.accessedVariables.push_back(varName);
                            }
                            
                            // Check if this is a write access
                            if (isVariableWrite(lines[i], varName)) {
                                loop.writtenVariables.insert(varName);
                                
                                // Update variable's write location
                                if (currentFunction->localVariables.find(varName) != currentFunction->localVariables.end()) {
                                    currentFunction->localVariables[varName].addWriteAccess(i);
                                } else if (globalVariables.find(varName) != globalVariables.end()) {
                                    globalVariables[varName].addWriteAccess(i);
                                }
                            } else {
                                // Update variable's read location
                                if (currentFunction->localVariables.find(varName) != currentFunction->localVariables.end()) {
                                    currentFunction->localVariables[varName].addReadAccess(i);
                                } else if (globalVariables.find(varName) != globalVariables.end()) {
                                    globalVariables[varName].addReadAccess(i);
                                }
                            }
                        }
                    }
                    
                    // Check if the loop is parallelizable
                    loop.isParallelizable = !loop.hasLoopCarriedDependency(currentFunction->localVariables) && 
                                         !loop.hasLoopCarriedDependency(globalVariables);
                    
                    // Add loop to current function
                    currentFunction->loops.push_back(loop);
                }
            }
            
            // Check for variable accesses outside loops
            else if (currentFunction != nullptr) {
                auto accesses = findVariableAccesses(line);
                for (const auto& varName : accesses) {
                    // Check if this is a write access
                    if (isVariableWrite(line, varName)) {
                        // Update variable's write location
                        if (currentFunction->localVariables.find(varName) != currentFunction->localVariables.end()) {
                            currentFunction->localVariables[varName].addWriteAccess(currentLine);
                        } else if (globalVariables.find(varName) != globalVariables.end()) {
                            globalVariables[varName].addWriteAccess(currentLine);
                        }
                    } else {
                        // Update variable's read location
                        if (currentFunction->localVariables.find(varName) != currentFunction->localVariables.end()) {
                            currentFunction->localVariables[varName].addReadAccess(currentLine);
                        } else if (globalVariables.find(varName) != globalVariables.end()) {
                            globalVariables[varName].addReadAccess(currentLine);
                        }
                    }
                }
            }
        }
    }
    
    const std::map<std::string, Function>& getFunctions() const {
        return functions;
    }
    
    const std::map<std::string, Variable>& getGlobalVariables() const {
        return globalVariables;
    }
    
    const std::vector<std::string>& getLines() const {
        return lines;
    }
};

// MPI Code Generator class to generate parallelized code
class MPICodeGenerator {
private:
    const CodeAnalyzer& analyzer;
    std::vector<std::string> result;
    bool mpiInitialized;
    
    void addMPIHeaders() {
        result.push_back("#include <mpi.h>");
        result.push_back("#include <algorithm> // For std::min");
        result.push_back("#include <iomanip>   // For std::setw in logging");
        result.push_back("");
        result.push_back("// Logging macro for MPI operations");
        result.push_back("#define MPI_LOG(msg) \\");
        result.push_back("    do { \\");
        result.push_back("        int _rank; \\");
        result.push_back("        MPI_Comm_rank(MPI_COMM_WORLD, &_rank); \\");
        result.push_back("        std::cout << \"[MPI Process \" << std::setw(3) << _rank << \"] \" << msg << std::endl; \\");
        result.push_back("    } while(0)");
        result.push_back("");
    }
    
    void addMPIInit() {
        result.push_back("int main(int argc, char** argv) {");
        result.push_back("    int rank, size;");
        result.push_back("    MPI_Init(&argc, &argv);");
        result.push_back("    MPI_Comm_rank(MPI_COMM_WORLD, &rank);");
        result.push_back("    MPI_Comm_size(MPI_COMM_WORLD, &size);");
        result.push_back("    MPI_LOG(\"Initialized MPI with \" << size << \" processes\");");
        mpiInitialized = true;
    }
    
    void addMPIFinalize() {
        result.push_back("    MPI_LOG(\"Finalizing MPI\");");
        result.push_back("    MPI_Finalize();");
        result.push_back("    return 0;");
        result.push_back("}");
    }
    
    void addMPIDatatype(const std::string& type) {
        // Add MPI datatype helper
        result.push_back("// Helper function to get MPI datatype for " + type);
        result.push_back("MPI_Datatype getMPIDatatype_" + type + "() {");
        
        if (type == "int") {
            result.push_back("    return MPI_INT;");
        } else if (type == "float") {
            result.push_back("    return MPI_FLOAT;");
        } else if (type == "double") {
            result.push_back("    return MPI_DOUBLE;");
        } else if (type == "char") {
            result.push_back("    return MPI_CHAR;");
        } else if (type == "long") {
            result.push_back("    return MPI_LONG;");
        } else if (type == "bool") {
            result.push_back("    return MPI_C_BOOL;");
        } else {
            // For complex types, return MPI_BYTE
            result.push_back("    // Warning: Using MPI_BYTE for possibly complex type");
            result.push_back("    return MPI_BYTE;");
        }
        
        result.push_back("}");
    }
    
    void processLoop(const Loop& loop, const Function& function) {
        const auto& lines = analyzer.getLines();
        
        // Get the original loop code
        std::vector<std::string> loopCode;
        for (int i = loop.startLine; i <= loop.endLine; ++i) {
            loopCode.push_back(lines[i]);
        }
        
        if (loop.isParallelizable && !loop.iterationVariable.empty() && !loop.endValue.empty()) {
            // This loop can be parallelized
            
            // First, add comments to indicate parallelization
            result.push_back("    // Parallelized loop using MPI");
            result.push_back("    // Original loop: for(" + loop.iterationVariable + " = " + loop.startValue + 
                           "; " + loop.condition + "; " + loop.increment + ")");
            
            // Analyze loop body for incremental updates (like d=d+2)
            std::map<std::string, std::string> incrementVars;
            for (int i = loop.startLine + 1; i < loop.endLine; ++i) {
                const std::string& line = lines[i];
                
                // Look for patterns like 'var = var + expr' or 'var += expr'
                for (const auto& varName : loop.writtenVariables) {
                    std::regex incrementPattern1("\\b" + varName + "\\s*=\\s*" + varName + "\\s*\\+\\s*([^;]+)");
                    std::regex incrementPattern2("\\b" + varName + "\\s*\\+=\\s*([^;]+)");
                    
                    std::smatch match;
                    if (std::regex_search(line, match, incrementPattern1) && match.size() > 1) {
                        incrementVars[varName] = match[1].str();
                    } else if (std::regex_search(line, match, incrementPattern2) && match.size() > 1) {
                        incrementVars[varName] = match[1].str();
                    }
                }
            }
            
            // Calculate the chunk size and starting point for each process
            result.push_back("    int loop_start = " + loop.startValue + ";");
            result.push_back("    int loop_end = " + loop.endValue + ";");
            result.push_back("    int total_iterations = loop_end - loop_start;");
            result.push_back("    int chunk_size = total_iterations / size;");
            result.push_back("    int remainder = total_iterations % size;");
            result.push_back("    int my_start = rank * chunk_size + loop_start + std::min(rank, remainder);");
            result.push_back("    int my_end = my_start + chunk_size + (rank < remainder ? 1 : 0);");
            result.push_back("    MPI_LOG(\"Processing loop iterations from \" << my_start << \" to \" << my_end << \" (total: \" << my_end - my_start << \")\");");
            
            // Handle special case for incrementing variables
            for (const auto& [varName, increment] : incrementVars) {
                result.push_back("    // Special handling for incrementing variable " + varName);
                result.push_back("    int original_" + varName + " = " + varName + ";");
                result.push_back("    " + varName + " = 0; // Reset to accumulate local changes");
            }
            
            // Add the modified loop for each process's chunk
            result.push_back("    // Each process executes its chunk of iterations");
            result.push_back("    for (int " + loop.iterationVariable + " = my_start; " + 
                           loop.iterationVariable + " < my_end; " + loop.iterationVariable + "++) {");
            
            // Add loop body without the outer loop and braces
            for (int i = loop.startLine + 1; i < loop.endLine; ++i) {
                result.push_back("        " + lines[i]);  // Add indentation (2 levels)
            }
            
            result.push_back("    }");
            
            // Sequential execution simulation for incremental variables
            for (const auto& [varName, increment] : incrementVars) {
                result.push_back("    // Gather and distribute incremental changes to " + varName);
                result.push_back("    int total_" + varName + " = 0;");
                result.push_back("    MPI_LOG(\"Reducing variable " + varName + " = \" << " + varName + " << \" from all processes\");");
                result.push_back("    MPI_Allreduce(&" + varName + ", &total_" + varName + ", 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);");
                
                // Reset the variable to its original value plus the total increment from all processes
                result.push_back("    " + varName + " = original_" + varName + " + total_" + varName + ";");
                
                // Add debugging
                result.push_back("    if (rank == 0) {");
                result.push_back("        MPI_LOG(\"Total " + varName + " after parallelization: \" << " + varName + ");");
                result.push_back("    }");
            }
            
            // Add barrier to ensure all processes are synchronized after the loop
            result.push_back("    MPI_LOG(\"Waiting at barrier after loop\");");
            result.push_back("    MPI_Barrier(MPI_COMM_WORLD);");
            
        } else {
            // This loop cannot be parallelized, add it as is
            result.push_back("    // Non-parallelizable loop");
            for (const auto& line : loopCode) {
                result.push_back(line);
            }
        }
    }
    
    void processFunction(const Function& function) {
        const auto& lines = analyzer.getLines();
        
        // Skip main function as we'll create our own
        if (function.name == "main") {
            return;
        }
        
        // Check if function has a return value
        bool hasReturnValue = function.hasReturnValue;
        std::string returnType = function.returnType;
        
        // For functions with return values, create a wrapper
        if (hasReturnValue) {
            // Create parallel wrapper function
            std::string wrapperName = function.name + "_mpi";
            
            // Generate wrapper function
            result.push_back(returnType + " " + wrapperName + "(");
            
            // Add parameters
            std::string params;
            for (size_t i = 0; i < function.parameters.size(); ++i) {
                params += "int " + function.parameters[i];
                if (i < function.parameters.size() - 1) {
                    params += ", ";
                }
            }
            result.push_back("    " + params + ") {");
            
            // Add MPI environment inside the wrapper
            result.push_back("    int rank, size;");
            result.push_back("    MPI_Comm_rank(MPI_COMM_WORLD, &rank);");
            result.push_back("    MPI_Comm_size(MPI_COMM_WORLD, &size);");
            result.push_back("    MPI_LOG(\"Entering " + wrapperName + "\");");
            
            // Process the function body
            int currentLine = function.startLine + 1;
            bool hasReturnStmt = false;
            std::string returnValue;
            
            while (currentLine < function.endLine) {
                bool processed = false;
                
                // Check if this line is a return statement
                if (analyzer.isReturnStatement(lines[currentLine])) {
                    returnValue = analyzer.extractReturnValue(lines[currentLine]);
                    hasReturnStmt = true;
                    // Don't process this line yet, will handle return values later
                    currentLine++;
                    continue;
                }
                
                // Check if this line is the start of a loop
                for (const auto& loop : function.loops) {
                    if (loop.startLine == currentLine) {
                        processLoop(loop, function);
                        currentLine = loop.endLine + 1;
                        processed = true;
                        break;
                    }
                }
                
                if (!processed) {
                    // Add line as is, with indentation
                    result.push_back("    " + lines[currentLine]);
                    currentLine++;
                }
            }
            
            // Handle return value if the function has one
            if (hasReturnStmt && !returnValue.empty()) {
                result.push_back("    // Gather return values from all processes");
                result.push_back("    " + returnType + " local_result = " + returnValue + ";");
                result.push_back("    " + returnType + " global_result;");
                result.push_back("    MPI_LOG(\"Process \" << rank << \" computed local result: \" << local_result);");
                
                // Choose appropriate MPI reduction operation based on return type
                std::string mpiOp = "MPI_SUM"; // default
                if (returnType == "int" || returnType == "float" || returnType == "double" || returnType == "long") {
                    result.push_back("    // Using MPI_Reduce with MPI_SUM for numeric return type");
                } else {
                    result.push_back("    // Warning: Using MPI_Reduce for possibly complex type");
                    result.push_back("    // May need manual adjustment for proper reduction");
                }
                
                // Add MPI reduction
                result.push_back("    MPI_LOG(\"Reducing results from all processes\");");
                result.push_back("    MPI_Reduce(&local_result, &global_result, 1, getMPIDatatype_" + returnType + "(), MPI_SUM, 0, MPI_COMM_WORLD);");
                
                // Broadcast result to all processes to ensure consistency
                result.push_back("    // Broadcast result to all processes");
                result.push_back("    MPI_Bcast(&global_result, 1, getMPIDatatype_" + returnType + "(), 0, MPI_COMM_WORLD);");
                result.push_back("    MPI_LOG(\"Final reduced result: \" << global_result);");
                result.push_back("    return global_result;");
            } else if (hasReturnStmt) {
                // No return value or void function with return
                result.push_back("    return;");
            }
            
            result.push_back("}");
            result.push_back("");
            
            // Now also add the original function for reference (commented out)
            result.push_back("// Original function (for reference)");
            for (int i = function.startLine; i <= function.endLine; ++i) {
                result.push_back("// " + lines[i]);
            }
            result.push_back("");
        } else {
            // For void functions, add the function as is but with parallelized loops
            // Add function signature and opening brace
            result.push_back(lines[function.startLine]);
            
            // Process function body
            int currentLine = function.startLine + 1;
            while (currentLine < function.endLine) {
                bool processed = false;
                
                // Check if this line is the start of a loop
                for (const auto& loop : function.loops) {
                    if (loop.startLine == currentLine) {
                        processLoop(loop, function);
                        currentLine = loop.endLine + 1;
                        processed = true;
                        break;
                    }
                }
                
                if (!processed) {
                    // Add line as is
                    result.push_back(lines[currentLine]);
                    currentLine++;
                }
            }
            
            // Add closing brace
            result.push_back(lines[function.endLine]);
        }
    }

public:
    MPICodeGenerator(const CodeAnalyzer& a) : analyzer(a), mpiInitialized(false) {}
    
    std::vector<std::string> generate() {
        // Create a new result vector instead of using the original code
        result.clear();
        
        // Add original includes and other preprocessor directives
        const auto& lines = analyzer.getLines();
        for (const auto& line : lines) {
            if (line.find("#include") != std::string::npos || 
                line.find("#define") != std::string::npos ||
                line.find("#pragma") != std::string::npos) {
                result.push_back(line);
            }
        }
        
        // Add MPI headers
        addMPIHeaders();
        
        // Add global variable declarations
        for (int i = 0; i < lines.size(); ++i) {
            const auto& line = lines[i];
            bool isGlobalVar = true;
            
            // Check if this line is inside any function
            for (const auto& funcPair : analyzer.getFunctions()) {
                const auto& function = funcPair.second;
                if (i >= function.startLine && i <= function.endLine) {
                    isGlobalVar = false;
                    break;
                }
            }
            
            // If it's a global variable declaration, add it
            if (isGlobalVar && analyzer.isVariableDeclaration(line)) {
                result.push_back(line);
            }
        }
        
        // Add MPI datatype helper functions for return types
        std::set<std::string> returnTypes;
        for (const auto& funcPair : analyzer.getFunctions()) {
            const auto& function = funcPair.second;
            if (function.hasReturnValue && function.returnType != "void") {
                returnTypes.insert(function.returnType);
            }
        }
        
        // Add MPI datatype helper for each return type
        for (const auto& type : returnTypes) {
            addMPIDatatype(type);
        }
        
        // Process each function (non-main first)
        for (const auto& funcPair : analyzer.getFunctions()) {
            const auto& name = funcPair.first;
            const auto& function = funcPair.second;
            
            if (name != "main") {
                // Process other functions
                processFunction(function);
            }
        }
        
        // Process main function last
        for (const auto& funcPair : analyzer.getFunctions()) {
            const auto& name = funcPair.first;
            const auto& function = funcPair.second;
            
            if (name == "main") {
                // Add commented original main function
                result.push_back("// Original main function replaced with MPI-enabled version");
                for (int i = function.startLine; i <= function.endLine; ++i) {
                    result.push_back("// " + lines[i]);  // Comment out original main
                }
                
                // Add our MPI main after the original code
                addMPIInit();
                
                // Copy all variable declarations from the original main
                result.push_back("    // Original variable declarations from main");
                for (int i = function.startLine + 1; i < function.endLine; ++i) {
                    if (analyzer.isVariableDeclaration(lines[i])) {
                        result.push_back("    " + lines[i]);
                    }
                }
                
                result.push_back("    // Begin parallelized code");
                
                // Process main function body, adding parallelized loops
                int currentLine = function.startLine + 1;
                while (currentLine < function.endLine) {
                    bool processed = false;
                    
                    // Skip variable declarations since we already added them
                    if (analyzer.isVariableDeclaration(lines[currentLine])) {
                        currentLine++;
                        continue;
                    }
                    
                    // Check if this line is the start of a loop
                    for (const auto& loop : function.loops) {
                        if (loop.startLine == currentLine) {
                            processLoop(loop, function);
                            currentLine = loop.endLine + 1;
                            processed = true;
                            break;
                        }
                    }
                    
                    if (!processed) {
                        // Check if this line calls a function with a return value
                        // This is a simplified approach; a more robust parser would be needed
                        for (const auto& funcPair : analyzer.getFunctions()) {
                            const auto& calledName = funcPair.first;
                            const auto& calledFunc = funcPair.second;
                            
                            if (calledName != "main" && calledFunc.hasReturnValue) {
                                std::regex functionCall("\\b" + calledName + "\\s*\\(");
                                if (std::regex_search(lines[currentLine], functionCall)) {
                                    // Found a call to a function with return value, replace with MPI version
                                    std::string originalLine = lines[currentLine];
                                    std::string modifiedLine = std::regex_replace(originalLine, 
                                                                                 std::regex("\\b" + calledName + "\\b"), 
                                                                                 calledName + "_mpi");
                                    
                                    result.push_back("    // Original function call: " + originalLine);
                                    result.push_back("    " + modifiedLine + " // Using MPI version");
                                    
                                    processed = true;
                                    break;
                                }
                            }
                        }
                        
                        if (!processed) {
                            // Add line as is, but with indentation
                            result.push_back("    " + lines[currentLine]);
                        }
                        currentLine++;
                    }
                }
                
                addMPIFinalize();
            }
        }
        
        return result;
    }
};

// Main function
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " input.cpp [output.cpp]" << std::endl;
        return 1;
    }
    
    std::string inputFile = argv[1];
    std::string outputFile = (argc > 2) ? argv[2] : inputFile + ".mpi.cpp";
    
    try {
        // Analyze the code
        CodeAnalyzer analyzer(inputFile);
        analyzer.analyze();
        
        // Generate parallelized code
        MPICodeGenerator generator(analyzer);
        auto parallelizedCode = generator.generate();
        
        // Write to output file
        std::ofstream outFile(outputFile);
        if (!outFile.is_open()) {
            std::cerr << "Error: Unable to open output file " << outputFile << std::endl;
            return 1;
        }
        
        for (const auto& line : parallelizedCode) {
            outFile << line << std::endl;
        }
        
        outFile.close();
        
        std::cout << "Parallelized code written to " << outputFile << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}