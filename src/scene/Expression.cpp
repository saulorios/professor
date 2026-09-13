#include "Expression.h"

#include <cmath>
#include <limits>

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kE = 2.718281828459045;

struct Token {
    enum class Kind { Number, Variable, Constant, Function, Operator, Open, Close, End };
    Kind kind = Kind::End;
    double value = 0.0;
    int func = 0;
    QChar op;
};

// Nomes reconhecidos numa sequência de letras (o maior casamento ganha:
// "exp" antes de "e", "log2" antes de "log")
struct Name {
    const char *text;
    Token::Kind kind;
    int func;       // índice em Expression::Func (só funções)
    double value;   // só constantes
};

const Name kNames[] = {
    {"asin", Token::Kind::Function, 3, 0}, {"acos", Token::Kind::Function, 4, 0},
    {"atan", Token::Kind::Function, 5, 0}, {"sqrt", Token::Kind::Function, 6, 0},
    {"log2", Token::Kind::Function, 11, 0}, {"floor", Token::Kind::Function, 12, 0},
    {"ceil", Token::Kind::Function, 13, 0}, {"sin", Token::Kind::Function, 0, 0},
    {"sen", Token::Kind::Function, 0, 0},  {"cos", Token::Kind::Function, 1, 0},
    {"tan", Token::Kind::Function, 2, 0},  {"tg", Token::Kind::Function, 2, 0},
    {"abs", Token::Kind::Function, 7, 0},  {"exp", Token::Kind::Function, 8, 0},
    {"ln", Token::Kind::Function, 9, 0},   {"log", Token::Kind::Function, 10, 0},
    {"pi", Token::Kind::Constant, 0, kPi}, {"e", Token::Kind::Constant, 0, kE},
    {"x", Token::Kind::Variable, 0, 0},
};

bool tokenize(const QString &text, std::vector<Token> *tokens, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    for (qsizetype i = 0; i < text.size();) {
        const QChar c = text[i];
        if (c.isSpace()) {
            ++i;
            continue;
        }
        Token t;
        if (c.isDigit() || (c == '.' && i + 1 < text.size() && text[i + 1].isDigit())) {
            qsizetype j = i;
            while (j < text.size() && (text[j].isDigit() || text[j] == '.'))
                ++j;
            bool ok = false;
            t.kind = Token::Kind::Number;
            t.value = text.mid(i, j - i).toDouble(&ok);
            if (!ok)
                return fail(QString("número inválido \"%1\"").arg(text.mid(i, j - i)));
            tokens->push_back(t);
            i = j;
            continue;
        }
        if (c.isLetter() && c != QChar(0x03C0)) {
            // Uma sequência de letras pode juntar vários nomes: "2xsen(x)", "xpi"
            const QString word = text.mid(i).toLower();
            const Name *best = nullptr;
            for (const Name &name : kNames) {
                const QString candidate = QString::fromLatin1(name.text);
                if (word.startsWith(candidate) && (!best || candidate.size() > qsizetype(qstrlen(best->text))))
                    best = &name;
            }
            if (!best)
                return fail(QString("nome desconhecido em \"%1\" (use x, pi, e e funções como sin, sqrt, log)")
                                .arg(text.mid(i)));
            t.kind = best->kind;
            t.func = best->func;
            t.value = best->value;
            tokens->push_back(t);
            i += qsizetype(qstrlen(best->text));
            continue;
        }
        switch (c.unicode()) {
        case 0x03C0: // π
            t.kind = Token::Kind::Constant;
            t.value = kPi;
            break;
        case 0x221A: // √
            t.kind = Token::Kind::Function;
            t.func = 6;
            break;
        case 0x00B2: // ²
        case 0x00B3: { // ³
            Token pow;
            pow.kind = Token::Kind::Operator;
            pow.op = '^';
            tokens->push_back(pow);
            t.kind = Token::Kind::Number;
            t.value = c.unicode() == 0x00B2 ? 2.0 : 3.0;
            break;
        }
        case '(': case '[': t.kind = Token::Kind::Open; break;
        case ')': case ']': t.kind = Token::Kind::Close; break;
        case '+': case '-': case '*': case '/': case '^':
            t.kind = Token::Kind::Operator;
            t.op = c;
            break;
        case 0x2212: t.kind = Token::Kind::Operator; t.op = '-'; break; // −
        case 0x00B7: case 0x00D7: t.kind = Token::Kind::Operator; t.op = '*'; break; // · ×
        case 0x00F7: t.kind = Token::Kind::Operator; t.op = '/'; break; // ÷
        default:
            return fail(QString("caractere inesperado '%1'").arg(c));
        }
        tokens->push_back(t);
        ++i;
    }
    tokens->push_back(Token{});
    return true;
}

// Descida recursiva que já emite o programa em notação polonesa reversa
struct Emitter {
    explicit Emitter(const std::vector<Token> &list) : tokens(list) {}

    const std::vector<Token> &tokens;
    std::size_t pos = 0;
    QString error;
    // (op, valor, função) — espelha Expression::Instruction sem expor o tipo privado
    struct Out {
        int op;
        double value;
        int func;
    };
    std::vector<Out> code;

    const Token &peek() const { return tokens[pos]; }
    bool isOp(QChar c) const { return peek().kind == Token::Kind::Operator && peek().op == c; }
    bool startsPrimary() const
    {
        const Token::Kind k = peek().kind;
        return k == Token::Kind::Number || k == Token::Kind::Variable || k == Token::Kind::Constant
               || k == Token::Kind::Function || k == Token::Kind::Open;
    }

    // op: 0 número, 1 variável, 2 +, 3 −, 4 *, 5 /, 6 ^, 7 neg, 8 função
    bool expression()
    {
        if (!term())
            return false;
        while (isOp('+') || isOp('-')) {
            const bool add = isOp('+');
            ++pos;
            if (!term())
                return false;
            code.push_back({add ? 2 : 3, 0, 0});
        }
        return true;
    }

    bool term()
    {
        if (!unary())
            return false;
        for (;;) {
            if (isOp('*') || isOp('/')) {
                const bool mul = isOp('*');
                ++pos;
                if (!unary())
                    return false;
                code.push_back({mul ? 4 : 5, 0, 0});
            } else if (startsPrimary()) {
                // Multiplicação implícita: 2x, 3(x+1), x sin(x)
                if (!power())
                    return false;
                code.push_back({4, 0, 0});
            } else {
                return true;
            }
        }
    }

    bool unary()
    {
        if (isOp('-') || isOp('+')) {
            const bool negate = isOp('-');
            ++pos;
            if (!unary())
                return false;
            if (negate)
                code.push_back({7, 0, 0});
            return true;
        }
        return power();
    }

    bool power()
    {
        if (!primary())
            return false;
        if (isOp('^')) {
            ++pos;
            if (!unary()) // associativa à direita e aceita x^-1
                return false;
            code.push_back({6, 0, 0});
        }
        return true;
    }

    bool primary()
    {
        const Token t = peek();
        switch (t.kind) {
        case Token::Kind::Number:
        case Token::Kind::Constant:
            ++pos;
            code.push_back({0, t.value, 0});
            return true;
        case Token::Kind::Variable:
            ++pos;
            code.push_back({1, 0, 0});
            return true;
        case Token::Kind::Function:
            ++pos;
            // sin(x), sen x, √x: o argumento liga mais forte que a multiplicação
            if (!power())
                return false;
            code.push_back({8, 0, t.func});
            return true;
        case Token::Kind::Open:
            ++pos;
            if (!expression())
                return false;
            if (peek().kind != Token::Kind::Close) {
                error = "parêntese sem fechar";
                return false;
            }
            ++pos;
            return true;
        default:
            error = t.kind == Token::Kind::End ? "a expressão terminou antes da hora" : "termo esperado";
            return false;
        }
    }
};

} // namespace

bool Expression::parse(const QString &text, QString *error)
{
    m_code.clear();
    std::vector<Token> tokens;
    if (!tokenize(text, &tokens, error))
        return false;
    Emitter emitter(tokens);
    if (!emitter.expression() || emitter.peek().kind != Token::Kind::End) {
        if (error)
            *error = emitter.error.isEmpty() ? QString("sobrou texto depois da expressão") : emitter.error;
        return false;
    }
    m_code.reserve(emitter.code.size());
    for (const Emitter::Out &out : emitter.code) {
        Instruction instruction;
        instruction.op = Op(out.op);
        instruction.value = out.value;
        instruction.func = Func(out.func);
        m_code.push_back(instruction);
    }
    return true;
}

double Expression::evaluate(double x) const
{
    constexpr int kMaxStack = 64;
    double stack[kMaxStack];
    int top = 0;
    for (const Instruction &in : m_code) {
        switch (in.op) {
        case Op::Number:
        case Op::Variable:
            if (top >= kMaxStack)
                return std::numeric_limits<double>::quiet_NaN();
            stack[top++] = in.op == Op::Number ? in.value : x;
            break;
        case Op::Neg:
            stack[top - 1] = -stack[top - 1];
            break;
        case Op::Function: {
            double &v = stack[top - 1];
            switch (in.func) {
            case Func::Sin: v = std::sin(v); break;
            case Func::Cos: v = std::cos(v); break;
            case Func::Tan: v = std::tan(v); break;
            case Func::Asin: v = std::asin(v); break;
            case Func::Acos: v = std::acos(v); break;
            case Func::Atan: v = std::atan(v); break;
            case Func::Sqrt: v = std::sqrt(v); break;
            case Func::Abs: v = std::abs(v); break;
            case Func::Exp: v = std::exp(v); break;
            case Func::Ln: v = std::log(v); break;
            case Func::Log10: v = std::log10(v); break;
            case Func::Log2: v = std::log2(v); break;
            case Func::Floor: v = std::floor(v); break;
            case Func::Ceil: v = std::ceil(v); break;
            }
            break;
        }
        default: {
            const double b = stack[--top];
            double &a = stack[top - 1];
            switch (in.op) {
            case Op::Add: a += b; break;
            case Op::Sub: a -= b; break;
            case Op::Mul: a *= b; break;
            case Op::Div: a /= b; break;
            case Op::Pow: a = std::pow(a, b); break;
            default: break;
            }
        }
        }
    }
    return top == 1 ? stack[0] : std::numeric_limits<double>::quiet_NaN();
}
