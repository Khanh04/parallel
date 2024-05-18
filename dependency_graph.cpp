#include "dependency_graph.h"
#include <iostream>
#include <algorithm>

using namespace std;

void updateGraph(const int maxStatementId, std::string lhsVar, std::vector<std::string> &dependsOnList) {
    Variables &v = Variables::varSet[Variables::iCurrentVarSet];

    Var *lhs = v.findVar(lhsVar);
    if (!lhs) {
        Var temp(lhsVar);
        v.vars[v.vars.size() - 1].setWrite(maxStatementId);
    } else {
        lhs->setWrite(maxStatementId);
    }

    for (int iDep = 0; iDep < dependsOnList.size(); iDep++) {
        Var *rhs = v.findVar(dependsOnList[iDep]);
        if (!rhs) {
            Var temp(dependsOnList[iDep]);
            rhs = &v.vars[v.vars.size() - 1];
            lhs = v.findVar(lhsVar);
        }
        rhs->setRead(maxStatementId);
        lhs = v.findVar(lhsVar);
        if (lhs && rhs) {
            Graph::graphs[Graph::iCurrentGraph].addDependency(*lhs, *rhs, maxStatementId);
        }
    }
}
