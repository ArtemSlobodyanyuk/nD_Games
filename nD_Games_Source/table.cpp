#include "table.h"

#include <stdexcept>
#include <utility>

namespace nd {
// Для перемикання черги та перевірки загроз потрібна протилежна сторона.
Color opposite(Color color) { return color == Color::White ? Color::Black : Color::White; }
// Єдина таблиця відповідності типів фігур і їхніх літерних позначень.
char symbol(PieceType type) {
    switch (type) {
    case PieceType::Soldier: return 'P';
    case PieceType::Rook: return 'R';
    case PieceType::Bishop: return 'B';
    case PieceType::Knight: return 'N';
    case PieceType::Stone: return 'L';
    case PieceType::Water: return 'W';
    case PieceType::Fire: return 'I';
    case PieceType::Air: return 'S';
    case PieceType::King: return 'K';
    case PieceType::Queen: return 'Q';
    }
    throw std::invalid_argument("Unknown piece type");
}
// Шукаємо літеру через ту саму таблицю, щоб два перетворення не розходилися.
PieceType piece_type(char code) {
    for (auto type : {PieceType::Soldier, PieceType::Rook, PieceType::Bishop,
                      PieceType::Knight, PieceType::Stone, PieceType::Water,
                      PieceType::Fire, PieceType::Air, PieceType::King, PieceType::Queen})
        if (symbol(type) == code) return type;
    throw std::invalid_argument("Unknown piece symbol");
}
// Перевірка виконується до доступу до масиву, щоб уникнути виходу за його межі.
bool Position::valid() const {
    return x >= 1 && x <= 8 && y >= 1 && y <= 8 && z >= 1 && z <= 8;
}
// Збіг двох координат недостатній: клітинки можуть належати різним шарам.
bool Position::operator==(const Position& p) const { return x == p.x && y == p.y && z == p.z; }
// Суворий формат відкидає пробіли, знаки, зайві символи та цифри 0 і 9.
Position Position::parse(const std::string& text) {
    if (text.size() != 3 || text.find_first_not_of("12345678") != std::string::npos)
        throw std::invalid_argument("Position must be exactly three digits in [1,8], e.g. 511");
    return {text[0] - '0', text[1] - '0', text[2] - '0'};
}
// Перетворення цифри на символ додає її значення до коду символу '0'.
std::string Position::str() const {
    if (!valid()) throw std::out_of_range("Position outside 8x8x8 board");
    return {char('0' + x), char('0' + y), char('0' + z)};
}
std::size_t Table::index(Position p) {
    if (!p.valid()) throw std::out_of_range("Position outside 8x8x8 board");
    // Перед шаром Z лежить (Z−1)*64 клітинок, перед рядом Y — ще (Y−1)*8.
    return static_cast<std::size_t>((p.z - 1) * 64 + (p.y - 1) * 8 + p.x - 1);
}
// Посилання const дозволяє прочитати клітинку без копіювання та зміни фігури.
const std::optional<Piece>& Table::at(Position p) const { return cells_[index(p)]; }
// Встановлення позиції перевіряє тип і колір, але не правила руху фігури.
void Table::put(Position p, Piece piece) {
    // Виклик потрібен для перевірки типу; саме позначення тут не використовується.
    (void)symbol(piece.type);
    if (piece.color != Color::White && piece.color != Color::Black)
        throw std::invalid_argument("Unknown color");
    cells_[index(p)] = piece;
}
// reset() очищає std::optional: у цій клітинці більше немає фігури.
void Table::remove(Position p) { cells_[index(p)].reset(); }
// Порожні клітинки пропускаємо, решту рахуємо за кольором.
int Table::count(Color color) const {
    int result = 0;
    for (const auto& cell : cells_) if (cell && cell->color == color) ++result;
    return result;
}
// Додаткова умова за типом корисна для пошуку королів і перевірки розстановки.
int Table::count(PieceType type, Color color) const {
    int result = 0;
    for (const auto& cell : cells_) if (cell && cell->color == color && cell->type == type) ++result;
    return result;
}
std::optional<Position> Table::king(Color color) const {
    // Нуль або кілька королів означають некоректну ігрову позицію.
    if (count(PieceType::King, color) != 1) return std::nullopt;
    for (int z = 1; z <= 8; ++z) for (int y = 1; y <= 8; ++y) for (int x = 1; x <= 8; ++x) {
        Position p{x,y,z};
        const auto& cell = at(p);
        if (cell && cell->type == PieceType::King && cell->color == color) return p;
    }
    return std::nullopt;
}
bool Table::light(Position p) {
    // Виклик перевіряє межі клітинки; сам індекс для обчислення кольору не потрібен.
    (void)index(p);
    return (p.x + p.y + p.z) % 2 == 0;
}
Table Table::initial() {
    Table table;
    // Координати 18 основних білих фігур; чорні отримують дзеркальне розташування.
    const std::pair<PieceType, const char*> back[] = {
        {PieceType::King,"511"}, {PieceType::Queen,"411"},
        {PieceType::Rook,"114"}, {PieceType::Rook,"814"},
        {PieceType::Rook,"118"}, {PieceType::Rook,"818"},
        {PieceType::Knight,"214"}, {PieceType::Knight,"714"},
        {PieceType::Knight,"218"}, {PieceType::Knight,"718"},
        {PieceType::Bishop,"314"}, {PieceType::Bishop,"614"},
        {PieceType::Bishop,"318"}, {PieceType::Bishop,"618"},
        {PieceType::Stone,"811"}, {PieceType::Air,"111"},
        {PieceType::Water,"211"}, {PieceType::Fire,"711"}
    };
    for (Color color : {Color::White, Color::Black}) {
        for (const auto& entry : back) {
            Position p = Position::parse(entry.second);
            // Відбиття по Y: перший ряд переходить у восьмий, другий — у сьомий.
            if (color == Color::Black) p.y = 9 - p.y;
            table.put(p, {entry.first, color});
        }
        // Вісім військових на шарі Z=1 і ще по чотири на шарах Z=4 та Z=8.
        int y = color == Color::White ? 2 : 7;
        for (int x = 1; x <= 8; ++x) table.put({x,y,1}, {PieceType::Soldier,color});
        for (int z : {4,8}) for (int x : {1,3,6,8})
            table.put({x,y,z}, {PieceType::Soldier,color});
    }
    return table;
}
} // Кінець простору імен гри.
