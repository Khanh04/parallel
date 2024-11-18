#ifndef DEPENDENCY_GRAPH_H
#define DEPENDENCY_GRAPH_H

#include <string>
#include <vector>
#include <set>
#include <iostream>
#include "variables.h"
#include "parser.h"

void updateGraph(const int maxStatementId, 
                 std::string lhsVar, 
                 std::set<std::string> &dependsOnList, 
                 Parser* parser = nullptr);

#endif // DEPENDENCY_GRAPH_H
