/*
Creating the parallel code from the C code.

This program creates Data dependency graph
and automatically parallelizes the code.

User test program in test.cpp is parallelized
and the resulting program is stored in testPar.cpp.

Scripts mpi_start.sh and openmp_start.sh
are created to execute the parallel code using MPI or OpenMP.
Before calling them, one might need to execute lamboot from shell.
*/

#include <iostream>
#include <vector>
#include <set>
#include <fstream>
#include <string>
#include <regex>
#include <algorithm>
#include <sstream>
#include <map>
#include <cctype>
#include <cmath>
#include <simd/math.h>

using namespace std;

#define VERBOSE 0
#define PARALLELIZE 0 // parse the input file, and produce the parallel equivalent

// Dependency graph constants (currently unused):
#define CONSTANT 0
#define SCALAR 1
#define ARRAY 2
#define POINTER 3

#define MAX_FUNCTIONS 100

// Error handling class
template<int N>
class Error {
public:
    explicit Error(const std::string& s) : message{s} { }
    std::string get_message() const { return message; }
    void put(std::ostream& os) const { os << message; }
    
private:
    std::string message;
};

double to_number(const std::string& s)
{
    std::istringstream ist{s};
    ist.exceptions(std::ios_base::failbit);
    double x;
    ist >> x;
    return x;
}

using Lexical_error = Error<1>;
using Syntax_error = Error<2>;
using Runtime_error = Error<3>;

template<int N>
std::ostream& operator<<(std::ostream& os, const Error<N>& e) {
    e.put(os);
    return os;
}

// Basic elements of our expressions
enum class Token {
    Id, Number, Sin, Cos, Tan, Asin, Acos, Atan, Log, Exp,
    Log10, Exp10, Sqrt, Int, Assign='=', Plus='+', Minus='-',
    Mul='*', Div='/', Mod='%', Pow='^', Lp='(', Rp=')', Eofsym=-1
};

// Lexer class for tokenizing the input
class Lexer {
public:
    explicit Lexer(std::istream& is);
    explicit Lexer(std::istream* ps);

    Lexer(const Lexer&) = delete;
    Lexer& operator=(const Lexer&) = delete;

    Lexer(Lexer&&) = delete;
    Lexer& operator=(Lexer&&) = delete;

    ~Lexer() { if (owns_input) delete p_input; }

    Token get_current_token() const { return current_token; }
    std::string get_token_text() const { return current_token_text; }
    void advance();

private:
    std::istream* p_input;
    bool owns_input;
    Token current_token;
    std::string current_token_text;

    void init();
    Token get_token();
    std::string token_buffer;
    void exponent_part(char& c);
};

Lexer::Lexer(std::istream& is) : p_input{&is}, owns_input{false} { init(); }
Lexer::Lexer(std::istream* ps) : p_input{ps}, owns_input{false} { init(); }

void Lexer::init() {
    current_token = get_token();
    current_token_text = token_buffer;
}

void Lexer::advance() {
    if (current_token != Token::Eofsym) {
        current_token = get_token();
        current_token_text = token_buffer;
    }
}

Token Lexer::get_token() {
    std::istream& input = *p_input;
    token_buffer.clear();

    char c = input.get();
    while (isspace(c)) c = input.get();
    if (!input) return Token::Eofsym;

    if (isalpha(c)) {
        token_buffer = c;
        c = input.get();
        while (isalnum(c) || (c == '[') || (c == ']')) {
            token_buffer += c;
            c = input.get();
        }
        input.putback(c);

        if (token_buffer == "sin") return Token::Sin;
        if (token_buffer == "cos") return Token::Cos;
        if (token_buffer == "tan") return Token::Tan;
        if (token_buffer == "asin") return Token::Asin;
        if (token_buffer == "acos") return Token::Acos;
        if (token_buffer == "atan") return Token::Atan;
        if (token_buffer == "log") return Token::Log;
        if (token_buffer == "exp") return Token::Exp;
        if (token_buffer == "log10") return Token::Log10;
        if (token_buffer == "exp10") return Token::Exp10;
        if (token_buffer == "sqrt") return Token::Sqrt;
        if (token_buffer == "int") return Token::Int;

        return Token::Id;
    }

    if (isdigit(c)) {
        token_buffer = c;
        c = input.get();
        while (isdigit(c)) {
            token_buffer += c;
            c = input.get();
        }
        if (c == '.') {
            token_buffer += c;
            c = input.get();
            while (isdigit(c)) {
                token_buffer += c;
                c = input.get();
            }
        }
        exponent_part(c);
        input.putback(c);
        return Token::Number;
    }

    if (c == '.') {
        token_buffer = c;
        c = input.get();
        if (!isdigit(c)) throw Lexical_error{token_buffer += c};
        while (isdigit(c)) {
            token_buffer += c;
            c = input.get();
        }
        exponent_part(c);
        input.putback(c);
        return Token::Number;
    }

    token_buffer = c;
    switch (c) {
        case '=': case '+': case '-': case '*': case '/': case '%':
        case '^': case '(': case ')': case ';': case '<': case '>':
        case '[': case ']': case ',':
            return Token(c);
    }

    throw Lexical_error{token_buffer};
}

void Lexer::exponent_part(char& c) {
    std::istream& input = *p_input;
    if (c != 'e' && c != 'E') return;
    token_buffer += c;
    c = input.get();
    if (c == '+' || c == '-') {
        token_buffer += c;
        c = input.get();
    }
    if (!isdigit(c)) throw Lexical_error{token_buffer += c};
    while (isdigit(c)) {
        token_buffer += c;
        c = input.get();
    }
}

// Symbol table for variables
std::map<std::string, double> symbol_table;

// Parser class for parsing expressions
class Parser {
public:
    Parser();
    double operator()(const std::string& s);
    static std::string _lhsToken;
    static std::vector<std::string> *_dependsOnList;

private:
    Lexer* p_lexer;

    double assign_expr();
    double add_expr();
    double mul_expr();
    double pow_expr();
    double unary_expr();
    double primary();

    double get_argument();
    static void check_domain(double x, double y);
};

std::string Parser::_lhsToken;
std::vector<std::string> *Parser::_dependsOnList = nullptr;

Parser::Parser() {
    symbol_table["pi"] = 4.0 * atan(1.0);
    symbol_table["e"] = exp(1.0);
}

double Parser::operator()(const std::string& s) {
    std::istringstream ist{s};
    p_lexer = new Lexer{ist};
    double result = assign_expr();
    delete p_lexer;
    return result;
}

double Parser::assign_expr() {
    Token t = p_lexer->get_current_token();
    std::string text = p_lexer->get_token_text();

    Parser::_lhsToken = "";

    double result = add_expr();

    if (p_lexer->get_current_token() == Token::Assign) {
        if (t != Token::Id)
            throw Syntax_error{"target of assignment must be an identifier"};

        if (text == "pi" || text == "e")
            throw Syntax_error{"attempt to modify the constant " + text};

        p_lexer->advance();
        return symbol_table[text] = add_expr();
    }

    return result;
}

double Parser::add_expr() {
    double result = mul_expr();
    for (;;) {
        switch (p_lexer->get_current_token()) {
            case Token::Plus:
                p_lexer->advance();
                result += mul_expr();
                break;
            case Token::Minus:
                p_lexer->advance();
                result -= mul_expr();
            default:
                return result;
        }
    }
}

double Parser::mul_expr() {
    double result = pow_expr();
    double x;
    for (;;) {
        switch (p_lexer->get_current_token()) {
            case Token::Mul:
                p_lexer->advance();
                result *= pow_expr();
                break;
            case Token::Div:
                p_lexer->advance();
                x = pow_expr();
                if (x == 0)
                    throw Runtime_error{"attempt to divide by zero"};
                result /= x;
                break;
            case Token::Mod:
                p_lexer->advance();
                x = pow_expr();
                if (x == 0)
                    throw Runtime_error{"attempt to divide by zero"};
                result = fmod(result, x);
                break;
            default:
                return result;
        }
    }
}

double Parser::pow_expr() {
    double result = unary_expr();
    if (p_lexer->get_current_token() == Token::Pow) {
        p_lexer->advance();
        double x = unary_expr();
        check_domain(result, x);
        return pow(result, x);
    }
    return result;
}

double Parser::unary_expr() {
    switch (p_lexer->get_current_token()) {
        case Token::Plus:
            p_lexer->advance();
            return +primary();
        case Token::Minus:
            p_lexer->advance();
            return -primary();
        default:
            return primary();
    }
}

double Parser::primary() {
    std::string text = p_lexer->get_token_text();
    double arg;

    switch (p_lexer->get_current_token()) {
        case Token::Id:
            if (Parser::_lhsToken.empty()) {
                Parser::_lhsToken = text;
                p_lexer->advance();
                std::string word = p_lexer->get_token_text();
                if (word == "[") {
                    p_lexer->advance();
                    std::string value = p_lexer->get_token_text();
                    p_lexer->advance();
                    word = p_lexer->get_token_text();
                    if (word != "]") {
                        std::cerr << "Parsing expression error: Expected ']' sign!" << std::endl;
                        exit(1);
                    }
                    Parser::_lhsToken = text + "[" + value + "]";
                    p_lexer->advance();
                }
            } else {
                p_lexer->advance();
                std::string word = p_lexer->get_token_text();
                if (word == "[") {
                    p_lexer->advance();
                    std::string value = p_lexer->get_token_text();
                    p_lexer->advance();
                    word = p_lexer->get_token_text();
                    if (word != "]") {
                        std::cerr << "Parsing expression error: Expected ']' sign!" << std::endl;
                        exit(1);
                    }
                    text += "[" + value + "]";
                    p_lexer->advance();
                }
                if (std::find(_dependsOnList->begin(), _dependsOnList->end(), text) == _dependsOnList->end()) {
                    Parser::_dependsOnList->push_back(text);
                }
            }
            return symbol_table[text];
        case Token::Number:
            if (std::find(_dependsOnList->begin(), _dependsOnList->end(), "PR") == _dependsOnList->end()) {
                Parser::_dependsOnList->push_back("PR");
            }
            p_lexer->advance();
            return to_number(text);
        case Token::Lp:
            p_lexer->advance();
            arg = add_expr();
            if (p_lexer->get_current_token() != Token::Rp)
                throw Syntax_error{"missing ) after subexpression"};
            p_lexer->advance();
            return arg;
        case Token::Sin:
            return sin(get_argument());
        case Token::Cos:
            return cos(get_argument());
        case Token::Tan:
            arg = get_argument();
            return tan(arg);
        case Token::Asin:
            return asin(get_argument());
        case Token::Acos:
            return acos(get_argument());
        case Token::Atan:
            return atan(get_argument());
        case Token::Log:
            arg = get_argument();
            return log(arg);
        case Token::Exp:
            return exp(get_argument());
        case Token::Log10:
            arg = get_argument();
            return log10(arg);
        case Token::Exp10:
            return simd::exp10(get_argument());
        case Token::Sqrt:
            arg = get_argument();
            if (arg < 0)
                throw Runtime_error{"attempt to take square root of negative number"};
            return sqrt(arg);
        case Token::Int:
            arg = get_argument();
            if (arg < 0)
                return ceil(arg);
            else
                return floor(arg);
        default:
            throw Syntax_error{"invalid primary expression"};
    }
}

void Parser::check_domain(double x, double y) {
    if (x >= 0) return;
    double e = std::abs(y);
    if (e <= 0 || e >= 1) return;
    throw Runtime_error{"attempt to take root of a negative number"};
}

double Parser::get_argument() {
    p_lexer->advance();
    if (p_lexer->get_current_token() != Token::Lp)
        throw Syntax_error{"missing ( after function name"};
    p_lexer->advance();
    double arg = add_expr();
    if (p_lexer->get_current_token() != Token::Rp)
        throw Syntax_error{"missing ) after function argument"};
    p_lexer->advance();
    return arg;
}

void parse(const std::string& s, std::vector<std::string>& dependsOnList) {
    Parser parser;
    Parser::_dependsOnList = &dependsOnList;
    
    std::cout.precision(12);
    
    try {
        double result = parser(s);
    } catch(const Lexical_error& e) {
        std::cerr << "Lexical error: " << e << '\n';
    } catch(const Syntax_error& e) {
        std::cerr << "Syntax error: " << e << '\n';
    } catch(const Runtime_error& e) {
        std::cerr << "Runtime error: " << e << '\n';
    }
}

// Number of opened and not closed { brackets
// from the beginning until currently read line:
int numOpenedBrackets;

void updateOpenedBrackets(const std::string& fileLine) {
    if (fileLine.find('{') != std::string::npos) numOpenedBrackets++;
    if (fileLine.find('}') != std::string::npos) numOpenedBrackets--;
}

void printVector(const std::vector<int>& v) {
    for (int i = 0; i < v.size(); i++) {
        if (i) std::cout << ",";
        std::cout << v[i];
    }
}

// Adds common and MPI-related lines to the beginning of the output file:
void addDefinesAndIncludes(std::ofstream& fOut) {
    fOut << R"(
using namespace std;

#define VERBOSE 0
#define MAX_RANKS 100
#define MAX_BYTES 1000

#include "mpi_functions.cpp"

)";
}

/*
Class Var is responsible for keeping information about variables.
Variables may include:
- constants
- scalar variables
- arrays
- pointers.

Each variable has a unique ID.
Each time a variable is read or written to,
id of the statement is associated it in
either a vector of statements when a read has occurred,
or a vector of statements when a write has occurred.
Only variables with names are stored in a vector of variables.
*/

class Var {
public:
    Var();
    Var(const std::string& varName);
    void printDetailed();
    bool operator==(const Var& v) const;
    friend std::ostream& operator<<(std::ostream& out, const Var& v);
    void setRead(int statementId);
    void setWrite(int statementId);
    void setName(const std::string& n);
    std::string getName() const;
    int nameLength() const;

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
    Var* findVar(const std::string& s);

private:
    std::vector<Var> vars;

public:
    static std::vector<Variables> varSet;
    static int iCurrentVarSet;
};

class Var {
public:
    Var();
    Var(const std::string& varName);
    void printDetailed();
    bool operator==(const Var& v) const;
    friend std::ostream& operator<<(std::ostream& out, const Var& v);
    void setRead(int statementId);
    void setWrite(int statementId);
    void setName(const std::string& n);
    std::string getName() const;
    int nameLength() const;

private:
    static int maxId;
    int id;
    int type;
    std::vector<int> read;
    std::vector<int> write;
    std::string name;
};

int Var::maxId = 0;
std::vector<Variables> Variables::varSet;
int Variables::iCurrentVarSet = 0;

Var::Var() : id(maxId++), name("no name") {}
Var::Var(const std::string& varName) : id(maxId++), name(varName) {
    Variables& v = Variables::varSet[Variables::iCurrentVarSet];
    v.vars.push_back(*this);
}

void Var::printDetailed() {
    std::cout << name << ", ";
    if (read.size()) {
        std::cout << "r(";
        printVector(read);
        std::cout << ")";
    }
    if (write.size()) {
        std::cout << "w(";
        printVector(write);
        std::cout << ")";
    }
    std::cout << std::endl;
}

bool Var::operator==(const Var& v) const {
    return v.name == name;
}

std::ostream& operator<<(std::ostream& out, const Var& v) {
    return out << v.name;
}

void Var::setRead(int statementId) {
    read.push_back(statementId);
}

void Var::setWrite(int statementId) {
    write.push_back(statementId);
}

void Var::setName(const std::string& n) {
    name = n;
}

std::string Var::getName() const {
    return name;
}

int Var::nameLength() const {
    return name.length();
}

class Dependency {
public:
    Dependency(Var& variable, Var& dependsOn, int statementId);
    void printStatementIds();
    void print();
    void addIndex(int statementId);
    bool operator==(const Dependency& dep) const;
    bool statementIdInStatementRangeExists(int min, int max);

private:
    Var _variable;
    Var _dependsOn;
    std::vector<int> statementIds;
};

class Graph {
public:
    Graph();
    void addDependency(Var& var, Var& dependsOn, int statementId);
    void print();
    void printTable();

private:
    std::vector<Dependency> dependencies;

public:
    static std::vector<Graph> graphs;
    static int iCurrentGraph;
};

class Functions {
public:
    void print();
    int findFunction(const std::string& s);
    void addCall(const std::string& f1, const std::string& f2);
    void addCall(const std::string& f2);
    void addFunction(const std::string& f);

private:
    std::vector<std::string> names;
    std::vector<std::vector<int>> functionCalls;

public:
    static std::string currentFunction;
};

std::string Functions::currentFunction = "";
std::vector<Graph> Graph::graphs;
int Graph::iCurrentGraph = 0;

class Variables {
public:
    void print();
    void printDetailed();
    Var* findVar(const std::string& s);
    std::vector<Var> vars;

private:

public:
    static std::vector<Variables> varSet;
    static int iCurrentVarSet;
};

Var::Var() : id(maxId++), name("no name") {}
Var::Var(const std::string& varName) : id(maxId++), name(varName) {
    Variables& v = Variables::varSet[Variables::iCurrentVarSet];
    v.vars.push_back(*this);
}

void Var::printDetailed() {
    std::cout << name << ", ";
    if (read.size()) {
        std::cout << "r(";
        printVector(read);
        std::cout << ")";
    }
    if (write.size()) {
        std::cout << "w(";
        printVector(write);
        std::cout << ")";
    }
    std::cout << std::endl;
}

bool Var::operator==(const Var& v) const {
    return v.name == name;
}

std::ostream& operator<<(std::ostream& out, const Var& v) {
    return out << v.name;
}

void Var::setRead(int statementId) {
    read.push_back(statementId);
}

void Var::setWrite(int statementId) {
    write.push_back(statementId);
}

void Var::setName(const std::string& n) {
    name = n;
}

std::string Var::getName() const {
    return name;
}

int Var::nameLength() const {
    return name.length();
}

class Dependency {
public:
    Dependency(Var& variable, Var& dependsOn, int statementId);
    void printStatementIds();
    void print();
    void addIndex(int statementId);
    bool operator==(const Dependency& dep) const;
    bool statementIdInStatementRangeExists(int min, int max);

private:
    Var _variable;
    Var _dependsOn;
    std::vector<int> statementIds;
};

class Graph {
public:
    Graph();
    void addDependency(Var& var, Var& dependsOn, int statementId);
    void print();
    void printTable();
    std::vector<Dependency> dependencies;
private:

public:
    static std::vector<Graph> graphs;
    static int iCurrentGraph;
};

class Functions {
public:
    void print();
    int findFunction(const std::string& s);
    void addCall(const std::string& f1, const std::string& f2);
    void addCall(const std::string& f2);
    void addFunction(const std::string& f);

private:
    std::vector<std::string> names;
    std::vector<std::vector<int>> functionCalls;

public:
    static std::string currentFunction;
};

// Function and variable tracking
bool inMain = false;

// Helper functions for dependency checking and parsing
void updateGraph(const int maxStatementId,
                 string lhsVar, vector<string> &dependsOnList){
    //cout << "LHS token found: " << endl << Parser::_lhsToken << endl;
    if(VERBOSE)
        cout << "LHS token " << lhsVar;

    // Current set of variables:    
    Variables &v = Variables::varSet[Variables::iCurrentVarSet];

    //Variables::print();

    // For a variable found on the left side,
    // set write statement ID to the current statement ID:
    // First, try to find this variable in a vector of variables:
    Var *lhs = v.findVar(lhsVar);
    if(!lhs){
        // If it is not found, create it and set maxStatementId as a statement ID:
        Var temp(lhsVar); 
        // temp is local; calling setWrite(maxStatementId) wont be stored. 
        // Therefore vars needs to be referenced:
        v.vars[v.vars.size() - 1].setWrite(maxStatementId);
    }else{
        // If it is found, set maxStatementId as a statement ID:
        lhs->setWrite(maxStatementId);
    }

    //v.print();    

    //cout << "The list of variable names that LHS depends on: " << endl;
    if(VERBOSE)
        cout << " depends on: ";
    // Print all vars that LHS depends on,
    // e.g. "LHS token a[3] depends on: a[2], PR":
    for(int iDep = 0; iDep < dependsOnList.size(); iDep++){
        if(iDep && VERBOSE)
            cout << ", ";
        // Print dependency variables:
        if(VERBOSE)
            cout << dependsOnList[iDep];

        // Try to find the variable dependsOnList[iDep]:
        Var *rhs = v.findVar(dependsOnList[iDep]);
        if(!rhs){ // if it doesn't exist
            // Create a local variable:
            Var temp(dependsOnList[iDep]);
            // New variable is added
            // to the end of the vector of variables:
            rhs = &v.vars[v.vars.size() - 1]; // the last one
            lhs = v.findVar(lhsVar);
        }
        // Store the statement ID
        // where the read of RHS variable has occured:
        rhs->setRead(maxStatementId);
        // Add the dependency to the dependency graph:
        lhs = v.findVar(lhsVar);
        if(lhs && rhs){
            if(VERBOSE)
                cout << "Adding dependency: " << lhs->getName()
                     << " <- " << rhs->getName() << endl;
            Graph::graphs[Graph::iCurrentGraph].addDependency(
                    *lhs, *rhs, maxStatementId);
        }else if(VERBOSE){
            if(!lhs){
                cout << "No lhs" << endl;
            }
            if(!rhs){
                cout << "No rhs" << endl;
            }
        }
    }
    if(VERBOSE)
        cout << endl;
}

bool parseFunctionCall(Functions &f, ofstream &fOut, const string &fileLine, const int maxStatementId) {
    std::istringstream ist{fileLine};
    std::unique_ptr<Lexer> p_lexer = std::make_unique<Lexer>(ist);
    string word = p_lexer->get_token_text();

    string sPushParameters, sPopParameters;
    string returnVariable = word;

    p_lexer->advance();
    word = p_lexer->get_token_text();

    vector<string> dependsOnList;
    bool isFunction = false;

    if (word == "=") {
        p_lexer->advance();
        string functionName = p_lexer->get_token_text();
        p_lexer->advance();
        word = p_lexer->get_token_text();

        if (word == "(") {
            cout << "Function call found: " << fileLine << endl;
            if (VERBOSE) {
                cout << "Return value: " << returnVariable << endl;
                cout << "Function name: " << functionName << endl;
            }

            f.addCall(functionName);

            p_lexer->advance();
            word = p_lexer->get_token_text();
            while (word != ")") {
                if (VERBOSE) {
                    cout << "Pushing argument " << word << " to the array of bytes to be sent" << endl
                         << "to the function executing rank." << endl;
                }

                sPushParameters += "    PUSH(" + word + ");\n";
                sPopParameters += "    POP(" + word + ");\n";

                if (std::find(dependsOnList.begin(), dependsOnList.end(), word) == dependsOnList.end()) {
                    dependsOnList.push_back(word);
                }

                p_lexer->advance();
                word = p_lexer->get_token_text();
                if (VERBOSE) {
                    cout << "After this argument, a word is found: " << word << endl;
                }

                if (word != ")") {
                    if (word != ",") {
                        cout << "Attention: comma or ')' expected in file line:" << endl << fileLine << endl;
                        return false;
                    } else {
                        p_lexer->advance();
                        word = p_lexer->get_token_text();
                    }
                }
            }
            isFunction = true;
        }
    }

    if (!isFunction) {
        return false;
    }

    updateGraph(maxStatementId, returnVariable, dependsOnList);

    // Write the parallel version of the function call to testPar.cpp
    fOut << "\n"
         << "int tempRank = 1;\n"
         << "// Rank 0 sends arguments to other rank:\n"
         << "if(rank == 0) {\n"
         << "    // Array storing data for the rank executing the function:\n"
         << "    char* array = (char *) malloc(MAX_BYTES);\n"
         << "    int nArray = 0; // length of the array\n"
         << sPushParameters << "\n"
         << "    // Send portion of data to processing element of rank number tempRank:\n"
         << "    cout << \"Rank 0 sending function call parameters to rank 1...\" << endl;\n"
         << "    MPI_Send(array, nArray, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD);\n"
         << "    cout << \"Rank 0 sent function call parameters to rank 1.\" << endl;\n"
         << "} else {\n"
         << "    // Only tempRank should be receiving the data and executing the function:\n"
         << "    if(rank == tempRank) {\n"
         << "        // Rank receives arguments prepared for it:\n"
         << "        char* array = (char *) malloc(MAX_BYTES); // array storing portion of data for this rank\n"
         << "        int nArray = 0; // length of the array\n"
         << "        cout << \"  Rank 1 receiving function call parameters from rank 0...\" << endl;\n"
         << "        MPI_Recv(array, MAX_BYTES, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // maximum MAX_BYTES characters\n"
         << sPopParameters << "\n"
         << "        cout << \"  Rank 1 received function call parameters from rank 0.\" << endl;\n"
         << "    }\n"
         << "}\n\n"
         << "MPI_Barrier(MPI_COMM_WORLD);\n"
         << "cout << \"Rank \" << rank << \" in the middle.\" << endl;\n\n"
         << "// Only rank 1 sends result of a function call to rank 0:\n"
         << "if(rank == tempRank) {\n"
         << "    cout << \"Rank 1 EXECUTING A FUNCTION CALL.\" << endl;\n"
         << fileLine << "\n"
         << "    // Array storing data for the result of a function call:\n"
         << "    char* array = (char *) malloc(MAX_BYTES);\n"
         << "    int nArray = 0; // length of the array\n"
         << "    // Send results to rank number 0:\n"
         << "    cout << \"  Rank 1 sending results of a function call to rank 0...\" << endl;\n"
         << "    PUSH(" << returnVariable << ");\n"
         << "    MPI_Send(array, nArray, MPI_CHAR, 0, 0, MPI_COMM_WORLD);\n"
         << "    cout << \"  Rank 1 sent results of a function call to rank 0.\" << endl;\n"
         << "} else {\n"
         << "    // Only rank 0 should be receiving the result:\n"
         << "    if(!rank) {\n"
         << "        // Array storing data for the result of a function call:\n"
         << "        char* array = (char *) malloc(MAX_BYTES);\n"
         << "        int nArray = 0; // length of the array\n"
         << "        // Start receiving results:\n"
         << "        cout << \"Rank 0 collecting results of a function call from rank 1...\" << endl;\n"
         << "        MPI_Recv(array, MAX_BYTES, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // maximum MAX_BYTES characters\n"
         << "        POP(" << returnVariable << ");\n"
         << "        cout << \"Rank 0 collected results of a function call from rank 1 (\" << " << returnVariable << " << \").\" << endl;\n"
         << "    }\n"
         << "}\n";

    return isFunction;
}


void parseExpression(ofstream &fOut, const string &fileLine, const int maxStatementId) {
    cout << "Parsing expression " << fileLine << endl;

    if (PARALLELIZE) {
        fOut << fileLine << endl;
    }

    vector<string> dependsOnList;
    parse(fileLine, dependsOnList);
    updateGraph(maxStatementId, Parser::_lhsToken, dependsOnList);
}

bool overlap(const std::set<string>& s1, const std::set<string>& s2)
{
    for( const auto& s : s1) {
        if(std::binary_search(s2.begin(), s2.end(), s))
            return true;
    }
    return false;
}


void parallelizeLoop(ifstream &fIn, ofstream &fOut, const string &varName, const int val1, const int val2) {
    fOut << R"(
    char* array[MAX_RANKS]; // array storing portions of data for each rank
    int nArray[MAX_RANKS]; // length of each array
    for (int tempRank = 0; tempRank < nRanks; tempRank++)
        array[tempRank] = NULL;

    if (rank == 0) { // only rank 0 sends pieces of data to other ranks
        // Sending & receiving are all non-blocking:
        MPI_Request requestSend[MAX_RANKS]; // for non-blocking sending
        MPI_Status statusSend[MAX_RANKS]; // for waiting on sending to finish
        for (int tempRank = 1; tempRank < nRanks; tempRank++) {
            // Send portion of data to processing element of rank number tempRank:
            MPI_Isend(array[tempRank], nArray[tempRank], MPI_CHAR, tempRank, 0, MPI_COMM_WORLD, &requestSend[tempRank]);
        }
        for (int tempRank = 1; tempRank < nRanks; tempRank++) {
            MPI_Wait(&requestSend[tempRank], &statusSend[tempRank]); // waiting for receiving to finish
        }
    } else {
        // Each rank receives a portion of data prepared for it:
        char* arrayInput; // array storing portion of data for this rank
        int nArrayInput = 0; // length of the array
        MPI_Request requestRecvFrom0; // for non-blocking receiving
        MPI_Status statusRecvFrom0; // for waiting on receiving to finish
        MPI_Irecv(arrayInput, MAX_BYTES, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &requestRecvFrom0); // maximum MAX_BYTES characters
        MPI_Wait(&requestRecvFrom0, &statusRecvFrom0); // waiting for receiving to finish
        // determine number of received bytes:
        MPI_Get_count(&statusRecvFrom0, MPI_CHAR, &nArrayInput);
        // The array is received. Deserialize the data,
        // and get number of parsed bytes from received array:
        // nArrayInput = deserialize_data(arrayInput);
    }

    // Split the for loop range onto nRanks ranks:
    int minValue = (val2 - val1) / nRanks * rank;
    int maxValue = (val2 - val1) / nRanks * (rank + 1);
    if (maxValue > val2)
        maxValue = val2;

    cout << "Rank " << rank << " processing range " << minValue << "..." << maxValue - 1 << endl;

    for (int )" << varName << R"( = minValue; )" << varName << R"( < maxValue; )" << varName << R"(++) {
)";

    string fileLine;
    getline(fIn, fileLine);
    updateOpenedBrackets(fileLine);
    fOut << fileLine << endl;

    getline(fIn, fileLine); // "}"
    updateOpenedBrackets(fileLine);
    fOut << "    }" << endl;

    fOut << R"(
    char* arrayResult = (char *) malloc(MAX_BYTES); // array storing results from this rank
    int nArrayResult = MAX_BYTES; // length of the array
    if (rank) { // only rank 0 doesn't send piece of data to rank zero
        MPI_Request requestSendResult; // for non-blocking sending
        MPI_Status statusSendResult; // for waiting on sending to finish
        // Send portion of results to processing element of rank number 0:
        MPI_Isend(arrayResult, nArrayResult, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &requestSendResult);
        MPI_Wait(&requestSendResult, &statusSendResult); // waiting for sending to finish
    } else {
        // only rank 0 receives pieces of data from other ranks
        MPI_Request requestRecvResults[MAX_RANKS]; // for non-blocking receiving
        MPI_Status statusRecvResults[MAX_RANKS]; // for waiting on receiving to finish
        int nArrayResults[MAX_RANKS]; // length of each array
        // Start receiving results from all ranks:
        for (int tempRank = 1; tempRank < nRanks; tempRank++) {
            char* arr = (char *) malloc(MAX_BYTES);
            MPI_Irecv(arr, MAX_BYTES, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD, &requestRecvResults[tempRank]); // maximum MAX_BYTES characters
        }
        // Wait until receiving results from each rank has finished:
        for (int tempRank = 1; tempRank < nRanks; tempRank++) {
            MPI_Wait(&requestRecvResults[tempRank], &statusRecvResults[tempRank]); // waiting for receiving to finish
            // determine number of received bytes:
            MPI_Get_count(&statusRecvResults[tempRank], MPI_CHAR, &nArrayResults[tempRank]);
            // The array is received. Deserialize the data,
            // and get number of parsed bytes from received array:
            // nArrayResults[tempRank] = deserialize_data(arrayInput);
        }
    }
)";
}

void detectDependenciesInLoop(ifstream &fIn, ofstream &fOut, string &fileLine, int &maxStatementId,
                              const int increment, int &loopMin, int &loopMax,
                              const string &varName, const int val1, const int val2) {
    vector<string> forLoopStatements;
    while (getline(fIn, fileLine)) {
        updateOpenedBrackets(fileLine);
        if (fileLine.find('}') != std::string::npos) break;
        forLoopStatements.push_back(fileLine);
    }

    loopMin = maxStatementId + 1;

    for (int i = val1; i * increment < val2 * increment; i += increment) {
        for (const auto &line : forLoopStatements) {
            maxStatementId++;
            string toParse = std::regex_replace(line, std::regex(varName), to_string(i));
            parseExpression(fOut, toParse, maxStatementId);
        }
    }

    loopMax = maxStatementId;
    cout << "\nLoop range: " << loopMin << " - " << loopMax << "\n" << endl;
}


bool checkLoopDependency(int &loopMin, int &loopMax) {
    Graph &g = Graph::graphs[Graph::iCurrentGraph];
    Variables &v = Variables::varSet[Variables::iCurrentVarSet];

    // Find the list of variables that are written to in the loop:
    set<string> writeVars;
    for (int iStatement = loopMin; iStatement < loopMax; ++iStatement) {
        for (auto &var : v.vars) {
            for (auto &dep : g.dependencies) {
                if (dep == Dependency(var, var, 0) && dep.statementIdInStatementRangeExists(loopMin, loopMax)) {
                    writeVars.insert(var.getName());
                    break;
                }
            }
        }
    }

    cout << "Variables that are written within a range:" << endl;
    for (const auto &var : writeVars) {
        cout << var << endl;
    }

    // Find the list of variables that are read in the loop:
    set<string> readVars;
    for (int iStatement = loopMin; iStatement < loopMax; ++iStatement) {
        for (auto &var : v.vars) {
            for (auto &dep : g.dependencies) {
                if (dep == Dependency(var, var, 0) && dep.statementIdInStatementRangeExists(loopMin, loopMax)) {
                    readVars.insert(var.getName());
                    break;
                }
            }
        }
    }

    cout << "Variables that are read within a range:" << endl;
    for (const auto &var : readVars) {
        cout << var << endl;
    }

    // Check if there is any overlap between writeVars and readVars:
    return !overlap(writeVars, readVars);
}

void parseLoop(string fileLine, int maxStatementId, int &loopMin, int &loopMax,
               Lexer* &p_lexer, ifstream &fIn, ofstream &fOut, int parallelize) {
    cout << "\nFor loop detected" << endl;

    // Remove all whitespace from fileLine
    fileLine.erase(remove_if(fileLine.begin(), fileLine.end(), ::isspace), fileLine.end());

    // Extract the loop components
    int openParenPos = fileLine.find("(");
    string loopContent = fileLine.substr(openParenPos + 1, fileLine.length() - openParenPos - 2);

    // Section 1: Variable initialization
    int equalsPos = loopContent.find("=");
    string varName = loopContent.substr(0, equalsPos);
    string value = loopContent.substr(equalsPos + 1, loopContent.find(";") - equalsPos - 1);
    cout << "\nvarName: " << varName << "\nvalue: " << value << endl;

    // Section 2: Loop condition
    loopContent = loopContent.substr(loopContent.find(";") + 1);
    int conditionPos = loopContent.find("<");
    bool increment = true;

    if (conditionPos == string::npos) {
        conditionPos = loopContent.find(">");
        increment = false;
    }

    string varName2 = loopContent.substr(0, conditionPos);
    string value2 = loopContent.substr(conditionPos + 1, loopContent.find(";") - conditionPos - 1);
    cout << "\nvarName condition: " << varName2 << "\nvalue: " << value2 << endl;

    // Section 3: Increment/Decrement operation
    loopContent = loopContent.substr(loopContent.find(";") + 1);
    string sec3 = loopContent.substr(0, loopContent.find(")"));
    if (sec3.find("+") != string::npos) {
        cout << "\nincrement" << endl;
    } else {
        increment = false;
    }

    int val1 = stoi(value);
    int val2 = stoi(value2);
    cout << "\nFor loop (" << varName << " " << val1 << ".." << val2 - 1 << ") found..." << endl;

    // Read the loop body
    vector<string> forLoopStatements;
    while (getline(fIn, fileLine)) {
        updateOpenedBrackets(fileLine);
        if (fileLine.find('}') != string::npos) break;
        forLoopStatements.push_back(fileLine);
    }

    // Detect loop range
    loopMin = maxStatementId + 1;

    // Parse the statements inside the for loop
    if (increment) {
        for (int i = val1; i < val2; ++i) {
            for (const string &line : forLoopStatements) {
                ++maxStatementId;
                string toParse = regex_replace(line, regex(varName), to_string(i));
                parseExpression(fOut, toParse, maxStatementId);
            }
        }
    } else {
        for (int i = val1; i > val2; --i) {
            for (const string &line : forLoopStatements) {
                ++maxStatementId;
                string toParse = regex_replace(line, regex(varName), to_string(i));
                parseExpression(fOut, toParse, maxStatementId);
            }
        }
    }

    loopMax = maxStatementId;
    cout << "\nLoop range: " << loopMin << " - " << loopMax << "\n" << endl;
}

void parseFunctionOrVariableDefinition(Functions &f, string &functionName, const string &fileLine, int maxStatementId,
                                       ifstream &fIn, ofstream &fOut, int parallelize) {
    // The assumption is that two types of statements can start with a primitive type keyword, e.g.:
    // 1) int a, b; - definition of variables
    // 2) int f(int a) { - function definition

    functionName.clear();

    std::istringstream ist{fileLine};
    auto p_lexer = std::make_unique<Lexer>(ist);
    string type = p_lexer->get_token_text();

    if (VERBOSE) {
        cout << "Primitive type " << type << " found." << endl;
    }

    // Parse the next word, i.e. variable name or function name:
    p_lexer->advance();
    string name = p_lexer->get_token_text();

    if (VERBOSE) {
        cout << "Name " << name << " found." << endl;
    }

    p_lexer->advance();
    string word = p_lexer->get_token_text();

    // Check if it's a variable definition
    if (word == "," || word == ";") {
        cout << "Skipping line with variable definition." << endl;
        if (parallelize) {
            fOut << fileLine << endl;
        }
        return;
    }

    // Check for variable definition with initialization
    if (word == "=") {
        cout << "Skipping line with variable definition and initialization." << endl;
        if (parallelize) {
            fOut << fileLine << endl;
        }
        return;
    }

    // Check for function definition
    if (word != "(") {
        cout << "'(' expected, but " << word << " found. Error in parsing!!!" << endl;
        if (parallelize) {
            fOut << fileLine << endl;
        }
        return;
    }

    cout << "Parsing function " << name << "." << endl;

    if (name == "main") {
        inMain = true;
    }

    // Register the function and set the current function context
    f.addFunction(name);
    Functions::currentFunction = name;

    // Create a separate dependency graph for the current function
    Graph::graphs.emplace_back();
    Graph::iCurrentGraph = Graph::graphs.size() - 1;

    if (VERBOSE) {
        cout << "iCurrentGraph = " << Graph::iCurrentGraph << endl;
    }

    // Create a separate set of variables for the current function
    Variables::varSet.emplace_back();
    Variables::iCurrentVarSet = Variables::varSet.size() - 1;

    if (VERBOSE) {
        cout << "iCurrentVarSet = " << Variables::iCurrentVarSet << endl;
    }

    if (parallelize) {
        fOut << fileLine << endl;
        if (name == "main") {
            if (VERBOSE) {
                cout << "Initializing MPI." << endl;
            }
            fOut << R"(
    int rank, nRanks;
    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nRanks);
)";
        }
    }
}

bool nonAlpha(std::ofstream& fOut, const std::string& fileLine, bool& inMain, int parallelize);
void parseWhile(const std::string& word, Lexer* p_lexer);
bool primitiveType(const std::string& word);
void parseInputFileLine(std::ifstream& fIn, std::ofstream& fOut, Functions& f, std::string& functionName, std::string& fileLine, int& maxStatementId, bool& inMain, int parallelize);

// Main function
int main() {
    int maxStatementId = 0;
    Functions f;

    std::ifstream fIn("test.cpp");
    std::ofstream fOut;
    if (PARALLELIZE)
        fOut.open("testPar.cpp", std::fstream::out);

    std::cout << "\n\nParsing input file test.cpp" << std::endl;

    if (PARALLELIZE)
        addDefinesAndIncludes(fOut);

    numOpenedBrackets = 0;
    std::string functionName = "";
    std::string fileLine;

    while (getline(fIn, fileLine)) {
        parseInputFileLine(fIn, fOut, f, functionName, fileLine, maxStatementId, inMain, PARALLELIZE);
    }

    fIn.close();
    if (PARALLELIZE)
        fOut.close();

    std::cout << "\n\nResults:" << std::endl;
    Variables& v = Variables::varSet[Variables::iCurrentVarSet];
    v.printDetailed();

    Graph::graphs[Graph::iCurrentGraph].print();
    Graph::graphs[Graph::iCurrentGraph].printTable();
    f.print();

    return 0;
}
