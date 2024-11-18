
#include <algorithm>
#include <vector>
#include "variables.h"

using namespace std;

int Var::maxId = 0;
std::vector<Variables> Variables::varSet;
int Variables::iCurrentVarSet = 0;
std::vector<Graph> Graph::graphs;
int Graph::iCurrentGraph = 0;

void printVector(vector<int> v){
    for(int i = 0; i < v.size(); i++){
        if(i)
            cout << ",";
        cout << v[i];
    }
}


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
        printVector(read);
        cout << ")";
    }
    if (write.size()) {
        cout << "w(";
        printVector(write);
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
    printVector(statementIds);
}

void Dependency::print() {
    cout << _variable << "<-" << _dependsOn << " (#";
    printStatementIds();
    cout << ")" << endl;
}

void Dependency::addIndex(int statementId) {
    statementIds.push_back(statementId);
}

bool Dependency::operator==(Dependency dep) {
    return (dep._variable == _variable) && (dep._dependsOn == _dependsOn);
}

bool Dependency::statementIdInStatementRangeExists(int min, int max) {
    for (int i = min; i <= max; i++) {
        if (std::find(statementIds.begin(), statementIds.end(), i) != statementIds.end())
            return true;
    }
    return false;
}

std::string Dependency::getStatementIdsString() {
    std::ostringstream oss;
    for (size_t i = 0; i < statementIds.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << statementIds[i];
    }
    return oss.str();
}


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

    // Print the header row
    cout << "Dependency table:" << endl;
    cout << "     "; // Starting space for row labels
    for (int i = 0; i < v.vars.size(); i++) {
        cout << v.vars[i] << " "; // Just print variable names with a single space
    }
    cout << endl;

    // Print each row
    for (int i = 0; i < v.vars.size(); i++) {
        cout << v.vars[i] << " "; // Print the row label
        for (int j = 0; j < v.vars.size(); j++) {
            bool found = false;
            for (auto &dep : dependencies) {
                if (dep == Dependency(v.vars[j], v.vars[i], 0)) {
                    cout << dep.getStatementIdsString() << " "; // Print dependency IDs
                    found = true;
                    break;
                }
            }
            if (!found) {
                cout << "- "; // Use "-" for no dependency
            }
        }
        cout << endl;
    }
    cout << endl;
}
