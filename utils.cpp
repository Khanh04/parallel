#include "utils.h"

double to_number(const std::string& s) {
    std::istringstream ist{s};
    ist.exceptions(std::ios_base::failbit);
    double x;
    ist >> x;
    return x;
}

std::string to_string(double x) {
    std::ostringstream ost;
    ost << x;
    return ost.str();
}
