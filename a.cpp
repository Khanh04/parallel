/*
Creating the parallel code from the C code.

This program creates Data dependancy graph
and automatically parallelizes the code.

User test program in test.cpp is parallelized
and the resulting program is stored in testPar.cpp.

Scripts mpi_start.sh and openmp_start.sh
are created to execute the parallel code using MPI or OpenMP.
Before calling them, one might need to execute lamboot from shell.
*/

using namespace std;

#define VERBOSE 0
#define PARALLELIZE 0 // parse the input file, and produce the parallel equivalent

// TODO: Delete following? Decide what to do with data of this type:
// Dependency graph constants are not used at the moment:
#define CONSTANT 0
#define SCALAR 1
#define ARRAY 2
#define POINTER 3

#define MAX_FUNCTIONS 100

#include <iostream>
#include <vector>
#include <set> // For checking overlapping or LHS and RHS variables in a loop
#include <fstream>
#include <string>
#include <regex>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <map>
#include <cctype>
#include <cmath>
#include <simd/math.h>


/*

Parser is based on:
https://unclechromedome.org/c++-tutorials/expression-parser/index.html

Added static fields are:
string Parser::_lhsToken;
vector<string> *Parser::_dependsOnList;

Order of evaluating an expression:
parser
assign_expr
add_expr
mul_expr
pow_expr
unary_expr
primary

*/



double to_number(const std::string& s)
{
    std::istringstream ist{s};
    ist.exceptions(std::ios_base::failbit);
    double x;
    ist >> x;
    return x;
}

std::string to_string(double x)
{
    std::ostringstream ost;
    ost << x;
    return ost.str();
}

template<int N>
class Error {
public:
    explicit Error(const std::string s) : message{s} { }
    
    std::string get_message() const { return message; }
    void put(std::ostream& os) const { os << message; }
    
private:
    std::string message;
};

using Lexical_error = Error<1>;
using Syntax_error = Error<2>;
using Runtime_error = Error<3>;

template<int N>
std::ostream& operator<<(std::ostream& os, const Error<N>& e)
{
    e.put(os);
    return os;
}

// The basic elements of our expressions.
enum class Token {
    Id, Number, Sin, Cos, Tan, Asin, Acos, Atan, Log, Exp,
    Log10, Exp10, Sqrt, Int, Assign='=', Plus='+', Minus='-',
    Mul='*', Div='/', Mod='%', Pow='^', Lp='(', Rp=')', Eofsym=-1
};

class Lexer {
public:
    explicit Lexer(std::istream& is);
    explicit Lexer(std::istream* ps);
    
    // A Lexer belongs to a parser and shouldn't be copied or moved.
    
    Lexer(const Lexer&) = delete;
    Lexer& operator=(const Lexer&) = delete;
    
    Lexer(Lexer&&) = delete;
    Lexer& operator=(Lexer&&) = delete;
    
    ~Lexer() { if (owns_input) delete p_input; }
    
    Token get_current_token() const { return current_token; }
    std::string get_token_text() const { return current_token_text; }
    
    void advance();		// Read the next token in the stream.
    
private:
    std::istream* p_input;		// The source stream (a stream of characters).
    bool owns_input;			// True if we can delete p_input, false if we can't.
    
    Token current_token;
    std::string current_token_text;
    
    void init();				// Code common to each constructor.
    
    Token get_token();			// The workhorse. Assembles characters from p_input into tokens.
    std::string token_buffer;		// The text of the token that get_token() just found.
    
    void exponent_part(char& c);		// A helper function for get_token() when it is looking for a number.
};

Lexer::Lexer(std::istream& is)
    : p_input{&is}, owns_input{false}
{
    init();
}

Lexer::Lexer(std::istream* ps)
    : p_input{ps}, owns_input{false}
{
    init();
}

void Lexer::init()
{
    current_token = get_token();
    current_token_text = token_buffer;
}

void Lexer::advance()
{
    if (current_token != Token::Eofsym) {
        current_token = get_token();
        current_token_text = token_buffer;
    }
}

Token Lexer::get_token()
{
    std::istream& input = *p_input;		// Shorthand to make the notation convenient.
    
    token_buffer.clear();				// Clear the buffer for the new token.
    
    char c = input.get();				// A priming read on the stream.
    
    // Skip whitespace.
    while (isspace(c)) c = input.get();
    
    // If there are no characters, we're at the end of the stream.
    if (!input) return Token::Eofsym;
    
    // Look for an identifier or function name.
    if (isalpha(c)) {
        token_buffer = c;
        c = input.get();
        
        // Look for zero or more letters or digits.
        while (isalnum(c) || (c == '[') || (c == ']') ) {
            token_buffer += c;
            c = input.get();
        }
        
        // The current character doesn' belong to our identifier.
        input.putback(c);
        
        // Check for a function name.
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
        
        // Whatever is not a function name must be an identifier.
        return Token::Id;
    }
    
    // Look for a number beginning with a digit.
    if (isdigit(c)) {
        token_buffer = c;
        c = input.get();
        
        // Look for other digits.
        while (isdigit(c)) {
            token_buffer += c;
            c = input.get();
        }
        
        // Look for an optional decimal point.
        // If there is one, it can be followed by zero or more digits.
        if (c == '.') {
            token_buffer += c;
            c = input.get();
            
            while (isdigit(c)) {
                token_buffer += c;
                c = input.get();
            }
        }
        
        // Look for an optional exponent part.
        exponent_part(c);
        
        input.putback(c);
        return Token::Number;
    }
    
    // Look for a number beginning with a decimal point.
    if (c == '.') {
        token_buffer = c;
        c = input.get();
        
        // A decimal point must be followed by a digit. Otherwise we have an error.
        if (!isdigit(c)) {
            throw Lexical_error{token_buffer += c};
        }
        while (isdigit(c)) {
            token_buffer += c;
            c = input.get();
        }
        
        // Check for the optional exponent part.
        exponent_part(c);
        
        input.putback(c);
        return Token::Number;
    }
    
    // Check for a single character token.
    token_buffer = c;

    switch (c) {
    // Note: fallthrough intentional.
        case '=':
        case '+':
        case '-':
        case '*':
        case '/':
        case '%':
        case '^':
        case '(':
        case ')':
        case ';':
        case '<':
        case '>':
        case '[':
        case ']':
        case ',':
        return Token(c);
    }
    
    // Anything else is an error.
    throw Lexical_error{token_buffer};
}

void Lexer::exponent_part(char& c)
{
    std::istream& input = *p_input;
    
    if (c != 'e' || c != 'E')
        return;
    
    token_buffer += c;
    c = input.get();
    
    // Check for an optional sign.
    if (c == '+' || c == '-') {
        token_buffer += c;
        c = input.get();
    }
    
    // We must have a digit. Otherwise, we have an error.
    if (!isdigit(c))
        throw Lexical_error{token_buffer += c};
    while (isdigit(c)) {
        token_buffer += c;
        c = input.get();
    }
}

std::map<std::string, double> symbol_table;

class Parser {
public:
    Parser();
    double operator()(const std::string& s);
    static string _lhsToken;
    static vector<string> *_dependsOnList;
    
private:
    Lexer* p_lexer;
    
    double assign_expr();
    double add_expr();
    double mul_expr();
    double pow_expr();
    double unary_expr();
    double primary();
    
    double get_argument();
    
    // Check for root of a negative number.
    static void check_domain(double x, double y);
};

string Parser::_lhsToken;
vector<string> *Parser::_dependsOnList = NULL;

Parser::Parser()
{
    symbol_table["pi"] = 4.0 * atan(1.0);
    symbol_table["e"] = exp(1.0);
}

// Parsing the expression given in s
double Parser::operator()(const std::string& s)
{
    std::istringstream ist{s};
    p_lexer = new Lexer{ist};
    double result = assign_expr();
    delete p_lexer;
    return result;
}

double Parser::assign_expr()
{
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

        //cout << "LHS Token: " << Parser::_lhsToken << endl;

        return symbol_table[text] = add_expr();
    }
    
    return result;
}

double Parser::add_expr()
{
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

double Parser::mul_expr()
{
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

double Parser::pow_expr()
{
    double result = unary_expr();
    
    if (p_lexer->get_current_token() == Token::Pow) {
        p_lexer->advance();
        double x = unary_expr();
        check_domain(result, x);
        return pow(result, x);
    }
    
    return result;
}

double Parser::unary_expr()
{
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

double Parser::primary()
{
    std::string text = p_lexer->get_token_text();
    double arg;
    
    switch (p_lexer->get_current_token()) {
        case Token::Id:

            if(Parser::_lhsToken == ""){
                Parser::_lhsToken = text;

                p_lexer->advance();

                std::string word = p_lexer->get_token_text();
                if(word == "["){ // array element
                    //cout << "[ found!" << std::endl;

                    p_lexer->advance();
                    std::string value = p_lexer->get_token_text();
                    //cout << endl << "word (value expected): '" << value << "'" << endl << endl;

                        p_lexer->advance();
                    word = p_lexer->get_token_text();
                    if(word != "]"){
                        cout << "Parsing expression error: Expected ']' sign!" << std::endl;
                        exit(1);
                    }

                    Parser::_lhsToken = text + "[" + value + "]";

                    p_lexer->advance();
                }
            }else{
                //cout << "RHS Token: " << text << endl;

                p_lexer->advance();

                std::string word = p_lexer->get_token_text();
                if(word == "["){ // array element
                    //cout << "[ found!" << std::endl;

                    p_lexer->advance();
                    std::string value = p_lexer->get_token_text();
                    //cout << endl << "word (value expected): '" << value << "'" << endl << endl;

                        p_lexer->advance();
                    word = p_lexer->get_token_text();
                    if(word != "]"){
                        cout << "Parsing expression error: Expected ']' sign!" << std::endl;
                        exit(1);
                    }

                    text += "[" + value + "]";

                    p_lexer->advance();
                }

                // If dep isn't already in _dependsOnList, add it:
                if(std::find(_dependsOnList->begin(), _dependsOnList->end(), text) == _dependsOnList->end()) {
                    Parser::_dependsOnList->push_back(text);
                }
            }


            return symbol_table[text];
        case Token::Number:

            // If a number (PR) isn't already in _dependsOnList, add it:
            if(std::find(_dependsOnList->begin(), _dependsOnList->end(), "PR") == _dependsOnList->end()) {
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
    } // switch
}

void Parser::check_domain(double x, double y)
{
    // There is no error if x is nonnegative.
    if (x >= 0) return;
    
    // There is no error unless 0 < abs(y) < 1.
    double e = std::abs(y);
    if (e <= 0 || e >= 1) return;
    
    // We have an error.
    throw Runtime_error{"attempt to take root of a negative number"};
}

double Parser::get_argument()
{
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

// Parsing expression given in s, e.g. p=q+r
void parse(const string s, vector<string> &dependsOnList){
    Parser parser;
    Parser::_dependsOnList = &dependsOnList;
    
    std::cout.precision(12);
    
    try {
        // Call to parser:
        double result = parser(s);
        
        //std::cout << result << '\n';
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

// Assumption is that only
// up to one opened bracket {
// and up to one closed bracket }
// may exist at a single line:
void updateOpenedBrackets(string fileLine){
    if(fileLine.find('{') != std::string::npos)
        numOpenedBrackets++;
    if(fileLine.find('}') != std::string::npos)
        numOpenedBrackets--;
    //cout << " // numOpenedBrackets = " << numOpenedBrackets << ":";
}

void printVector(vector<int> v){
    for(int i = 0; i < v.size(); i++){
        if(i)
            cout << ",";
        cout << v[i];
    }
}

// Adds common and MPI-related lines to the beginning of the output file:
void addDefinesAndIncludes(ofstream &fOut){
    fOut << "\n\
using namespace std;\n\
\n\
#define VERBOSE 0\n\
// PARALLELIZE constant directs whether the parallel program should be created.\n\
// Value 0 means that the input file will be solely parsed,\n\
// without parallelization\n\
// Value 1 means that the input file will be parsed,\n\
// and the parallel version of the algorithm will be created.\n\
#define MAX_RANKS 100 // maximum number of ranks allowed\n\
#define MAX_BYTES 1000 // maximum number of bytes that each rank is allowed to send/receive\n\
\n\
#include \"mpi_functions.cpp\"\n\
\n";
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
either a vector of statements when a read has occured,
or a vector of statements when a write has occured.
Only variables with names are stored in a vector of variables.

*/

class Var{
public:
    // Default constructor:
    Var();
    // Constructing a variable with a name:
    Var(string varName);
    void printDetailed();
    // Two variables are considered to be the same variable
    // if and only if the name is the same:
    bool operator == (Var &v){
        return v.name == name;
    }
    friend ostream& operator<<(ostream &out, Var &v){
        return out << v.name; //" (ID=" << id << ")" << endl;
    }
    void setRead(int statementId){
        //Storing the ID of current statement
        //into the vector of IDs when a read has occured:
        read.push_back(statementId);
    }
    void setWrite(int statementId){
        //Storing the ID of current statement
        //into the vector of IDs when a write has occured:
        write.push_back(statementId);
    }
    void setName(string n){
        name = n;
    }
    string getName(){
        return name;
    }
    int nameLength(){ // used for printing purposes
        return name.length();
    }
    friend class Variables;
private:
    static int maxId; // maximal ID value assigned to any variable so far
    int id; // unique ID of a variable
    int type; // possible values are: CONSTANT, SCALAR, ARRAY, POINTER
    vector<int> read; // statement IDs where read has occured
    vector<int> write; // statement IDs where write has occured
    string name; // name of the variable
};


class Variables{
public:
    void print(){
        cout << "All variables: " << endl;
        for(auto &it : vars){
            cout << it << endl;
        }
        cout << endl;
    }
    void printDetailed(){
        cout << "All variables: " << endl;
        for(auto &it : vars){
            it.printDetailed();
        }
        cout << endl;
    }
    // Finding a variable of a given name, if exists:
    Var* findVar(const string &s){
        for(auto &it : vars){
            if(it.name == s)
                return &it;
        }
        return NULL;
    }

    // Variables are stored in a vector vars:
    vector<Var> vars;
    
    static vector<Variables> varSet;
    static int iCurrentVarSet;
private:
};


class Var;

Var::Var(){
    id = maxId++; // assigns an unique ID to each variable
    name = "no name"; // variable can have its name.
}

// Constructing a variable with a name:
Var::Var(string varName){
    id = maxId++; // assigns an unique ID to each variable
    name = varName;
    //cout << "Adding variable " << varName << endl;
    
    // Current set of variables:    
    Variables &v = Variables::varSet[Variables::iCurrentVarSet];

    // Only variables with names are stored in a vector of variables:
    v.vars.push_back(*this);
}

void Var::printDetailed(){
    // Example printing of a variable: p, r(3,7)w(1)
    // This means that variable p has been read in statements with IDs 3 and 7,
    // and that it has been written to in a statement with ID 1.
    cout << name << ", ";
    if(read.size()){
        cout << "r";
        //cout << read.size();
        // Print statement IDs where read has occured:
        cout << "(";
        printVector(read);
        cout << ")";
    }
    if(write.size()){
        cout << "w";
        //cout << write.size();
        // Print statement IDs where write has occured:
        cout << "(";
        printVector(write);
        cout << ")";
    }
    cout << endl;
}


/*
Class Dependency is responsible for keeping a single dependency,
e.g a<-b, where a is a variable that depends on variable b.
*/
class Dependency{
public:
    // Constructor of a dependency
    Dependency(Var &variable, Var &dependsOn, int statementId){
        _variable = variable;
        _dependsOn = dependsOn;
        statementIds.push_back(statementId);
    }
    // Printing values of all statements where the dependency appears:
    void printStatementIds(){
        printVector(statementIds);
    }
    // Printing a dependency in a form: b<-q (#3),
    // where b is a variable that depends on variable q
    // based on statement with ID = 3:
    void print(){
        cout << _variable;
        cout << "<-";
        cout << _dependsOn;
        cout << " (#";
        printStatementIds();
        cout << ")";
        cout << endl;
    }
    // If a parsed dependency already exists,
    // the statement ID where the dependency is found again
    // should be added to the vector of IDs of statements
    // where this dependency exists:
    void addIndex(int statementId){
        statementIds.push_back(statementId);
    }
    // Dependency is considered to be equal to another one if and only if
    // both variables are the same, and in the same order,
    // no matter of statement IDs:
    bool operator == (Dependency dep){
        return( (dep._variable == _variable) && (dep._dependsOn == _dependsOn) );
    }
    bool statementIdInStatementRangeExists(int min, int max){
        for(int i = min; i <= max; i++){
            // If found:
            if(std::find(statementIds.begin(), statementIds.end(), i)
                    != statementIds.end())
                return true;
        }
        return false;
    }
private:
    Var _variable; // A variable that depends on another
    Var _dependsOn; // A variable it depends on
    vector<int> statementIds; // statement IDs where the dependency is found
};


class Var;

/*
Dependency graph is a graph storing dependencies between variables.
*/
class Graph{
public:
    // Default constructor:
    Graph(){}
    // Adding a dependency between two variables if it didn't exist,
    // or adding statement ID to the vector of statemetn IDs if it existed:
    void addDependency(Var &var, Var &dependsOn, int statementId){
        // A new temporary dependency is created for given two variables:
        Dependency dep(var, dependsOn, statementId);
        
        // If this temporaty dependency isn't already in dependencies:
        if(std::find(dependencies.begin(), dependencies.end(), dep) == dependencies.end()) {
            // Add temporary dependency to the vector of dependencies:
            dependencies.push_back(dep);
        }else{
            // Find the dependency, 
            // and add current statement ID to the vector of statement IDs:
            std::find(dependencies.begin(), dependencies.end(), dep)->addIndex(statementId);
        }
    }
    // Printing dependencies in a form:
    // p<-r (#1), where variable p depends on variable r based on statement with ID = 1:
    void print(){
        cout << "Dependency graph (1. depends on 2.; # - statement ID where dependency exist):" << endl;
        for(auto &dep : dependencies){
            dep.print();
        }
        cout << endl;
    }
    // Printing dependencies in a form of a table, e.g.
    //p    q    r    a    PR   b    c    a[3] a[2] 
    //p                             3,7                   
    //q    1                        3                   
    //r    1                                            
    //a                             7                   
    //PR                  2                   5         
    //b                   2                             
    //c                   2                             
    //a[3]                                              
    //a[2]                                    5
    void printTable(){
        // Current set of variables:    
        Variables &v = Variables::varSet[Variables::iCurrentVarSet];

        cout << "Dependency table:" << endl;
        // First row is a header, consisting of variable names:
        cout << "     ";
        for(int i = 0; i < v.vars.size(); i++){
            cout << v.vars[i];
            if(v.vars[i].nameLength() == 1)
                cout << "    ";
            if(v.vars[i].nameLength() == 2)
                cout << "   ";
            if(v.vars[i].nameLength() == 3)
                cout << "  ";
            if(v.vars[i].nameLength() == 4)
                cout << " ";
        }
        cout << endl;
        // Each of the following rows is responsible for a single variable:
        for(int i = 0; i < v.vars.size(); i++){
            cout << v.vars[i];
            if(v.vars[i].nameLength() == 1)
                cout << "    ";
            if(v.vars[i].nameLength() == 2)
                cout << "   ";
            if(v.vars[i].nameLength() == 3)
                cout << "  ";
            if(v.vars[i].nameLength() == 4)
                cout << " ";
            // For this variable vars[i], print dependent variables:
            for(int j = 0; j < v.vars.size(); j++){
                // Try to find whether the dependency
                // between vars[i] and vars[j] exists:
                bool found = false;
                for(auto &dep : dependencies){
                    if(dep == Dependency(v.vars[j],
                                         v.vars[i], 0)){
                        dep.printStatementIds();
                        cout << "    ";
                        found = true;
                        break;
                    }
                }
                // If the dependency is not found, print spaces instead:
                if(!found)
                    cout << "     ";
            }
            cout << endl;
        }
        cout << endl;
    }
//private:
    // Dependencies are stored in a vector:
    vector<Dependency> dependencies;
    static vector<Graph> graphs;
    static int iCurrentGraph;
};


class Functions{
public:
    void print(){
        cout << "All function calls: " << endl;
        for(int i = 0; i < names.size(); i++){
            if(functionCalls[i].empty()){
                cout << names[i] << " not calling any function." << endl;
            }else{
                cout << names[i] << " calling: ";
                for(int j = 0; j < names.size(); j++){
                    if(std::find(functionCalls[i].begin(),
                                 functionCalls[i].end(),
                                 j)
                           != functionCalls[i].end()){
                        cout << names[j] << ", ";
                    }
                }                    
                cout << endl;
            }
        }
        cout << endl;
    }
    // Finding a function of a given name, if exists:
    int findFunction(const string &s){
        for(int i = 0; i < names.size(); i++){
            if(names[i] == s)
                return i;
        }
        return -1;
    }
    
    void addCall(string f1, string f2){
        if(VERBOSE)
            cout << "Function " << f1 << " calls " << f2 << endl;
        int i1 = findFunction(f1);
        int i2 = findFunction(f2);
        if(i1 == -1){
            cout << "Function " << f1 << " not found!" << endl;
            exit(1);
        }
        if(i2 == -1){
            cout << "Function " << f2 << " not found!" << endl;
            exit(1);
        }
        functionCalls[i1].push_back(i2);
    }
    
    void addCall(string f2){
        addCall(currentFunction, f2);
    }

    void addFunction(string f){
        if(VERBOSE)
            cout << "Adding function " << f << endl;
        names.push_back(f);
        vector<int> v;
        functionCalls.push_back(v);
    }

    // Names of functions:
    vector<string> names;
    // Who calls whom:
    vector<vector<int>> functionCalls;
    
    static string currentFunction;
private:
};

string Functions::currentFunction = "";



// When a function parsing is finished, } should follow MPI_Finalize in main().
bool inMain;


/*
Statements are numerated.
During the creation of the data dependency graph,
each time a statement is parsed, it gets a new statement id.
For example, statements a=b+c within a loop that iterates 10 times
will be assigned a new id 10 times.
This id is associated with variables that are read or written to.
int maxStatementId = 0;
*/


// Initializing a maximal variable ID to 0:
int Var::maxId = 0;

// Graphs from all functions found in user cpp file:
vector<Graph> Graph::graphs;
int Graph::iCurrentGraph;

// Variable sets from all functions found in user cpp file:
vector<Variables> Variables::varSet;
int Variables::iCurrentVarSet;

// During parsing expression, certain dependencies between variables are found.
// This function updates dependency graph accordingly:
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

bool parseFunctionCall(Functions &f,
                       ofstream &fOut,
                       string fileLine, const int maxStatementId){
    // Function is being parsed, e.g. c = f(a, b);:

    std::istringstream ist{fileLine};
    Lexer* p_lexer = new Lexer{ist};
    string word = p_lexer->get_token_text();
    //cout << "Word '" << word << "' found." << endl;

    // As a list of parameters is parsed.
    // For each of them, rank 0 should call PUSH macro.
    // A string with calls to PUSH macro
    // is appended as arguments are parsed,
    // so that these parameters can be pushed once needed.
    // At the same time, calls to POP macro are appended. 
    string sPushParameters;
    string sPopParameters;

    // Try to recognize function call with return value, e.g. c = f(a,b);

    // A result should be sent back to the rank 0.
    // However, while function is still being parsed,
    // it cannot be guarantied that the statement
    // will be treated as a function call.
    string returnVariable = word; // e.g. c

    p_lexer->advance();
    word = p_lexer->get_token_text();

    // if function call is detected, update dependencies between
    // return value and parameters:
    vector<string> dependsOnList;
    
    bool isFunction = false;
    if(word == "="){
        p_lexer->advance();
        string functionName = p_lexer->get_token_text();
        p_lexer->advance();
        word = p_lexer->get_token_text();
        if(word == "("){
            cout << "Function call found: " << fileLine << endl;
            if(VERBOSE)
                cout << "Return value: " << returnVariable << endl;
            if(VERBOSE)
                cout << "Function name: " << functionName << endl;
            
            // Store that this function is called from currentFunction:
            f.addCall(functionName);

            p_lexer->advance();
            word = p_lexer->get_token_text();
            // Assumption is that ")" will appear in the same fileline:
            while(word != ")"){
                if(VERBOSE)
                    cout << "Pushing argument " << word
                         << " to the array of bytes to be sent" << endl
                         << "to the function executing rank." << endl;

                sPushParameters += "    PUSH(" + word + ");\n";
                sPopParameters += "    POP(" + word + ");\n";

                // If dep isn't already in dependsOnList, add it:
                if(std::find(dependsOnList.begin(),
                             dependsOnList.end(), word)
                        == dependsOnList.end()) {
                    dependsOnList.push_back(word);
                }

                p_lexer->advance();
                word = p_lexer->get_token_text();
                if(VERBOSE)
                    cout << "After this argument, a word is found: "
                         << word << endl;
                // "," or ")" expected:
                if(word != ")"){
                    if(word != ","){
                        cout << "Attention: comma or ')' expected in file line:"
                             << endl << fileLine << endl;
                        word = ")";
                        return false;
                    }else{
                        p_lexer->advance();
                        word = p_lexer->get_token_text();
                    }
                }
            } // parsing function call arguments
            isFunction = true;
        }
    }
    if(!isFunction)
        return false;

    // Update dependency graph based on return value and parameters found:
    updateGraph(maxStatementId, returnVariable, dependsOnList);


// Write a parallel version of a for loop to the testPar.cpp:
fOut << endl;


//TODO: defined f in testPar.cpp


fOut << endl << "int tempRank = 1;" << endl; // TODO: Who will be executing the function
fOut << "// Rank 0 sends arguments to other rank:" << endl;
fOut << "if(rank == 0){" << endl;

fOut << "    // Array storing data for the rank executing the function:" << endl;
fOut << "    char* array = (char *) malloc(MAX_BYTES);" << endl;
fOut << "    int nArray = 0; // length of the array" << endl;
fOut << sPushParameters << endl;

fOut << "    // Send portion of data to processing element of rank number tempRank:" << endl;
fOut << "    cout << \"Rank 0 sending function call parameters to rank 1...\" << endl;" << endl;
fOut << "    MPI_Send(array, nArray, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD);" << endl;
fOut << "    cout << \"Rank 0 sent function call parameters to rank 1.\" << endl;" << endl;
fOut << "}else{" << endl;
fOut << "    // Only tempRank should be receiving the data" << endl;
fOut << "    // and executing the function:" << endl;
fOut << "    if(rank == tempRank){" << endl;
fOut << "       // Rank receives arguments prepared for it:" << endl;
fOut << "       char* array = (char *) malloc(MAX_BYTES); // array storing portion of data for this rank" << endl;
fOut << "       int nArray = 0; // length of the array" << endl;
//fOut << "       MPI_Request requestRecvFrom0; // for non-blocking receiving" << endl;
//fOut << "       MPI_Status statusRecvFrom0; // for waiting on receiving to finish" << endl;
fOut << "       cout << \"  Rank 1 receiving function call parameters from rank 0...\" << endl;" << endl;
fOut << "       MPI_Recv(array, MAX_BYTES, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // maximum MAX_BYTES characters" << endl;

fOut << sPopParameters << endl;
//TODO:
fOut << "       cout << \"  Rank 1 received function call parameters from rank 0.\" << endl;" << endl;
//fOut << "       cout << \"  Rank 1 received function call parameters from rank 0 (\" << a << \", \" << b << \").\" << endl;" << endl;
fOut << "   }" << endl;
fOut << "}" << endl;
fOut << endl;

fOut << "MPI_Barrier(MPI_COMM_WORLD);" << endl;

fOut << "cout << \"Rank \" << rank << \" in the middle.\" << endl;" << endl;


fOut << endl;
fOut << "// Only rank 1 sends result of a function call to rank 0:" << endl;
fOut << "if(rank == tempRank){" << endl;

fOut << "    cout << \"Rank 1 EXECUTING A FUNCTION CALL.\" << endl;" << endl;
// Copy the line from input file into the output file, e.g. c = f(a, b);:
fOut << fileLine << endl;

fOut << "    // Array storing data for the result of a function call:" << endl;
fOut << "    char* array = (char *) malloc(MAX_BYTES);" << endl;
fOut << "    int nArray = 0; // length of the array" << endl;
fOut << "    // Send results to rank number 0:" << endl;
fOut << "    cout << \"  Rank 1 sending results of a function call to rank 0...\" << endl;" << endl;


fOut << "    PUSH(" << returnVariable << ");" << endl;

fOut << "    MPI_Send(array, nArray, MPI_CHAR, 0, 0, MPI_COMM_WORLD);" << endl;
fOut << "    cout << \"  Rank 1 sent results of a function call to rank 0.\" << endl;" << endl;
fOut << "}else{" << endl;
fOut << "    // Only rank 0 should be receiving the result:" << endl;
fOut << "    if(!rank){" << endl;
fOut << "        // Array storing data for the result of a function call:" << endl;
fOut << "        char* array = (char *) malloc(MAX_BYTES);" << endl;
fOut << "        int nArray = 0; // length of the array" << endl;
fOut << "        // Start receiving results:" << endl;
fOut << "        cout << \"Rank 0 collecting results of a function call from rank 1...\" << endl;" << endl;
fOut << "        MPI_Recv(array, MAX_BYTES, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // maximum MAX_BYTES characters" << endl;
fOut << "        POP(" << returnVariable << ");" << endl;
fOut << "        cout << \"Rank 0 collected results of a function call from rank 1 (\" << " << returnVariable << " << \").\" << endl;" << endl;
fOut << "        //nArrayResults[tempRank] = deserialize_data(arrayInput);" << endl;
fOut << "    }" << endl;
fOut << "}" << endl;
fOut << endl;

    return isFunction;
}

void parseExpression(ofstream &fOut, string fileLine,
                     const int maxStatementId){
    // Expression is being parsed:

    // Example line from file: s="p=q+r"
    //cout << "Parsing file line: " << s << endl;

    // TODO: skip input file statements of no interest
    // for now, such do not exist.

    cout << "Parsing expression " << fileLine << endl;

    // Copy the line from input file into the output file, e.g. c = f(a, b);:
    if(PARALLELIZE)
        fOut << fileLine << endl;

    vector<string> dependsOnList;

    // Call the parser on the expression.
    // results are stored in Parser::_lhsToken
    // and the vector<string> dependsOnList 
    parse(fileLine, dependsOnList);

    updateGraph(maxStatementId, Parser::_lhsToken, dependsOnList);
}

void parallelizeLoop(ifstream &fIn, ofstream &fOut,
                     const string varName, const int val1, const int val2){
    // Write a parallel version of a for loop to the testPar.cpp:
    
    string fileLine;
    
    fOut << endl;
    fOut << "    char* array[MAX_RANKS]; // array storing portions of data for each rank" << endl;
    fOut << "    int nArray[MAX_RANKS]; // length of each array" << endl;
    fOut << "    for(int tempRank = 0; tempRank < nRanks; tempRank++)" << endl;
    fOut << "        array[tempRank] = NULL;" << endl;
    fOut << "    if(rank == 0){ // only rank 0 sends pieces of data to other ranks" << endl;
    fOut << "        // Sending & receiving are all non-blocking:" << endl;
    fOut << "        MPI_Request requestSend[MAX_RANKS]; // for non-blocking sending" << endl;
    fOut << "        MPI_Status statusSend[MAX_RANKS]; // for waiting on sending to finish" << endl;

    fOut << "        for(int tempRank = 1; tempRank < nRanks; tempRank++){" << endl;
    fOut << "            // Send portion of data to processing element of rank number tempRank:" << endl;
    fOut << "            MPI_Isend(array[tempRank], nArray[tempRank], MPI_CHAR, tempRank, 0, MPI_COMM_WORLD, &requestSend[tempRank]);" << endl;
    fOut << "        }" << endl;
    fOut << "        for(int tempRank = 1; tempRank < nRanks; tempRank++){" << endl;
    fOut << "            MPI_Wait(&requestSend[tempRank], &statusSend[tempRank]); // waiting for receiving to finish" << endl;
    fOut << "        }" << endl;

    fOut << "    }else{" << endl;
    fOut << endl;

    fOut << "        // Each rank receives a portion of data prepared for it:" << endl;
    fOut << "        char* arrayInput; // array storing portion of data for this rank" << endl;
    fOut << "        int nArrayInput = 0; // length of the array" << endl;

    fOut << "        MPI_Request requestRecvFrom0; // for non-blocking receiving" << endl;
    fOut << "        MPI_Status statusRecvFrom0; // for waiting on receiving to finish" << endl;
    fOut << "        MPI_Irecv(arrayInput, MAX_BYTES, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &requestRecvFrom0); // maximum MAX_BYTES characters" << endl;
    fOut << "        MPI_Wait(&requestRecvFrom0, &statusRecvFrom0); // waiting for receiving to finish" << endl;
    fOut << "        // determine number of received bytes:" << endl;
    fOut << "        MPI_Get_count(&statusRecvFrom0, MPI_CHAR, &nArrayInput);" << endl;
    fOut << "        // The array is received. Deserialize the data," << endl;
    fOut << "        // and get number of parsed bytes from received array:" << endl;
    fOut << "        //nArrayInput = deserialize_data(arrayInput);" << endl;
    fOut << "    }" << endl;
    fOut << endl;

    // Split the for loop range onto nRanks ranks:
    fOut << "    int minValue = (" << val2 << " - " << val1 << ") / nRanks * rank;" << endl;
    fOut << "    int maxValue = (" << val2 << " - " << val1 << ") / nRanks * (rank + 1);" << endl;
    fOut << "    if(maxValue > " << val2 << ")" << endl;
    fOut << "        maxValue = " << val2 << ";" << endl;
    fOut << endl;
    fOut << "    cout << \"Rank \" << rank << \" processing range \" << minValue << \"...\" << maxValue-1 << endl;" << endl;

    fOut << "    for(int " << varName << " = minValue; " << varName << " < maxValue; " << varName << "++){" << endl;

    getline(fIn, fileLine);
    updateOpenedBrackets(fileLine);
    fOut << fileLine << endl;

    getline(fIn, fileLine); // "}"
    updateOpenedBrackets(fileLine);
    fOut << "    }" << endl;

    fOut << endl;
    fOut << "    char* arrayResult = (char *) malloc(MAX_BYTES); // array storing results from this rank" << endl;
    fOut << "    int nArrayResult = MAX_BYTES; // length of the array" << endl;
    fOut << "    if(rank){ // only rank 0 doesn't send piece of data to rank zero" << endl;
    fOut << "        MPI_Request requestSendResult; // for non-blocking sending" << endl;
    fOut << "        MPI_Status statusSendResult; // for waiting on sending to finish" << endl;
    fOut << "        // Send portion of results to processing element of rank number 0:" << endl;
    fOut << "        MPI_Isend(arrayResult, nArrayResult, MPI_CHAR, 0, 0, MPI_COMM_WORLD, &requestSendResult);" << endl;
    fOut << "        MPI_Wait(&requestSendResult, &statusSendResult); // waiting for sending to finish" << endl;
    fOut << "    }else{" << endl;
    fOut << "        // only rank 0 receives pieces of data from other ranks" << endl;
    fOut << "        MPI_Request requestRecvResults[MAX_RANKS]; // for non-blocking receiving" << endl;
    fOut << "        MPI_Status statusRecvResults[MAX_RANKS]; // for waiting on receiving to finish" << endl;
    fOut << "        int nArrayResults[MAX_RANKS]; // length of each array" << endl;
    fOut << endl;
    fOut << "        // Start receiving results from all ranks:" << endl;
    fOut << "        for(int tempRank = 1; tempRank < nRanks; tempRank++){" << endl;
    fOut << "            char* arr = (char *) malloc(MAX_BYTES);" << endl;
    fOut << "            MPI_Irecv(arr, MAX_BYTES, MPI_CHAR, tempRank, 0, MPI_COMM_WORLD, &requestRecvResults[tempRank]); // maximum MAX_BYTES characters" << endl;
    fOut << "        }" << endl;
    fOut << "        // Wait until receiving results from each rank has finished:" << endl;
    fOut << "        for(int tempRank = 1; tempRank < nRanks; tempRank++){" << endl;
    fOut << "            MPI_Wait(&requestRecvResults[tempRank], &statusRecvResults[tempRank]); // waiting for receiving to finish" << endl;
    fOut << "            // determine number of received bytes:" << endl;
    fOut << "            MPI_Get_count(&statusRecvResults[tempRank], MPI_CHAR, &nArrayResults[tempRank]);" << endl;
    fOut << "            // The array is received. Deserialize the data," << endl;
    fOut << "            // and get number of parsed bytes from received array:" << endl;
    fOut << "            //nArrayResults[tempRank] = deserialize_data(arrayInput);" << endl;
    fOut << "        }" << endl;
    fOut << "    }" << endl;
    fOut << endl;
}

void detectDependenciesInLoop(ifstream &fIn, ofstream &fOut,
                              string &fileLine, int &maxStatementId,
                              const int increment,
                              int &loopMin, int &loopMax,
                              const string varName,
                              const int val1, const int val2){
    getline(fIn, fileLine);
    updateOpenedBrackets(fileLine);

    // Defining a vector of strings to store statements inside for loop:
    vector <string> forLoopStatements;
    // Loop until the end of for loop
    while(fileLine.find('}') == std::string::npos){
        forLoopStatements.push_back(fileLine);
        getline(fIn, fileLine);
        updateOpenedBrackets(fileLine);
    }

    // Detect loop range:
    loopMin = maxStatementId + 1;

    // Loop to parse the statements inside the "for loop"  
    int i = val1;
    while(i*increment < val2*increment){
        for(string x:forLoopStatements){
            maxStatementId++;
            // Replace varName with the value of i:
            string toParse = std::regex_replace(x, std::regex(varName), to_string(i));
            parseExpression(fOut, toParse, maxStatementId);
        }
        i += increment;
    }
    
    loopMax = maxStatementId;
    cout << endl << "Loop range: " << loopMin << "-" << loopMax << endl << endl;
}

void parseFunctionOrVariableDefinition(Functions &f,
                                       string &functionName,
                                       string fileLine, int maxStatementId,
                                       ifstream &fIn, ofstream &fOut,
                                       int parallelize){
    // The assumption is that two types of statements
    // can start with a primitive type keyword, e.g.:
    // 1) int a, b; - definition of variables
    // 2) int f(int a){ - function definition

    functionName = "";

    std::istringstream ist{fileLine};
    Lexer* p_lexer = new Lexer{ist};
    string type = p_lexer->get_token_text();
    if(VERBOSE)
        cout << "Primitive type " << type << " found." << endl;

    // Parse the next word, i.e. variable name or function name:
    p_lexer->advance();
    string name = p_lexer->get_token_text();
    if(VERBOSE)
        cout << "Name " << name << " found." << endl;

    p_lexer->advance();
    string word = p_lexer->get_token_text();

    // If a comma or semicomma is found, it is assumed that
    // fileline containst variable definition:
    if( (word == ",") || (word == ";") ){
        cout << "Skipping line with variable definition." << endl;
        if(parallelize){
            // Copy the line from input file into the output file
            fOut << fileLine << endl;
        }
        return;
    }

    // The assumption is that function call is detected,
    // or variable definition, e.g. int a = 1;.
    // If not, report an error:
    if(word == "="){
        cout << "Skipping line with variable definition and initialization.\n";
        if(parallelize){
            // Copy the line from input file into the output file
            fOut << fileLine << endl;
            return;
        }
    } else if(word != "("){
        cout << "'(' expected, but " << word
             << " found. Error in parsing!!!" << endl << endl;
        if(parallelize){
            // Copy the line from input file into the output file
            fOut << fileLine << endl;
        }
        return;
    }

    cout << "Parsing function " << name << "." << endl;
    
    if(name == "main")
        inMain = true;
        
    // When searching for functions called from this function,
    // one should know which function is being parsed:
    f.addFunction(name);
    Functions::currentFunction = name;
    
    // Create a separate dependency graph for current function:
    Graph newGraph;
    Graph::graphs.push_back(newGraph);
    Graph::iCurrentGraph = Graph::graphs.size()-1;
    if(VERBOSE)
        cout << "iCurrentGraph = " << Graph::iCurrentGraph << endl;
    
    // Create a separate set of variables for current function:
    Variables newVariables;
    Variables::varSet.push_back(newVariables);
    Variables::iCurrentVarSet = Variables::varSet.size()-1;
    if(VERBOSE)
        cout << "iCurrentVarSet = " << Variables::iCurrentVarSet << endl;

    if(parallelize){
        fOut << fileLine << endl;
        if(name == "main"){
            if(VERBOSE)
                cout << "Initializing MPI." << endl;
            fOut << "\n\
    int rank, nRanks;\n\
    MPI_Init(NULL, NULL);\n\
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);\n\
    MPI_Comm_size(MPI_COMM_WORLD, &nRanks);\n\
    ";
        }
    }
}

bool overlap(const std::set<string>& s1, const std::set<string>& s2)
{
    for( const auto& s : s1) {
        if(std::binary_search(s2.begin(), s2.end(), s))
            return true;
    }
    return false;
}

// Detecting whether the statements in a for loop are independent:   
bool checkLoopDependency(int &loopMin, int &loopMax){
    Graph &g = Graph::graphs[Graph::iCurrentGraph];

    // Current set of variables:    
    Variables &v = Variables::varSet[Variables::iCurrentVarSet];

    // Find the list of variables that are written to in the for loop:
    set<string> writeVars;
    for(int iStatement = loopMin; iStatement < loopMax; iStatement++){
        for(auto &var: v.vars){
            // For this variable, check if it dependens on anything:
            for(int i = 0; i < v.vars.size(); i++){
                // Try to find whether the dependency
                // between var and vars[i] exists:
                bool found = false;
                for(auto &dep : g.dependencies){
                    if(dep == Dependency(var, v.vars[i], 0)){
                        found = dep.statementIdInStatementRangeExists(loopMin, loopMax);
                        if(found){
                            //cout << "Found" << endl;
                            //cout << var;
                            //cout << endl;
                            writeVars.insert(var.getName());
                        }
                        break;
                    }
                }
            }
        }
    }

    cout << "Variables that are written within a range:" << endl;    
    for(auto &var : writeVars){
        cout << var << endl;
    }

    // No variables from the writeVars are allowed to figure on the right side of any statement.
    // It should be allowed that from 10 statements of a for loop, 
    // a statement may depend from previous belonging to the same iteration.
    set<string> readVars;
    for(int iStatement = loopMin; iStatement < loopMax; iStatement++){
        for(auto &var: v.vars){
            // For this variable, check if any variable depends on it:
            for(int i = 0; i < v.vars.size(); i++){
                // Try to find whether the dependency
                // between vars[i] and var exists:
                bool found = false;
                for(auto &dep : g.dependencies){
                    if(dep == Dependency(v.vars[i], var, 0)){
                        //found = dep.statementIdInStatementRangeExists(loopMin, loopMax);
                        found = dep.statementIdInStatementRangeExists(loopMin, loopMax);
                        if(found){
                            //cout << "Found" << endl;
                            //cout << var;
                            //cout << endl;
                            readVars.insert(var.getName());
                        }
                        break;
                    }
                }
            }
        }
    }

    cout << "Variables that are read within a range:" << endl;    
    for(auto &var : readVars){
        cout << var << endl;
    }

    // If a variable is both read and written to within a loop:
    if( overlap(writeVars, readVars) ){
        return false;
    }else{
        return true;
    }
}

void parseLoop(string fileLine, int maxStatementId,
               int &loopMin, int &loopMax, Lexer* &p_lexer,
               ifstream &fIn, ofstream &fOut, int parallelize){
    // For loop is detected.

    string word,str,varName,value,varName2,value2,sec3;
    bool increment=1;
    // The assumption is that loops are in the following form:
    // for (var = 0; var < 5; var++){}
    // There will be no checking of sign. Sign < is expected.
    // There will be no checking of operator at the end. Operator ++ is expected.
    // There will be no checking for expressions. Pure variable names are expected.
    cout<<endl<<"for loop is detceted"<<endl;

    word = fileLine;
    word.erase(std::remove_if(word.begin(), word.end(), ::isspace),word.end());
    int size = word.find("(");
    
    //section 1: in "for loop"
    str=word.substr(size+1,word.length());
    
    //variable intialization
    size=str.find("=");
    varName=str.substr(0,size);
    cout<<endl<<"varName "<<varName<<endl;
    str=str.substr(size+1,str.length());

    value=str.substr(0,str.find(";"));
    cout<<endl<<"value "<<value<<endl;

    //section 2: in "for loop"
    str=str.substr(str.find(";")+1,str.length());
    if(str.find("<")!=std::string::npos){
    size=str.find("<");
    varName2=str.substr(0,size);
    cout<<endl<<"varName condition "<<varName2<<endl;
    
    size=str.find("<");
    str=str.substr(size+1,str.length());
    }
    else
    {
        //change the status of "for loop" to decrement
        increment=0;
    size=str.find(">");
    varName2=str.substr(0,size);
    cout<<endl<<"varName condition "<<varName2<<endl;
    
    size=str.find(">");
    str=str.substr(size+1,str.length());

    }
    value2=str.substr(0,str.find(";"));
    cout<<endl<<"value "<<value2<<endl;

    //section 3: in "for loop"
    str.erase(std::remove_if(str.begin(), str.end(), ::isspace),
        str.end());
    
    str=str.substr(str.find(";")+1,str.length()-1);
    //cout<<endl<<str<<endl;
    
    size=str.find(')');
    //cout<<endl<<size<<endl;
    sec3=str.substr(0,size);
    cout<<endl<<sec3<<endl;
    
    if(sec3.find("+"))
    cout<<endl<<"increment"<<endl;
    else
    increment=0;

    int val1 = stoi(value);
    int val2 = stoi(value2);
    cout << endl << "For loop (" << varName << " " << val1 << ".." << val2-1 << ") found..." << endl;
    
    getline(fIn, fileLine);
    string toParse=fileLine;
    //define array of strings to store statements inside for loop
    vector <string> myvector;
    //loop  until reach the end of for loop
    while(toParse.find('}')== std::string::npos){
        myvector.push_back(toParse);
        getline(fIn, fileLine);
        toParse=fileLine;
        //erase spaces to check the closing bracket
        toParse.erase(std::remove_if(toParse.begin(), toParse.end(), ::isspace),
        toParse.end());
    }
    //loop to parse the statements inside the "for loop"  
    int i=val1;
    if(increment){
    while(i<val2){
    for(string x:myvector){
    maxStatementId++;
    x = std::regex_replace(x, std::regex(varName), to_string(i));
    parseExpression(fOut,x, maxStatementId);
    }
    i++;
    }
    }
    else
    {
    while(i>val2){
    for(string x:myvector){
    maxStatementId++;
    x = std::regex_replace(x, std::regex(varName), to_string(i));
    parseExpression(fOut,x, maxStatementId);
    }
    i--;
    }
    }
   /* int val1 = stoi(value1);
    int val2 = stoi(value2);
    cout << endl << "For loop (" << varName << " " << val1 << ".." << val2-1 << ") found..." << endl;

    if(PARALLELIZE){
        parallelizeLoop(fIn, fOut, varName, val1, val2);
    }else{
        // Dependency checking:
        detectDependenciesInLoop(fIn, fOut, fileLine, maxStatementId,
                                 increment, loopMin, loopMax,
                                 varName, val1, val2);
    }

    if(!parallelize){
         // Detecting whether the statements in a for loop are independent:   
        if( !checkLoopDependency(loopMin, loopMax) ){
            cout << "Overlapping sets - cannot parallelize." << endl << endl;
        }else{
            cout << "Sets are not overlapping. Parallelization is possible." << endl << endl;
        }
    }*/
}

// While parsing the input file,
// detect lines that contain comments and defines,
// or anything starting with non-alpha char
// if } is detected, and the currently parsed function is main(),
// before closing main(), MPI should be finalized:
bool nonAlpha(ofstream &fOut, string &fileLine, bool &inMain, int parallelize){
    // Skip lines that don't start with a letter after optional spaces.
    int iFileLine = 0;
    while((iFileLine < fileLine.length()) && isspace(fileLine[iFileLine]))
        iFileLine++;
    if(!isalpha(fileLine[iFileLine])){
        if(fileLine[iFileLine] == '}'){
            cout << "} found, numOpenedBrackets = " << numOpenedBrackets << endl;

            // Add MPI_Finalize(); MPI directive at the end of the main:
            if(inMain){
                fOut << "    MPI_Finalize();" << endl;
                inMain = false;
            }
        }else{
            cout << "Skipping empty or non-alpha starting line." << endl;
        }
        if(parallelize)
            fOut << fileLine << endl;
        return true;
    }
    return false;
}

void parseWhile(string &word, Lexer *p_lexer){
    // While loop is detected.
    // Parse elements of a while loop, i.e. variable name and condition:
    p_lexer->advance();
    word = p_lexer->get_token_text();
    //cout << endl << "word ('(' expected): '" << word << "'" << endl << endl;

    p_lexer->advance();
    string varName = p_lexer->get_token_text();
    //cout << endl << "word (variable name expected): '" << varName << "'" << endl << endl;
    
    p_lexer->advance();
    string sign = p_lexer->get_token_text();
    //cout << endl << "word (sign expected): '" << sign << "'" << endl << endl;
    
    p_lexer->advance();
    string value1 = p_lexer->get_token_text();
    //cout << endl << "word (value expected): '" << value1 << "'" << endl << endl;
    
    cout << endl << "While loop (" << varName << " " << sign << " " << value1 << ") found..." << endl;
}

bool primitiveType(string &word){
    return (word == "int") || (word == "bool") || (word == "double")
                || (word == "float") || (word == "double")
                || (word == "char") || (word == "string") || (word == "void");
}

void parseInputFileLine(ifstream &fIn, ofstream &fOut,
                        Functions &f,
                        string &functionName,
                        string &fileLine, int &maxStatementId,
                        bool &inMain, int parallelize){

    // Skip empty lines:
    if(fileLine == "")
        return;
        
    // When { is detected, increse the number of open brackets
    // When } is detected, reduce number of open brackets,
    // and act if the function is main:
    updateOpenedBrackets(fileLine);

    // Increase global statement ID,
    // so that the dependencies formed in this line are associated a new ID:
    maxStatementId++;

    // Get first word in order to determine
    // whether a loop starts in this line:
    cout << endl << "#" << maxStatementId << ": " << fileLine << ". Parsing..." << endl;

    if( nonAlpha(fOut, fileLine, inMain, PARALLELIZE) )
        return;

    std::istringstream ist{fileLine};
    Lexer* p_lexer = new Lexer{ist};
    string word = p_lexer->get_token_text();
    //cout << "Word '" << word << "' found." << endl;

    // If function definition is detected:
    if( primitiveType(word) ){
        parseFunctionOrVariableDefinition(f, functionName,
                                          fileLine, maxStatementId,
                                          fIn, fOut, parallelize);
    }else if(word == "for"){ // if "for" loop is detected
        int loopMin, loopMax; // loop range in statement IDs
        parseLoop(fileLine, maxStatementId, loopMin, loopMax,
                  p_lexer, fIn, fOut, parallelize);
    }else if(word == "while"){
        parseWhile(word, p_lexer);
    }else if(fileLine.find("=") == std::string::npos){
        cout << "Skipping line " << fileLine << endl;
        if(parallelize){
            // Copy the line from input file into the output file
            fOut << fileLine << endl;
        }
    }else{ // Expression or function:
        bool isFunction = parseFunctionCall(f, fOut,
                                            fileLine, maxStatementId);
        if(!isFunction)
            parseExpression(fOut, fileLine, maxStatementId);
    } // if "for" loop, or ..., or an expression
}

int main(){
    // Statements are numerated.
    // Each time a statement is parsed, it gets a new statement id.
    int maxStatementId = 0;

    inMain = false; // Whether currently parsed function is main()
    
    Functions f;

    ifstream fIn("test.cpp"); // input file stream

    // If PARALLELIZE is set, output file should be opened for writing:
    ofstream fOut; // output file stream
    if(PARALLELIZE)
        fOut.open("testPar.cpp", std::fstream::out);

    cout << endl << endl << "Parsing input file test.cpp" << endl;

    if(PARALLELIZE)
        addDefinesAndIncludes(fOut);

    numOpenedBrackets = 0;

    string functionName = ""; // name of the function that is currently parsed
    
    string fileLine; // a string containing a line read from the input file
    while( getline(fIn, fileLine) ){
        parseInputFileLine(fIn, fOut, f,
                           functionName,
                           fileLine, maxStatementId,
                           inMain, PARALLELIZE);
    }

    fIn.close(); // closing the input file

    if(PARALLELIZE)
        fOut.close(); // closing the output file
    
    cout << endl << endl << "Results:" << endl;

    // Current set of variables:    
    Variables &v = Variables::varSet[Variables::iCurrentVarSet];

    // Printing all variables:
    //if(VERBOSE)
        v.printDetailed();

    // Printing dependencies in a form of a vector of dependencies:
    if(VERBOSE)
        Graph::graphs[Graph::iCurrentGraph].print();

    // Printing dependencies in a form of a dependency graph:
    Graph::graphs[Graph::iCurrentGraph].printTable();

    f.print();
    
    return 0;
}

