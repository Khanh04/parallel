#ifndef DEPENDENCY_GRAPH_H
#define DEPENDENCY_GRAPH_H

#include <string>
#include <vector>
#include <set>
#include <iostream>

class Var {
public:
    Var();
    Var(std::string varName);
    void printDetailed();
    bool operator==(Var &v);
    friend std::ostream& operator<<(std::ostream &out, Var &v);
    void setRead(int statementId);
    void setWrite(int statementId);
    void setName(std::string n);
    std::string getName();
    int nameLength();
    friend class Variables;
private:
    static int maxId;
    int id;
    int type;
    std::vector<int> read;
    std::vector<int> write;
    std::string name;
};

class Variables {
public:
    void print();
    void printDetailed();
    Var* findVar(const std::string &s);
    std::vector<Var> vars;
    static std::vector<Variables> varSet;
    static int iCurrentVarSet;
private:
};

class Dependency {
public:
    Dependency(Var &variable, Var &dependsOn, int statementId);
    void printStatementIds();
    void print();
    void addIndex(int statementId);
    bool operator==(Dependency dep);
    bool statementIdInStatementRangeExists(int min, int max);
private:
    Var _variable;
    Var _dependsOn;
    std::vector<int> statementIds;
};

class Graph {
public:
    Graph();
    void addDependency(Var &var, Var &dependsOn, int statementId);
    void print();
    void printTable();
    std::vector<Dependency> dependencies;
    static std::vector<Graph> graphs;
    static int iCurrentGraph;
private:
};

void updateGraph(const int maxStatementId, std::string lhsVar, std::vector<std::string> &dependsOnList);

#endif // DEPENDENCY_GRAPH_H
