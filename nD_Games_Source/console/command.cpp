#include "command.h"

#include <sstream>
#include <stdexcept>

namespace nd {
// Читає координати, потім необов'язкове перетворення та необов'язковий маршрут.
// Зайві слова й частково прочитані номери на кшталт "1abc" відхиляються.
Move parse_move(const std::string& text) {
    std::istringstream input(text);
    std::string from, to, token, direction, number, extra;
    if (!(input >> from >> to)) throw std::invalid_argument("Move requires FROM TO");
    Move move{Position::parse(from),Position::parse(to),std::nullopt};
    if (!(input >> token)) return move;
    // Якщо перше додаткове слово не "path", воно має позначати перетворення.
    if (token != "path") {
        if (token.size() != 1 || (token != "N" && token != "B" && token != "R"))
            throw std::invalid_argument("Promotion must be N, B or R");
        move.promotion = piece_type(token[0]);
        if (!(input >> token)) return move;
    }
    if (token != "path" || !(input >> direction >> number) || number.size() != 1
        || number[0] < '1' || number[0] > '8' || (input >> extra))
        throw std::invalid_argument("Expected path DIRECTION 1..8");
    move.path = PathSelection{direction,number[0]-'0'};
    (void)move.path->id();
    return move;
}
}
