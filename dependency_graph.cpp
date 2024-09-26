#include "dependency_graph.h"
#include <iostream>
#include <algorithm>

using namespace std;

void updateGraph(const int maxStatementId, std::string lhsVar, std::set<std::string> &dependsOnList) {
    Variables &v = Variables::varSet[Variables::iCurrentVarSet];
    
    // Debug output
    cout << "maxStatementId: " << maxStatementId << endl;
    cout << "lhsVar: " << lhsVar << endl;
    cout << "dependsOnList: ";
    for (const auto &var : dependsOnList) {
        cout << var << " ";
    }
    cout << endl;
    
    // Find the LHS variable
    Var *lhs = v.findVar(lhsVar);
    if (!lhs) {
        Var temp(lhsVar);
        v.vars[v.vars.size() - 1].setWrite(maxStatementId);
    } else {
        lhs->setWrite(maxStatementId);
    }

    // Iterate over the set of dependencies
    for (const auto &depVar : dependsOnList) {
        Var *rhs = v.findVar(depVar);
        if (!rhs) {
            Var temp(depVar);
            rhs = &v.vars[v.vars.size() - 1];  // Assuming the variable is created here
            lhs = v.findVar(lhsVar);           // Re-fetch lhs to avoid invalid references
        }
        rhs->setRead(maxStatementId);  // Mark RHS as read in the current statement
        
        // Re-fetch lhs to avoid any invalid reference issues
        lhs = v.findVar(lhsVar);
        
        // If both LHS and RHS are valid, add the dependency to the graph
        if (lhs && rhs) {
            Graph::graphs[Graph::iCurrentGraph].addDependency(*lhs, *rhs, maxStatementId);
        }
    }
}
