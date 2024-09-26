#include "functions.h"
#include "parser.h"
#include "dependency_graph.h"
#include "lexer.h"
#include "utils.h"
#include "common.h"
#include "file_operations.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <fstream>

using namespace std;

std::string Functions::currentFunction = "";

void Functions::print() {
    cout << "All function calls: " << endl;
    for (int i = 0; i < names.size(); i++) {
        if (functionCalls[i].empty()) {
            cout << names[i] << " not calling any function." << endl;
        } else {
            cout << names[i] << " calling: ";
            for (int j = 0; j < names.size(); j++) {
                if (std::find(functionCalls[i].begin(), functionCalls[i].end(), j) != functionCalls[i].end()) {
                    cout << names[j] << ", ";
                }
            }                    
            cout << endl;
        }
    }
    cout << endl;
}

int Functions::findFunction(const std::string &s) {
    for (int i = 0; i < names.size(); i++) {
        if (names[i] == s)
            return i;
    }
    return -1;
}

void Functions::addCall(std::string f1, std::string f2) {
    int i1 = findFunction(f1);
    int i2 = findFunction(f2);
    if (i1 == -1) {
        cout << "Function " << f1 << " not found!" << endl;
        exit(1);
    }
    if (i2 == -1) {
        cout << "Function " << f2 << " not found!" << endl;
        exit(1);
    }
    functionCalls[i1].push_back(i2);
}

void Functions::addCall(std::string f2) {
    addCall(currentFunction, f2);
}

void Functions::addFunction(std::string f) {
    names.push_back(f);
    std::vector<int> v;
    functionCalls.push_back(v);
}

bool parseFunctionCall(Functions &f, std::ofstream &fOut, std::string fileLine, const int maxStatementId) {
    std::istringstream ist{fileLine};
    Lexer* p_lexer = new Lexer{ist};
    std::string word = p_lexer->get_token_text();

    std::string sPushParameters;
    std::string sPopParameters;

    std::string returnVariable = word;

    p_lexer->advance();
    word = p_lexer->get_token_text();

    std::set<std::string> dependsOnList;  // Now using std::set to store dependencies
    bool isFunction = false;

    if (word == "=") {
        p_lexer->advance();
        std::string functionName = p_lexer->get_token_text();
        p_lexer->advance();
        word = p_lexer->get_token_text();
        if (word == "(") {
            f.addCall(functionName);
            p_lexer->advance();
            word = p_lexer->get_token_text();
            while (word != ")") {
                sPushParameters += "    PUSH(" + word + ");\n";
                sPopParameters += "    POP(" + word + ");\n";

                // No need for std::find, just insert into the set
                dependsOnList.insert(word);

                p_lexer->advance();
                word = p_lexer->get_token_text();
                if (word != ")") {
                    if (word != ",") {
                        word = ")";
                        return false;
                    } else {
                        p_lexer->advance();
                        word = p_lexer->get_token_text();
                    }
                }
            }
            isFunction = true;
        }
    } else if (word == "(") {
        f.addCall(returnVariable); // in this case, returnVariable is the function name
        p_lexer->advance();
        word = p_lexer->get_token_text();
        while (word != ")") {
            sPushParameters += "    PUSH(" + word + ");\n";
            sPopParameters += "    POP(" + word + ");\n";

            // Insert the word into the set directly
            dependsOnList.insert(word);

            p_lexer->advance();
            word = p_lexer->get_token_text();
            if (word != ")") {
                if (word != ",") {
                    word = ")";
                    return false;
                } else {
                    p_lexer->advance();
                    word = p_lexer->get_token_text();
                }
            }
        }
        isFunction = true;
    }

    if (!isFunction)
        return false;

    // Update the graph with dependencies from the set
    if (!returnVariable.empty() && returnVariable != "=") {
        updateGraph(maxStatementId, returnVariable, dependsOnList);
    }

    // Write the parallel version to fOut
    fOut << endl;

    fOut << endl << "int tempRank = 1;" << endl;
    fOut << "// Rank 0 sends arguments to other rank:" << endl;
    fOut << "if(rank == 0){" << endl;
    fOut << "    char* array = (char *) malloc(MAX_BYTES);" << endl;
    fOut << "    int nArray = 0; // length of the array" << endl;
    fOut << sPushParameters << endl;
    fOut << "    MPI_Send(array, nArray, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD);" << endl;
    fOut << "}else{" << endl;
    fOut << "    if(rank == tempRank){" << endl;
    fOut << "       char* array = (char *) malloc(MAX_BYTES);" << endl;
    fOut << "       int nArray = 0;" << endl;
    fOut << "       MPI_Recv(array, MAX_BYTES, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);" << endl;
    fOut << sPopParameters << endl;
    fOut << "   }" << endl;
    fOut << "}" << endl;
    fOut << endl;

    fOut << "MPI_Barrier(MPI_COMM_WORLD);" << endl;
    fOut << "cout << \"Rank \" << rank << \" in the middle.\" << endl;" << endl;

    fOut << endl;
    fOut << "if(rank == tempRank){" << endl;
    fOut << "    " << fileLine << endl;
    if (!returnVariable.empty() && returnVariable != "=") {
        fOut << "    char* array = (char *) malloc(MAX_BYTES);" << endl;
        fOut << "    int nArray = 0;" << endl;
        fOut << "    PUSH(" << returnVariable << ");" << endl;
        fOut << "    MPI_Send(array, nArray, MPI_CHAR, 0, 0, MPI_COMM_WORLD);" << endl;
    }
    fOut << "}else{" << endl;
    fOut << "    if(!rank){" << endl;
    if (!returnVariable.empty() && returnVariable != "=") {
        fOut << "        char* array = (char *) malloc(MAX_BYTES);" << endl;
        fOut << "        int nArray = 0;" << endl;
        fOut << "        MPI_Recv(array, MAX_BYTES, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);" << endl;
        fOut << "        POP(" << returnVariable << ");" << endl;
    }
    fOut << "    }" << endl;
    fOut << "}" << endl;
    fOut << endl;

    return isFunction;
}

void parseFunctionOrVariableDefinition(Functions &f, std::string &functionName, std::string fileLine, int maxStatementId, std::ifstream &fIn, std::ofstream &fOut, int parallelize) {
    functionName = "";

    std::istringstream ist{fileLine};
    Lexer* p_lexer = new Lexer{ist};
    std::string type = p_lexer->get_token_text();
    std::cout << "Type: " << type << std::endl;
    p_lexer->advance();

    bool isFunction = false;
    bool isVariable = false;
    bool firstVar = true;
    std::ostringstream varStream;

    while (true) {
        std::string name = p_lexer->get_token_text();
        std::cout << "Name: " << name << std::endl;
        p_lexer->advance();
        std::string word = p_lexer->get_token_text();
        std::cout << "Word: " << word << std::endl;

        if (word == "(") {
            isFunction = true;
            functionName = name;
            break;
        } else if (word == "=" || word == "," || word == ";") {
            isVariable = true;
            if (parallelize) {
                if (firstVar) {
                    varStream << type << " " << name;
                    firstVar = false;
                } else {
                    varStream << ", " << name;
                }
                if (word == "=") {
                    p_lexer->advance();
                    std::string value = p_lexer->get_token_text();
                    varStream << " = " << value;
                    p_lexer->advance();
                    word = p_lexer->get_token_text();
                    // Parse dependencies
                    std::set<std::string> dependsOnList;
                    std::string definitionLine = name + " = " + value;
                    parse(definitionLine, dependsOnList);
                    updateGraph(maxStatementId, Parser::_lhsToken, dependsOnList);
                }
                if (word == ";") {
                    varStream << word;
                    if (word == ";") {
                        break;
                    }
                }
            }
        } else {
            if (parallelize) {
                varStream << type << " " << name << word;
            }
            break;
        }
        p_lexer->advance();
    }

    if (parallelize && isVariable) {
        fOut << varStream.str() << std::endl;
        return;
    }

    if (isFunction) {
        f.addFunction(functionName);
        Functions::currentFunction = functionName;

        Graph newGraph;
        Graph::graphs.push_back(newGraph);
        Graph::iCurrentGraph = Graph::graphs.size() - 1;

        Variables newVariables;
        Variables::varSet.push_back(newVariables);
        Variables::iCurrentVarSet = Variables::varSet.size() - 1;

        if (parallelize) {
            fOut << fileLine << std::endl;
            if (functionName == "main") {
                fOut << "\n\
    int rank, nRanks;\n\
    MPI_Init(NULL, NULL);\n\
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);\n\
    MPI_Comm_size(MPI_COMM_WORLD, &nRanks);\n\
    ";
            }
        }
    }

    delete p_lexer;
}

void parseExpression(std::ofstream &fOut, std::string fileLine, const int maxStatementId) {
    cout << "Parsing expression " << fileLine << endl;
    if (PARALLELIZE)
        fOut << fileLine << endl;

    std::set<std::string> dependsOnList;
    parse(fileLine, dependsOnList);
    updateGraph(maxStatementId, Parser::_lhsToken, dependsOnList);
}

void parallelizeLoop(std::ifstream &fIn, std::ofstream &fOut, const std::string varName, const int val1, const int val2) {
    std::string fileLine;

    fOut << endl;
    fOut << "    char* array[MAX_RANKS];" << endl;
    fOut << "    int nArray[MAX_RANKS];" << endl;
    fOut << "    for(int tempRank = 0; tempRank < nRanks; tempRank++)" << endl;
    fOut << "        array[tempRank] = NULL;" << endl;
    fOut << "    if(rank == 0){" << endl;
    fOut << "        MPI_Request requestSend[MAX_RANKS];" << endl;
    fOut << "        MPI_Status statusSend[MAX_RANKS];" << endl;
    fOut << "        for(int tempRank = 1; tempRank < nRanks; tempRank++){" << endl;
    fOut << "            MPI_Isend(array[tempRank], nArray[tempRank], MPI_CHAR, tempRank, 0, MPI_COMM_WORLD, &requestSend[tempRank]);" << endl;
    fOut << "        }" << endl;
    fOut << "        for(int tempRank = 1; tempRank < nRanks; tempRank++){" << endl;
    fOut << "            MPI_Wait(&requestSend[tempRank], &statusSend[tempRank]);" << endl;
    fOut << "        }" << endl;
    fOut << "    }else{" << endl;
    fOut << endl;
    fOut << "        char* arrayInput;" << endl;
    fOut << "        int nArrayInput = 0;" << endl;
    fOut << "        MPI_Request requestRecvFrom0;" << endl;
    fOut << "        MPI_Status statusRecvFrom0;" << endl;
    fOut << "        MPI_Irecv(arrayInput, MAX_BYTES, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &requestRecvFrom0);" << endl;
    fOut << "        MPI_Wait(&requestRecvFrom0, &statusRecvFrom0);" << endl;
    fOut << "        MPI_Get_count(&statusRecvFrom0, MPI_CHAR, &nArrayInput);" << endl;
    fOut << "    }" << endl;
    fOut << endl;
    fOut << "    int minValue = (" << val2 << " - " << val1 << ") / nRanks * rank;" << endl;
    fOut << "    int maxValue = (" << val2 << " - " << val1 << ") / nRanks * (rank + 1);" << endl;
    fOut << "    if(maxValue > " << val2 << ")" << endl;
    fOut << "        maxValue = " << val2 << ";" << endl;
    fOut << endl;
    fOut << "    cout << \"Rank \" << rank << \" processing range \" << minValue << \"...\" << maxValue-1 << endl;" << endl;
    fOut << "    for(int " << varName << " = minValue; " << varName << " < maxValue; " << varName << "++){" << endl;

    getline(fIn, fileLine);
    updateOpenedBrackets(fileLine);
    fOut << fileLine << endl;

    getline(fIn, fileLine);
    updateOpenedBrackets(fileLine);
    fOut << "    }" << endl;

    fOut << endl;
    fOut << "    char* arrayResult = (char *) malloc(MAX_BYTES);" << endl;
    fOut << "    int nArrayResult = MAX_BYTES;" << endl;
    fOut << "    if(rank){" << endl;
    fOut << "        MPI_Request requestSendResult;" << endl;
    fOut << "        MPI_Status statusSendResult;" << endl;
    fOut << "        MPI_Isend(arrayResult, nArrayResult, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &requestSendResult);" << endl;
    fOut << "        MPI_Wait(&requestSendResult, &statusSendResult);" << endl;
    fOut << "    }else{" << endl;
    fOut << "        MPI_Request requestRecvResults[MAX_RANKS];" << endl;
    fOut << "        MPI_Status statusRecvResults[MAX_RANKS];" << endl;
    fOut << "        int nArrayResults[MAX_RANKS];" << endl;
    fOut << endl;
    fOut << "        for(int tempRank = 1; tempRank < nRanks; tempRank++){" << endl;
    fOut << "            char* arr = (char *) malloc(MAX_BYTES);" << endl;
    fOut << "            MPI_Irecv(arr, MAX_BYTES, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD, &requestRecvResults[tempRank]);" << endl;
    fOut << "        }" << endl;
    fOut << "        for(int tempRank = 1; tempRank < nRanks; tempRank++){" << endl;
    fOut << "            MPI_Wait(&requestRecvResults[tempRank], &statusRecvResults[tempRank]);" << endl;
    fOut << "            MPI_Get_count(&statusRecvResults[tempRank], MPI_CHAR, &nArrayResults[tempRank]);" << endl;
    fOut << "        }" << endl;
    fOut << "    }" << endl;
    fOut << endl;
}

void detectDependenciesInLoop(std::ifstream &fIn, std::ofstream &fOut, std::string &fileLine, int &maxStatementId, const int increment, int &loopMin, int &loopMax, const std::string varName, const int val1, const int val2) {
    getline(fIn, fileLine);
    updateOpenedBrackets(fileLine);

    std::vector <std::string> forLoopStatements;
    while (fileLine.find('}') == std::string::npos) {
        forLoopStatements.push_back(fileLine);
        getline(fIn, fileLine);
        updateOpenedBrackets(fileLine);
    }

    loopMin = maxStatementId + 1;

    int i = val1;
    while (i * increment < val2 * increment) {
        for (std::string x : forLoopStatements) {
            maxStatementId++;
            std::string toParse = std::regex_replace(x, std::regex(varName), to_string(i));
            parseExpression(fOut, toParse, maxStatementId);
        }
        i += increment;
    }
    
    loopMax = maxStatementId;
    cout << endl << "Loop range: " << loopMin << "-" << loopMax << endl << endl;
}

bool checkLoopDependency(int &loopMin, int &loopMax) {
    Graph &g = Graph::graphs[Graph::iCurrentGraph];
    Variables &v = Variables::varSet[Variables::iCurrentVarSet];
    std::set<std::string> writeVars;

    for (int iStatement = loopMin; iStatement < loopMax; iStatement++) {
        for (auto &var: v.vars) {
            for (int i = 0; i < v.vars.size(); i++) {
                bool found = false;
                for (auto &dep : g.dependencies) {
                    if (dep == Dependency(var, v.vars[i], 0)) {
                        found = dep.statementIdInStatementRangeExists(loopMin, loopMax);
                        if (found) {
                            writeVars.insert(var.getName());
                        }
                        break;
                    }
                }
            }
        }
    }

    std::set<std::string> readVars;
    for (int iStatement = loopMin; iStatement < loopMax; iStatement++) {
        for (auto &var: v.vars) {
            for (int i = 0; i < v.vars.size(); i++) {
                bool found = false;
                for (auto &dep : g.dependencies) {
                    if (dep == Dependency(v.vars[i], var, 0)) {
                        found = dep.statementIdInStatementRangeExists(loopMin, loopMax);
                        if (found) {
                            readVars.insert(var.getName());
                        }
                        break;
                    }
                }
            }
        }
    }

    if (overlap(writeVars, readVars)) {
        return false;
    } else {
        return true;
    }
}

bool overlap(const std::set<std::string>& s1, const std::set<std::string>& s2) {
    for (const auto& s : s1) {
        if (std::binary_search(s2.begin(), s2.end(), s))
            return true;
    }
    return false;
}

void parseLoopBody(const std::string &varName, int val1, int val2, bool increment, std::vector<std::string> &myvector, int &maxStatementId, std::unordered_map<std::string, bool> &varReads, std::unordered_map<std::string, bool> &varWrites) {
    cout << "Parsing loop body..." << val1 << val2 << endl;
    int i = val1;
    std::set<std::string> dependsOnList;
    while ((increment && i < val2) || (!increment && i > val2)) {
        for (std::string x : myvector) {
            maxStatementId++;
            // Find the position of the assignment operator
            size_t assignPos = x.find("=");

            // Replace occurrences of varName only in the right-hand side of the assignment
            if (assignPos != std::string::npos) {
                std::string lhs = x.substr(0, assignPos + 1); // LHS including '='
                std::string rhs = x.substr(assignPos + 1);    // RHS after '='

                // Replace varName in the RHS
                rhs = std::regex_replace(rhs, std::regex("\\b" + varName + "\\b"), std::to_string(i));

                // Reconstruct the line with the modified RHS
                x = lhs + rhs;
            }
            // std::cout << "#" << maxStatementId << "  Parsing expression " << x << std::endl;
            parse(x, dependsOnList); // Parse dependencies

            // Check reads and writes
            bool loopCarried = false;
            // std::cout << "Depends on: " << std::endl;
            // for (const auto &var : dependsOnList) {
            //     std::cout << var << std::endl;
            // }
            for (const auto &var : dependsOnList) {
                if (varWrites[var]) {
                    loopCarried = true; // Loop-carried dependence detected
                }
                varReads[var] = true;
            }
            updateGraph(maxStatementId, Parser::_lhsToken, dependsOnList); // Update dependency graph

            // Example of variable writing
            if (x.find("=") != std::string::npos) {
                std::string writtenVar = x.substr(0, x.find("="));
                varWrites[writtenVar] = true;
                if (varReads[writtenVar]) {
                    loopCarried = true; // Loop-carried dependence detected
                }
            }

            if (loopCarried) {
                std::cout << "Loop-carried dependence detected.\n";
            } else {
                // std::cout << "Loop-independent.\n";
            }
        }
        increment ? i++ : i--;
    }
}

void parseForLoop(std::string fileLine, int &maxStatementId, int &loopMin, int &loopMax, std::unordered_map<std::string, bool> &varReads, std::unordered_map<std::string, bool> &varWrites, std::ifstream &fIn, std::ofstream &fOut, int parallelize) {
    std::string word, str, varName, value, varName2, value2, sec3;
    bool increment = true;

    word = fileLine;
    word.erase(std::remove_if(word.begin(), word.end(), ::isspace), word.end());
    int size = word.find("(");
    
    str = word.substr(size + 1, word.length());
    size = str.find("=");
    varName = str.substr(0, size);
    str = str.substr(size + 1, str.length());

    value = str.substr(0, str.find(";"));

    str = str.substr(str.find(";") + 1, str.length());
    if (str.find("<") != std::string::npos) {
        size = str.find("<");
        varName2 = str.substr(0, size);
        size = str.find("<");
        str = str.substr(size + 1, str.length());
    } else {
        increment = false;
        size = str.find(">");
        varName2 = str.substr(0, size);
        size = str.find(">");
        str = str.substr(size + 1, str.length());
    }
    value2 = str.substr(0, str.find(";"));

    str.erase(std::remove_if(str.begin(), str.end(), ::isspace), str.end());
    str = str.substr(str.find(";") + 1, str.length() - 1);
    size = str.find(')');
    sec3 = str.substr(0, size);

    if (sec3.find("+") != std::string::npos)
        increment = true;
    else
        increment = false;

    int val1 = stoi(value);
    int val2 = stoi(value2);
    std::cout << "\nFor loop (" << varName << " " << val1 << ".." << val2-1 << ") found...\n";

    getline(fIn, fileLine);
    std::string toParse = fileLine;
    std::vector<std::string> myvector;
    while (toParse.find('}') == std::string::npos) {
        myvector.push_back(toParse);
        getline(fIn, fileLine);
        toParse = fileLine;
        toParse.erase(std::remove_if(toParse.begin(), toParse.end(), ::isspace), toParse.end());
    }

    parseLoopBody(varName, val1, val2, increment, myvector, maxStatementId, varReads, varWrites);
}

void parseWhile(std::string &word, Lexer *p_lexer, int &maxStatementId, std::unordered_map<std::string, bool> &varReads, std::unordered_map<std::string, bool> &varWrites, std::ifstream &fIn, std::ofstream &fOut) {
    p_lexer->advance();
    word = p_lexer->get_token_text();

    p_lexer->advance();
    std::string varName = p_lexer->get_token_text();
    
    p_lexer->advance();
    std::string sign = p_lexer->get_token_text();
    
    p_lexer->advance();
    std::string value1 = p_lexer->get_token_text();
    
    std::cout << "\nWhile loop (" << varName << " " << sign << " " << value1 << ") found..." << std::endl;

    std::set<std::string> dependsOnList;
    Parser parser;
    Parser::_dependsOnList = &dependsOnList;
    int val1 = parser.get_variable_value(varName);

    // Set loop boundaries based on sign
    bool increment = sign == "<" || sign == "<=";
    int val2 = stoi(value1); // Depending on the condition, set a boundary that can be adjusted

    getline(fIn, word);
    std::string toParse = word;
    std::vector<std::string> myvector;
    while (toParse.find('}') == std::string::npos) {
        myvector.push_back(toParse);
        getline(fIn, word);
        toParse = word;
        toParse.erase(std::remove_if(toParse.begin(), toParse.end(), ::isspace), toParse.end());
    }

    parseLoopBody(varName, val1, val2, increment, myvector, maxStatementId, varReads, varWrites);
}

void parseDoWhile(std::string &word, Lexer *p_lexer, int &maxStatementId, std::unordered_map<std::string, bool> &varReads, std::unordered_map<std::string, bool> &varWrites, std::ifstream &fIn, std::ofstream &fOut) {
    std::cout << "\nDo-While loop found...\n";
    
    getline(fIn, word);
    std::string toParse = word;
    std::vector<std::string> myvector;
    while (toParse.find('}') == std::string::npos) {
        myvector.push_back(toParse);
        getline(fIn, word);
        toParse = word;
        toParse.erase(std::remove_if(toParse.begin(), toParse.end(), ::isspace), toParse.end());
    }

    // Parse the condition after the body
    p_lexer->advance();
    p_lexer->advance();
    std::string varName = p_lexer->get_token_text();
    
    p_lexer->advance();
    std::string sign = p_lexer->get_token_text();
    
    p_lexer->advance();
    std::string value1 = p_lexer->get_token_text();

    std::set<std::string> dependsOnList;
    Parser parser;
    Parser::_dependsOnList = &dependsOnList;
    int val1 = parser.get_variable_value(varName);

    bool increment = sign == "<" || sign == "<=";
    int val2 = stoi(value1);

    parseLoopBody(varName, val1, val2, increment, myvector, maxStatementId, varReads, varWrites);
}

bool primitiveType(std::string &word) {
    return (word == "int") || (word == "bool") || (word == "double")
            || (word == "float") || (word == "double")
            || (word == "char") || (word == "string") || (word == "void");
}
