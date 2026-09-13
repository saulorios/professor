#pragma once

#include <QString>

#include <vector>

// Expressão matemática de uma variável, lida uma vez e avaliada muitas vezes
// (comando "grafico"). Aceita o que uma IA costuma escrever:
//   números (2, 0.5), x, pi/π, e
//   + − * / ^ (e também ·, ×, − e ÷), parênteses, menos unário
//   multiplicação implícita: 2x, 3(x+1), (x-1)(x+1), 2sin(x), x pi
//   funções: sin sen cos tan tg asin acos atan sqrt √ abs exp ln log log2
//            floor ceil
// A potência é associativa à direita e vem antes do menos unário: -x^2 = -(x²).
// Resultado NaN fora do domínio (sqrt(-1), log(0)): o gráfico interrompe a curva.
class Expression
{
public:
    // Devolve false com `error` quando o texto não é uma expressão válida
    bool parse(const QString &text, QString *error = nullptr);
    bool isValid() const { return !m_code.empty(); }

    double evaluate(double x) const;

private:
    enum class Op { Number, Variable, Add, Sub, Mul, Div, Pow, Neg, Function };
    enum class Func { Sin, Cos, Tan, Asin, Acos, Atan, Sqrt, Abs, Exp, Ln, Log10, Log2, Floor, Ceil };

    // Programa em notação polonesa reversa: avaliar é só percorrer a lista
    struct Instruction {
        Op op;
        double value = 0.0;
        Func func = Func::Sin;
    };

    std::vector<Instruction> m_code;
};
