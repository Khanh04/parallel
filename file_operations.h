#ifndef FILE_OPERATIONS_H
#define FILE_OPERATIONS_H

#include "common.h"
#include "functions.h"

extern int numOpenedBrackets;

void updateOpenedBrackets(const std::string &fileLine);

void parseInputFileLine(std::ifstream &fIn, std::ofstream &fOut,
                        Functions &f,
                        std::string &functionName,
                        std::string &fileLine, int &maxStatementId,
                        bool &inMain, int parallelize);

#endif // FILE_OPERATIONS_H
