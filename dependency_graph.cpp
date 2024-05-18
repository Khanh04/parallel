#include "dependency_graph.h"
#include <iostream>
#include <algorithm>

using namespace std;

int Var::maxId = 0;
std::vector<Graph> Graph::graphs;
int Graph::iCurrentGraph;
std::vector<Variables> Variables::varSet;
int Variables::iCurrentVarSet;

Var::Var() {
    id = maxId++;
    name = "no name";
}

Var::Var(std::string varName) {
    id = maxId++;
    name = varName;
    Variables &v = Variables::varSet[Variables::iCurrentVarSet];
    v.vars.push_back(*this);
}

void Var::printDetailed() {
    cout << name << ", ";
    if (read.size()) {
        cout << "r(";
        for (int i = 0; i < read.size(); i++) {
            if (i) cout << ",";
            cout << read[i];
        }
        cout << ")";
    }
    if (write.size()) {
        cout << "w(";
        for (int i = 0; i < write.size(); i++) {
            if (i) cout << ",";
            cout << write[i];
        }
        cout << ")";
    }
    cout << endl;
}

bool Var::operator==(Var &v) {
    return v.name == name;
}

std::ostream& operator<<(std::ostream &out, Var &v) {
    return out << v.name;
}

void Var::setRead(int statementId) {
    read.push_back(statementId);
}

void Var::setWrite(int statementId) {
    write.push_back(statementId);
}

void Var::setName(std::string n) {
    name = n;
}

std::string Var::getName() {
    return name;
}

int Var::nameLength() {
    return name.length();
}

void Variables::print() {
    cout << "All variables: " << endl;
    for (auto &it : vars) {
        cout << it << endl;
    }
    cout << endl;
}

void Variables::printDetailed() {
    cout << "All variables: " << endl;
    for (auto &it : vars) {
        it.printDetailed();
    }
    cout << endl;
}

Var* Variables::findVar(const std::string &s) {
    for (auto &it : vars) {
        if (it.name == s)
            return &it;
    }
    return NULL;
}

Dependency::Dependency(Var &variable, Var &dependsOn, int statementId)
    : _variable(variable), _dependsOn(dependsOn) {
    statementIds.push_back(statementId);
}

void Dependency::printStatementIds() {
    for (int i = 0; i < statementIds.size(); i++) {
        if (i) cout << ",";
        cout << statementIds[i];
    }
}

void Dependency::print() {
    cout << _variable;
    cout << "<-";
    cout << _dependsOn;
    cout << " (#";
    printStatementIds();
    cout << ")";
    cout << endl;
}

void Dependency::addIndex(int statementId) {
    statementIds.push_back(statementId);
}

bool Dependency::operator==(Dependency dep) {
    return ((dep._variable == _variable) && (dep._dependsOn == _dependsOn));
}

bool Dependency::statementIdInStatementRangeExists(int min, int max) {
    for (int i = min; i <= max; i++) {
        if (std::find(statementIds.begin(), statementIds.end(), i) != statementIds.end())
            return true;
    }
    return false;
}

Graph::Graph() {}

void Graph::addDependency(Var &var, Var &dependsOn, int statementId) {
    Dependency dep(var, dependsOn, statementId);
    if (std::find(dependencies.begin(), dependencies.end(), dep) == dependencies.end()) {
        dependencies.push_back(dep);
    } else {
        std::find(dependencies.begin(), dependencies.end(), dep)->addIndex(statementId);
    }
}

void Graph::print() {
    cout << "Dependency graph (1. depends on 2.; # - statement ID where dependency exist):" << endl;
    for (auto &dep : dependencies) {
        dep.print();
    }
    cout << endl;
}

void Graph::printTable() {
    Variables &v = Variables::varSet[Variables::iCurrentVarSet];

    cout << "Dependency table:" << endl;
    cout << "     ";
    for (int i = 0; i < v.vars.size(); i++) {
        cout << v.vars[i];
        if (v.vars[i].nameLength() == 1)
            cout << "    ";
        if (v.vars[i].nameLength() == 2)
            cout << "   ";
        if (v.vars[i].nameLength() == 3)
            cout << "  ";
        if (v.vars[i].nameLength() == 4)
            cout << " ";
    }
    cout << endl;
    for (int i = 0; i < v.vars.size(); i++) {
        cout << v.vars[i];
        if (v.vars[i].nameLength() == 1)
            cout << "    ";
        if (v.vars[i].nameLength() == 2)
            cout << "   ";
        if (v.vars[i].nameLength() == 3)
            cout << "  ";
        if (v.vars[i].nameLength() == 4)
            cout << " ";
        for (int j = 0; j < v.vars.size(); j++) {
            bool found = false;
            for (auto &dep : dependencies) {
                if (dep == Dependency(v.vars[j], v.vars[i], 0)) {
                    dep.printStatementIds();
                    cout << "    ";
                    found = true;
                    break;
                }
            }
            if (!found)
                cout << "     ";
        }
        cout << endl;
    }
    cout << endl;
}

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
