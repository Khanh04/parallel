#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <sstream>
#include <iostream>

double to_number(const std::string& s);
std::string to_string(double x);

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
std::ostream& operator<<(std::ostream& os, const Error<N>& e) {
    e.put(os);
    return os;
}

#endif // UTILS_H
