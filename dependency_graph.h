#ifndef DEPENDENCY_GRAPH_H
#define DEPENDENCY_GRAPH_H

#include <string>
#include <vector>
#include <set>
#include <iostream>
#include "variables.h"

void updateGraph(const int maxStatementId, std::string lhsVar, std::vector<std::string> &dependsOnList);

#endif // DEPENDENCY_GRAPH_H
