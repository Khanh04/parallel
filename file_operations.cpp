#include "file_operations.h"
#include "parser.h"
#include "functions.h"
#include "dependency_graph.h"
#include "utils.h"
#include "lexer.h"
#include <sstream>
#include <regex>

int numOpenedBrackets = 0; // Initialize the global variable

void updateOpenedBrackets(const std::string &fileLine) {
    if (fileLine.find('{') != std::string::npos)
        numOpenedBrackets++;
    if (fileLine.find('}') != std::string::npos)
        numOpenedBrackets--;
    // Uncomment the line below for debugging
    // std::cout << " // numOpenedBrackets = " << numOpenedBrackets << ":";
}

bool nonAlpha(std::ofstream &fOut, std::string &fileLine, bool &inMain, int parallelize) {
    // Skip lines that don't start with a letter after optional spaces.
    int iFileLine = 0;
    while ((iFileLine < fileLine.length()) && isspace(fileLine[iFileLine]))
        iFileLine++;
    if (!isalpha(fileLine[iFileLine])) {
        if (fileLine[iFileLine] == '}') {
            std::cout << "} found, numOpenedBrackets = " << numOpenedBrackets << std::endl;
            // Add MPI_Finalize(); MPI directive at the end of the main:
            if (inMain) {
                fOut << "    MPI_Finalize();" << std::endl;
                inMain = false;
            }
        } else {
            std::cout << "Skipping empty or non-alpha starting line." << std::endl;
        }
        if (parallelize)
            fOut << fileLine << std::endl;
        return true;
    }
    return false;
}

void parseInputFileLine(std::ifstream &fIn, std::ofstream &fOut,
                        Functions &f,
                        std::string &functionName,
                        std::string &fileLine, int &maxStatementId,
                        bool &inMain, int parallelize) {
    // Skip empty lines:
    if (fileLine.empty())
        return;

    // Initialize read and write maps
    std::unordered_map<std::string, bool> varReads;
    std::unordered_map<std::string, bool> varWrites;

    // When { is detected, increase the number of open brackets
    // When } is detected, reduce the number of open brackets,
    // and act if the function is main:
    updateOpenedBrackets(fileLine);

    // Increase global statement ID,
    // so that the dependencies formed in this line are associated with a new ID:
    maxStatementId++;

    // Get the first word in order to determine
    // whether a loop starts in this line:
    std::cout << std::endl << "#" << maxStatementId << ": " << fileLine << ". Parsing..." << std::endl;

    if (nonAlpha(fOut, fileLine, inMain, parallelize))
        return;

    std::istringstream ist{fileLine};
    Lexer* p_lexer = new Lexer{ist};
    std::string word = p_lexer->get_token_text();
    
    // If function definition is detected:
    if (primitiveType(word)) {
        parseFunctionOrVariableDefinition(f, functionName, fileLine, maxStatementId, fIn, fOut, parallelize);
    } else if (word == "for") { 
        // if "for" loop is detected
        int loopMin, loopMax; // loop range in statement IDs
        parseForLoop(fileLine, maxStatementId, loopMin, loopMax, varReads, varWrites, fIn, fOut, parallelize);
    } else if (word == "while") { 
        // if "while" loop is detected
        parseWhile(word, p_lexer, maxStatementId, varReads, varWrites, fIn, fOut);
    } else if (word == "do") {
        // if "do-while" loop is detected
        parseDoWhile(word, p_lexer, maxStatementId, varReads, varWrites, fIn, fOut);
    } else {
        // Check if it's a function call or an expression
        if (fileLine.find("(") != std::string::npos && fileLine.find(")") != std::string::npos) {
            bool isFunction = parseFunctionCall(f, fOut, fileLine, maxStatementId);
            if (!isFunction) {
                parseExpression(fOut, fileLine, maxStatementId);
            }
        } else if (fileLine.find("=") != std::string::npos) {
            // New condition: Check for assignment operations
            parseExpression(fOut, fileLine, maxStatementId);
        } else {
            std::cout << "Skipping line " << fileLine << std::endl;
            if (parallelize) {
                // Copy the line from the input file into the output file
                fOut << fileLine << std::endl;
            }
        }
    }
}
