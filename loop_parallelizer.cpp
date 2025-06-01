#include <clang-c/Index.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <regex>

struct LoopInfo {
    std::string type;                    // "for", "while", "do-while"
    std::string source_code;             // Original loop source
    std::string loop_variable;           // Loop iterator variable
    std::vector<std::string> read_vars;  // Variables read in loop
    std::vector<std::string> write_vars; // Variables written in loop
    std::vector<std::string> reduction_vars; // Reduction variables
    bool has_dependencies;               // Loop-carried dependencies
    bool has_function_calls;             // Contains function calls
    bool has_io_operations;              // Contains I/O operations
    bool is_nested;                      // Is a nested loop
    bool parallelizable;                 // Can be parallelized
    std::string schedule_type;           // Recommended schedule
    std::string analysis_notes;          // Analysis details
    unsigned start_line, end_line;       // Source location
};

class LibClangLoopAnalyzer {
private:
    CXIndex index;
    CXTranslationUnit translation_unit;
    std::vector<LoopInfo> loops;
    std::string source_file;
    std::vector<std::string> source_lines;
    bool inside_loop = false;  // Track if we're inside a loop
    
public:
    LibClangLoopAnalyzer() {
        index = clang_createIndex(0, 0);
    }
    
    ~LibClangLoopAnalyzer() {
        if (translation_unit) {
            clang_disposeTranslationUnit(translation_unit);
        }
        clang_disposeIndex(index);
    }
    
    bool parseFile(const std::string& filename) {
        source_file = filename;
        loadSourceLines();
        
        // Parse the file
        translation_unit = clang_parseTranslationUnit(
            index,
            filename.c_str(),
            nullptr, 0,          // command line args
            nullptr, 0,          // unsaved files
            CXTranslationUnit_None
        );
        
        if (!translation_unit) {
            std::cerr << "Error: Failed to parse " << filename << std::endl;
            return false;
        }
        
        std::cout << "Successfully parsed: " << filename << std::endl;
        return true;
    }
    
    void analyzeLoops() {
        CXCursor root_cursor = clang_getTranslationUnitCursor(translation_unit);
        clang_visitChildren(root_cursor, visitNode, this);
        
        // Post-process analysis
        for (auto& loop : loops) {
            performDependencyAnalysis(loop);
            generateRecommendations(loop);
        }
    }
    
    void printAnalysis() {
        std::cout << "\n=== LIBCLANG LOOP ANALYSIS RESULTS ===\n";
        std::cout << "Found " << loops.size() << " loops\n\n";
        
        for (size_t i = 0; i < loops.size(); i++) {
            const auto& loop = loops[i];
            std::cout << "Loop #" << (i + 1) << ":\n";
            std::cout << "Type: " << loop.type << "\n";
            std::cout << "Location: Lines " << loop.start_line << "-" << loop.end_line << "\n";
            std::cout << "Parallelizable: " << (loop.parallelizable ? "YES" : "NO") << "\n";
            
            if (!loop.loop_variable.empty()) {
                std::cout << "Loop Variable: " << loop.loop_variable << "\n";
            }
            
            if (!loop.read_vars.empty()) {
                std::cout << "Variables Read: ";
                for (const auto& var : loop.read_vars) {
                    std::cout << var << " ";
                }
                std::cout << "\n";
            }
            
            if (!loop.write_vars.empty()) {
                std::cout << "Variables Written: ";
                for (const auto& var : loop.write_vars) {
                    std::cout << var << " ";
                }
                std::cout << "\n";
            }
            
            if (!loop.reduction_vars.empty()) {
                std::cout << "Reduction Variables: ";
                for (const auto& var : loop.reduction_vars) {
                    std::cout << var << " ";
                }
                std::cout << "\n";
            }
            
            std::cout << "Recommended Schedule: " << loop.schedule_type << "\n";
            std::cout << "Analysis Notes: " << loop.analysis_notes << "\n";
            std::cout << "Source Code:\n" << loop.source_code << "\n";
            std::cout << "----------------------------------------\n\n";
        }
    }
    
    void generateOpenMPVersion(const std::string& output_filename) {
        std::ifstream input(source_file);
        std::ofstream output(output_filename);
        
        if (!input.is_open() || !output.is_open()) {
            std::cerr << "Error: Cannot open files for OpenMP generation\n";
            return;
        }
        
        output << "// OpenMP Parallelized Version\n";
        output << "// Generated by LibClang Loop Analyzer\n\n";
        output << "#include <omp.h>\n";
        
        std::string line;
        unsigned line_number = 1;
        const LoopInfo* current_loop = nullptr;
        
        // Read entire file into memory first
        std::vector<std::string> file_lines;
        while (std::getline(input, line)) {
            file_lines.push_back(line);
        }
        input.close();
        
        // Process line by line
        for (line_number = 1; line_number <= file_lines.size(); line_number++) {
            bool wrote_line = false;
            
            // Check if we're starting a parallelizable loop
            for (const auto& loop : loops) {
                if (loop.parallelizable && line_number == loop.start_line) {
                    current_loop = &loop;
                    
                    // Write the pragma
                    output << generateOpenMPPragma(loop) << "\n";
                    
                    // Write all lines of this loop
                    for (unsigned i = loop.start_line; i <= loop.end_line && i <= file_lines.size(); i++) {
                        output << file_lines[i-1] << "\n";
                    }
                    
                    // Skip ahead to after this loop
                    line_number = loop.end_line;
                    wrote_line = true;
                    current_loop = nullptr;
                    break;
                }
            }
            
            // If we didn't write a parallelized loop, write the original line
            if (!wrote_line) {
                output << file_lines[line_number-1] << "\n";
            }
        }
        
        output.close();
        
        std::cout << "Generated OpenMP version: " << output_filename << std::endl;
    }
    
private:
    void loadSourceLines() {
        std::ifstream file(source_file);
        std::string line;
        while (std::getline(file, line)) {
            source_lines.push_back(line);
        }
        file.close();
    }
    
    static enum CXChildVisitResult visitNode(CXCursor cursor, CXCursor parent, CXClientData client_data) {
        LibClangLoopAnalyzer* analyzer = static_cast<LibClangLoopAnalyzer*>(client_data);
        
        CXCursorKind kind = clang_getCursorKind(cursor);
        
        // Check for different types of loops
        if (kind == CXCursor_ForStmt || kind == CXCursor_WhileStmt || kind == CXCursor_DoStmt) {
            // Only process if we're not already inside a loop
            if (!analyzer->inside_loop) {
                analyzer->inside_loop = true;
                
                if (kind == CXCursor_ForStmt) {
                    analyzer->processForLoop(cursor);
                } else if (kind == CXCursor_WhileStmt) {
                    analyzer->processWhileLoop(cursor);
                } else if (kind == CXCursor_DoStmt) {
                    analyzer->processDoWhileLoop(cursor);
                }
                
                analyzer->inside_loop = false;
                return CXChildVisit_Continue;  // Don't recurse into this loop
            }
        }
        
        return CXChildVisit_Recurse;
    }
    
    void processForLoop(CXCursor cursor) {
        LoopInfo loop;
        loop.type = "for";
        
        // Get source location
        CXSourceRange range = clang_getCursorExtent(cursor);
        CXSourceLocation start = clang_getRangeStart(range);
        CXSourceLocation end = clang_getRangeEnd(range);
        
        clang_getSpellingLocation(start, nullptr, &loop.start_line, nullptr, nullptr);
        clang_getSpellingLocation(end, nullptr, &loop.end_line, nullptr, nullptr);
        
        // Extract source code
        loop.source_code = extractSourceCode(loop.start_line, loop.end_line);
        
        // Analyze loop components
        analyzeLoopChildren(cursor, loop);
        
        loops.push_back(loop);
    }
    
    void processWhileLoop(CXCursor cursor) {
        LoopInfo loop;
        loop.type = "while";
        
        CXSourceRange range = clang_getCursorExtent(cursor);
        CXSourceLocation start = clang_getRangeStart(range);
        CXSourceLocation end = clang_getRangeEnd(range);
        
        clang_getSpellingLocation(start, nullptr, &loop.start_line, nullptr, nullptr);
        clang_getSpellingLocation(end, nullptr, &loop.end_line, nullptr, nullptr);
        
        loop.source_code = extractSourceCode(loop.start_line, loop.end_line);
        analyzeLoopChildren(cursor, loop);
        
        // While loops are generally harder to parallelize
        loop.parallelizable = false;
        loop.analysis_notes = "While loops require careful manual analysis for parallelization. ";
        
        loops.push_back(loop);
    }
    
    void processDoWhileLoop(CXCursor cursor) {
        LoopInfo loop;
        loop.type = "do-while";
        
        CXSourceRange range = clang_getCursorExtent(cursor);
        CXSourceLocation start = clang_getRangeStart(range);
        CXSourceLocation end = clang_getRangeEnd(range);
        
        clang_getSpellingLocation(start, nullptr, &loop.start_line, nullptr, nullptr);
        clang_getSpellingLocation(end, nullptr, &loop.end_line, nullptr, nullptr);
        
        loop.source_code = extractSourceCode(loop.start_line, loop.end_line);
        analyzeLoopChildren(cursor, loop);
        
        loop.parallelizable = false;
        loop.analysis_notes = "Do-while loops require careful manual analysis for parallelization. ";
        
        loops.push_back(loop);
    }
    
    void analyzeLoopChildren(CXCursor cursor, LoopInfo& loop) {
        // Initialize flags
        loop.has_function_calls = false;
        loop.has_io_operations = false;
        loop.is_nested = false;
        
        LoopChildrenAnalyzer analyzer_data;
        analyzer_data.loop = &loop;
        analyzer_data.parent_analyzer = this;
        analyzer_data.depth = 0;
        
        clang_visitChildren(cursor, analyzeLoopChild, &analyzer_data);
    }
    
    struct LoopChildrenAnalyzer {
        LoopInfo* loop;
        LibClangLoopAnalyzer* parent_analyzer;
        int depth;
    };
    
    static enum CXChildVisitResult analyzeLoopChild(CXCursor cursor, CXCursor parent, CXClientData client_data) {
        LoopChildrenAnalyzer* analyzer = static_cast<LoopChildrenAnalyzer*>(client_data);
        CXCursorKind kind = clang_getCursorKind(cursor);
        
        // Check for nested loops - mark as nested but don't analyze the inner loop separately
        if ((kind == CXCursor_ForStmt || kind == CXCursor_WhileStmt || kind == CXCursor_DoStmt) && analyzer->depth > 0) {
            analyzer->loop->is_nested = true;
            // Still recurse to analyze variables in the nested loop
        }
        
        // Check for variable references
        if (kind == CXCursor_DeclRefExpr) {
            CXString spelling = clang_getCursorSpelling(cursor);
            std::string var_name = clang_getCString(spelling);
            clang_disposeString(spelling);
            
            // Determine if it's a read or write based on parent context
            CXCursorKind parent_kind = clang_getCursorKind(parent);
            if (parent_kind == CXCursor_BinaryOperator) {
                // Simple heuristic: if it's on LHS of assignment, it's a write
                analyzer->loop->write_vars.push_back(var_name);
            } else {
                analyzer->loop->read_vars.push_back(var_name);
            }
        }
        
        // Check for I/O operations
        if (kind == CXCursor_CallExpr) {
            analyzer->loop->has_function_calls = true;
            
            // Get the function name
            CXString spelling = clang_getCursorSpelling(cursor);
            std::string func_name = clang_getCString(spelling);
            clang_disposeString(spelling);
            
            // Check for I/O operations
            if (func_name.find("printf") != std::string::npos ||
                func_name.find("scanf") != std::string::npos ||
                func_name.find("cout") != std::string::npos ||
                func_name.find("cin") != std::string::npos ||
                func_name.find("print") != std::string::npos) {
                analyzer->loop->has_io_operations = true;
            }
        }
        
        // Check for compound assignment operators (reduction patterns)
        if (kind == CXCursor_CompoundAssignOperator) {
            // Extract the variable being assigned to
            clang_visitChildren(cursor, [](CXCursor child, CXCursor parent, CXClientData data) -> CXChildVisitResult {
                if (clang_getCursorKind(child) == CXCursor_DeclRefExpr) {
                    LoopChildrenAnalyzer* analyzer = static_cast<LoopChildrenAnalyzer*>(data);
                    CXString spelling = clang_getCursorSpelling(child);
                    std::string var_name = clang_getCString(spelling);
                    clang_disposeString(spelling);
                    analyzer->loop->reduction_vars.push_back(var_name);
                    return CXChildVisit_Break;
                }
                return CXChildVisit_Continue;
            }, analyzer);
        }
        
        // Extract loop variable for for-loops (only at depth 0)
        if (analyzer->loop->type == "for" && kind == CXCursor_VarDecl && analyzer->depth == 0) {
            CXString spelling = clang_getCursorSpelling(cursor);
            std::string var_name = clang_getCString(spelling);
            clang_disposeString(spelling);
            
            if (analyzer->loop->loop_variable.empty()) {
                analyzer->loop->loop_variable = var_name;
            }
        }
        
        analyzer->depth++;
        return CXChildVisit_Recurse;
    }
    
    std::string extractSourceCode(unsigned start_line, unsigned end_line) {
        std::stringstream code;
        
        if (start_line > 0 && end_line <= source_lines.size()) {
            for (unsigned i = start_line - 1; i < end_line; i++) {
                code << source_lines[i];
                if (i < end_line - 1) code << "\n";
            }
        }
        
        return code.str();
    }
    
    void performDependencyAnalysis(LoopInfo& loop) {
        // Remove duplicates
        std::sort(loop.read_vars.begin(), loop.read_vars.end());
        loop.read_vars.erase(std::unique(loop.read_vars.begin(), loop.read_vars.end()), loop.read_vars.end());
        
        std::sort(loop.write_vars.begin(), loop.write_vars.end());
        loop.write_vars.erase(std::unique(loop.write_vars.begin(), loop.write_vars.end()), loop.write_vars.end());
        
        std::sort(loop.reduction_vars.begin(), loop.reduction_vars.end());
        loop.reduction_vars.erase(std::unique(loop.reduction_vars.begin(), loop.reduction_vars.end()), loop.reduction_vars.end());
        
        // Initialize as no dependencies
        loop.has_dependencies = false;
        
        // Check for loop-carried dependencies in source code
        std::string code = loop.source_code;
        
        // Simple I/O detection in source code - be more specific
        bool has_cout = (code.find("cout") != std::string::npos && code.find("<<") != std::string::npos);
        bool has_cin = (code.find("cin") != std::string::npos && code.find(">>") != std::string::npos);
        bool has_printf = (code.find("printf") != std::string::npos);
        bool has_scanf = (code.find("scanf") != std::string::npos);
        
        if (has_cout || has_cin || has_printf || has_scanf) {
            loop.has_io_operations = true;
        }
        
        // Simple reduction detection in source code
        if (code.find("+=") != std::string::npos) {
            // Extract variable name before +=
            size_t pos = code.find("+=");
            if (pos != std::string::npos) {
                // Look backwards to find the variable name
                std::string before = code.substr(0, pos);
                size_t end = before.find_last_not_of(" \t\n");
                if (end != std::string::npos) {
                    // Find the start of the variable name (not array access)
                    size_t bracket_pos = before.rfind('[', end);
                    size_t var_start = 0;
                    
                    if (bracket_pos != std::string::npos && bracket_pos < end) {
                        // This is an array access, find the array name
                        size_t array_end = bracket_pos - 1;
                        while (array_end > 0 && std::isspace(before[array_end])) array_end--;
                        
                        size_t array_start = array_end;
                        while (array_start > 0 && (std::isalnum(before[array_start-1]) || before[array_start-1] == '_')) {
                            array_start--;
                        }
                        
                        if (array_start <= array_end) {
                            std::string var_name = before.substr(array_start, array_end - array_start + 1);
                            if (!var_name.empty()) {
                                loop.reduction_vars.push_back(var_name);
                            }
                        }
                    } else {
                        // Regular variable, not array access
                        size_t start = before.find_last_of(" \t\n(=", end);
                        if (start == std::string::npos) start = 0;
                        else start++;
                        std::string var_name = before.substr(start, end - start + 1);
                        // Clean up the variable name
                        var_name.erase(0, var_name.find_first_not_of(" \t\n"));
                        var_name.erase(var_name.find_last_not_of(" \t\n") + 1);
                        if (!var_name.empty() && var_name.find_first_of("0123456789") != 0) {
                            loop.reduction_vars.push_back(var_name);
                        }
                    }
                }
            }
        }
        
        // Similar for *= operations  
        if (code.find("*=") != std::string::npos) {
            size_t pos = code.find("*=");
            if (pos != std::string::npos) {
                std::string before = code.substr(0, pos);
                size_t end = before.find_last_not_of(" \t\n");
                if (end != std::string::npos) {
                    size_t start = before.find_last_of(" \t\n(=", end);
                    if (start == std::string::npos) start = 0;
                    else start++;
                    std::string var_name = before.substr(start, end - start + 1);
                    var_name.erase(0, var_name.find_first_not_of(" \t\n"));
                    var_name.erase(var_name.find_last_not_of(" \t\n") + 1);
                    if (!var_name.empty() && var_name.find_first_of("0123456789") != 0) {
                        loop.reduction_vars.push_back(var_name);
                    }
                }
            }
        }
        
        // FIXED: More accurate loop-carried dependency detection
        // Look for array access patterns with loop variable offsets
        // We need to check if there's a pattern like: array[i] = ... array[i-k] ...
        // where the array[i-k] is on the RHS (being read)
        
        // First, let's identify the loop variable (typically 'i', 'j', 'k', etc.)
        std::string loop_var = loop.loop_variable;
        if (loop_var.empty()) {
            // Try to extract it from the for loop syntax
            std::regex for_pattern(R"(for\s*\(\s*\w+\s+(\w+)\s*=)");
            std::smatch match;
            if (std::regex_search(code, match, for_pattern)) {
                loop_var = match[1];
            } else {
                // Default to 'i' if we can't find it
                loop_var = "i";
            }
        }
        
        // Now look for array access patterns with offset indices
        // Pattern: variable[loop_var +/- offset] on the right side of assignment
        std::regex dependency_pattern(R"((\w+)\s*\[\s*)" + loop_var + R"(\s*[-+]\s*\d+\s*\])");
        std::smatch matches;
        std::string temp_code = code;
        
        while (std::regex_search(temp_code, matches, dependency_pattern)) {
            std::string full_match = matches[0];
            std::string array_name = matches[1];
            
            // Check if this pattern appears on the RHS of an assignment
            size_t match_pos = temp_code.find(full_match);
            size_t assign_pos = temp_code.rfind("=", match_pos);
            
            if (assign_pos != std::string::npos && assign_pos < match_pos) {
                // Found assignment before the array access
                // Check if there's another = between them (which would mean it's not RHS)
                size_t next_assign = temp_code.find("=", assign_pos + 1);
                if (next_assign == std::string::npos || next_assign > match_pos) {
                    // This is on the RHS - we have a dependency
                    // But we need to check if the same array is being written to
                    std::regex write_pattern(array_name + R"(\s*\[\s*)" + loop_var + R"(\s*\]\s*=)");
                    if (std::regex_search(code, write_pattern)) {
                        loop.has_dependencies = true;
                        break;
                    }
                }
            }
            
            temp_code = matches.suffix();
        }
        
        // Also check for the simpler pattern where we explicitly see a[i] = ... a[i-1] ...
        // This is a more direct check
        if (!loop.has_dependencies) {
            // Look for patterns like "a[i] = a[i-1]" or "a[i] = ... + a[i-1] + ..."
            std::regex simple_dep_pattern(R"((\w+)\s*\[\s*(\w+)\s*\]\s*=.*\1\s*\[\s*\2\s*[-+]\s*\d+\s*\])");
            if (std::regex_search(code, simple_dep_pattern)) {
                loop.has_dependencies = true;
            }
        }
        
        // Debug output
        std::cout << "DEBUG: Loop analysis for lines " << loop.start_line << "-" << loop.end_line << std::endl;
        std::cout << "  - Loop variable: " << loop_var << std::endl;
        std::cout << "  - Has I/O: " << loop.has_io_operations << std::endl;
        std::cout << "  - Has dependencies: " << loop.has_dependencies << std::endl;
        std::cout << "  - Reduction vars: " << loop.reduction_vars.size() << std::endl;
        std::cout << "  - Function calls: " << loop.has_function_calls << std::endl;
    }
    
    void generateRecommendations(LoopInfo& loop) {
        // Generate analysis notes
        std::stringstream notes;
        
        // Start with assumption that for loops can be parallelized
        if (loop.type == "for") {
            loop.parallelizable = true;
        } else {
            loop.parallelizable = false;
            notes << "Only for-loops are automatically parallelizable. ";
        }
        
        // Check disqualifying conditions
        if (loop.has_io_operations) {
            notes << "Contains I/O operations - not parallelizable. ";
            loop.parallelizable = false;
        }
        
        if (loop.has_dependencies) {
            notes << "Has loop-carried dependencies - not parallelizable. ";
            loop.parallelizable = false;
        }
        
        // Check for reduction patterns (these can be parallelized)
        if (!loop.reduction_vars.empty() && loop.type == "for") {
            notes << "Contains reduction operations - parallelizable with reduction clause. ";
            loop.parallelizable = true;  // Override dependency check for reductions
        }
        
        if (loop.has_function_calls && loop.parallelizable) {
            notes << "Contains function calls - verify no side effects. ";
        }
        
        // For nested loops, we typically only parallelize the outer loop
        if (loop.is_nested) {
            notes << "Nested loop detected - only outer loop will be parallelized. ";
            loop.schedule_type = "static";  // Use static for better cache locality in nested loops
        } else if (loop.has_function_calls) {
            loop.schedule_type = "dynamic";
        } else {
            loop.schedule_type = "static";
        }
        
        if (loop.parallelizable) {
            notes << "PARALLELIZABLE - OpenMP pragma will be added. ";
        } else {
            notes << "NOT PARALLELIZABLE - no pragma added. ";
        }
        
        loop.analysis_notes = notes.str();
        
        // Debug output
        std::cout << "DEBUG: Final decision for loop " << loop.start_line << ": " 
                  << (loop.parallelizable ? "PARALLELIZABLE" : "NOT PARALLELIZABLE") << std::endl;
    }
    
    std::string generateOpenMPPragma(const LoopInfo& loop) {
        std::stringstream pragma;
        pragma << "    #pragma omp parallel for";
        
        // Add reduction clause
        if (!loop.reduction_vars.empty()) {
            pragma << " reduction(";
            // Determine the reduction operation type
            if (loop.source_code.find("*=") != std::string::npos) {
                pragma << "*:";
            } else {
                pragma << "+:";  // Default to addition
            }
            
            for (size_t i = 0; i < loop.reduction_vars.size(); i++) {
                if (i > 0) pragma << ",";
                pragma << loop.reduction_vars[i];
            }
            pragma << ")";
        }
        
        // Add private clause for loop variable
        if (!loop.loop_variable.empty()) {
            pragma << " private(" << loop.loop_variable << ")";
        }
        
        // Add schedule clause
        pragma << " schedule(" << loop.schedule_type;
        if (loop.schedule_type == "dynamic") {
            pragma << ",1000";
        }
        pragma << ")";
        
        return pragma.str();
    }
};

// Helper function to create sample code
void createSampleCode() {
    std::ofstream sample("sample_loops.cpp");
    sample << R"(#include <iostream>
#include <vector>
#include <cmath>
using namespace std;

int main() {
    const int N = 1000;
    vector<double> a(N), b(N), c(N);
    double sum = 0.0, product = 1.0;
    
    // 1. Simple independent loop - parallelizable
    for(int i = 0; i < N; i++) {
        a[i] = i * 2.0;
        b[i] = sqrt(i + 1.0);
    }
    
    // 2. Reduction loop - parallelizable with reduction
    for(int i = 0; i < N; i++) {
        sum += a[i] * b[i];
    }
    
    // 3. Another reduction loop
    for(int i = 0; i < N; i++) {
        product *= (1.0 + a[i] * 0.001);
    }
    
    // 4. Loop with dependency - not parallelizable
    for(int i = 1; i < N; i++) {
        a[i] = a[i-1] + b[i] * 0.5;
    }
    
    // 5. Loop with I/O - not parallelizable
    for(int i = 0; i < 10; i++) {
        cout << "Value " << i << ": " << a[i] << endl;
    }
    
    // 6. Nested loop - parallelizable
    for(int i = 0; i < N; i++) {
        for(int j = 0; j < N; j++) {
            c[i] += a[i] * b[j];
        }
    }
    
    // 7. Loop with function call - check carefully
    for(int i = 0; i < N; i++) {
        c[i] = sin(a[i]) + cos(b[i]);
    }
    
    return 0;
}
)";
    sample.close();
    std::cout << "Created sample file: sample_loops.cpp\n";
}

int main(int argc, char* argv[]) {
    std::cout << "LibClang C++ Loop Analyzer and OpenMP Generator\n";
    std::cout << "===============================================\n\n";
    
    std::string input_file;
    
    if (argc > 1) {
        input_file = argv[1];
    } else {
        createSampleCode();
        input_file = "sample_loops.cpp";
        std::cout << "Usage: " << argv[0] << " <input_file.cpp>\n";
        std::cout << "Using sample file for demonstration.\n\n";
    }
    
    LibClangLoopAnalyzer analyzer;
    
    if (!analyzer.parseFile(input_file)) {
        return 1;
    }
    
    std::cout << "Analyzing loops...\n";
    analyzer.analyzeLoops();
    
    analyzer.printAnalysis();
    
    std::string output_file = input_file.substr(0, input_file.find_last_of('.')) + "_openmp.cpp";
    analyzer.generateOpenMPVersion(output_file);
    
    std::cout << "\nAnalysis complete!\n";
    std::cout << "Original file: " << input_file << "\n";
    std::cout << "OpenMP version: " << output_file << "\n";
    
    return 0;
}