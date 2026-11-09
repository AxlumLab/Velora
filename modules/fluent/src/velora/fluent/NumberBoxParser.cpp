// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Derived from WinUI 2.3 NumberBoxParser.cpp: bounded tokenization and operator-stack evaluation.
#include <charconv>
#include <cmath>
#include <vector>
#include <velora/fluent/NumberBoxParser.hpp>

namespace velora
{
namespace
{
int precedence(char op)
{
    return op == '^' ? 2 : (op == '*' || op == '/' ? 1 : 0);
}
bool apply(std::vector<f64>& values, char op)
{
    if (values.size() < 2)
        return false;
    const auto right = values.back();
    values.pop_back();
    auto& left = values.back();
    switch (op)
    {
    case '+':
        left += right;
        break;
    case '-':
        left -= right;
        break;
    case '*':
        left *= right;
        break;
    case '/':
        if (right == 0)
            return false;
        left /= right;
        break;
    case '^':
        left = std::pow(left, right);
        break;
    default:
        return false;
    }
    return std::isfinite(left);
}
} // namespace

std::optional<f64> NumberBoxParser::compute(std::string_view text, bool expression)
{
    // Bound memory and work for untrusted XAML/user text; no recursion or NUL-termination assumption.
    if (text.empty() || text.size() > 65536)
        return std::nullopt;
    std::vector<f64> values;
    std::vector<char> operators;
    bool expectNumber = true;
    usize offset = 0;
    while (offset < text.size())
    {
        const char c = text[offset];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v')
        {
            ++offset;
            continue;
        }
        if (expectNumber)
        {
            if (expression && c == '(')
            {
                operators.push_back(c);
                ++offset;
                continue;
            }
            f64 value = 0;
            const auto parsed = std::from_chars(text.data() + offset, text.data() + text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr == text.data() + offset || !std::isfinite(value))
                return std::nullopt;
            offset = static_cast<usize>(parsed.ptr - text.data());
            values.push_back(value);
            expectNumber = false;
        }
        else if (expression && c == ')')
        {
            while (!operators.empty() && operators.back() != '(')
            {
                if (!apply(values, operators.back()))
                    return std::nullopt;
                operators.pop_back();
            }
            if (operators.empty())
                return std::nullopt;
            operators.pop_back();
            ++offset;
        }
        else
        {
            if (!expression || (c != '+' && c != '-' && c != '*' && c != '/' && c != '^'))
                return std::nullopt;
            while (!operators.empty() && operators.back() != '(' &&
                   precedence(operators.back()) >= precedence(c))
            {
                if (!apply(values, operators.back()))
                    return std::nullopt;
                operators.pop_back();
            }
            operators.push_back(c);
            ++offset;
            expectNumber = true;
        }
    }
    if (expectNumber)
        return std::nullopt;
    while (!operators.empty())
    {
        if (!apply(values, operators.back()))
            return std::nullopt;
        operators.pop_back();
    }
    return values.size() == 1 ? std::optional<f64>(values.back()) : std::nullopt;
}
} // namespace velora
