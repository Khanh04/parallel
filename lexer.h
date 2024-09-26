#ifndef LEXER_H
#define LEXER_H

#include <istream>
#include <string>

enum class Token {
    Array, Id, Number, Sin, Cos, Tan, Asin, Acos, Atan, Log, Exp,
    Log10, Exp10, Sqrt, Int, Assign='=', Plus='+', Minus='-',
    Mul='*', Div='/', Mod='%', Pow='^', Lp='(', Rp=')', Eofsym=-1, 
};

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
    
    void advance();		// Read the next token in the stream.
    
private:
    std::istream* p_input;
    bool owns_input;
    Token current_token;
    std::string current_token_text;
    std::string token_buffer;

    void init();
    Token get_token();
    void exponent_part(char& c);
};

#endif // LEXER_H
