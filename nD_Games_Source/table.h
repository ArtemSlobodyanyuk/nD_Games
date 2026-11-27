#pragma once

#include <array>
#include <optional>
#include <string>

namespace nd {
// Ребро кубічної дошки: 8 × 8 × 8 = 512 клітинок.
constexpr int board_size = 8;
// Колір визначає сторону гравця, черговість і допустимість взяття.
enum class Color { White, Black };
// Повертає колір суперника.
Color opposite(Color color);
// Внутрішні назви фігур; їхні літерні позначення задає symbol().
enum class PieceType {
    Soldier, // Військовий.
    Rook,    // Тура.
    Bishop,  // Слон.
    Knight,  // Кінь.
    Stone,   // Камінь, або літоїд.
    Water,   // Вода.
    Fire,    // Вогонь, або інферно.
    Air,     // Повітря.
    King,    // Король.
    Queen    // Королева.
};
// Перетворює тип фігури на позначення P, R, B, N, L, W, I, S, K або Q.
char symbol(PieceType type);
// Виконує зворотне перетворення; невідома літера спричиняє виняток.
PieceType piece_type(char code);

// Координати клітинки починаються з 1: X — стовпець, Y — ряд, Z — шар.
// У траєкторіях цей самий тип зберігає відносні зміщення, зокрема від'ємні.
struct Position {
    int x, y, z;
    // Перевіряє межі лише для координат клітинки, а не для зміщення маршруту.
    bool valid() const;
    // Порівнює всі три координати; потрібне також для порівняння траєкторій.
    bool operator==(const Position& other) const;
    bool operator!=(const Position& other) const { return !(*this == other); }
    // Читає рівно три цифри 1–8: наприклад, "511" означає (5,1,1).
    static Position parse(const std::string& text);
    // Записує допустиму клітинку назад у формат XYZ.
    std::string str() const;
};
// Опис однієї фігури; її координата визначається клітинкою у Table.
struct Piece {
    PieceType type;
    Color color;
};

// Зберігає позицію та чергу гравця. Правильність ходів перевіряє MoveEngine.
class Table {
public:
    // Створює початкову розстановку: по 34 фігури, з них по 16 військових.
    static Table initial();
    // Повертає фігуру або порожнє значення std::optional для вільної клітинки.
    // Координати за межами дошки спричиняють виняток.
    const std::optional<Piece>& at(Position p) const;
    // Ставить або замінює фігуру; це інструмент розстановки, а не ігровий хід.
    void put(Position p, Piece piece);
    // Звільняє клітинку, навіть якщо вона вже порожня.
    void remove(Position p);
    // Читає або встановлює сторону, яка зараз має ходити.
    Color turn() const { return turn_; }
    void set_turn(Color color) { turn_ = color; }
    // Підрахунок усіх фігур сторони або лише вказаного типу.
    int count(Color color) const;
    int count(PieceType type, Color color) const;
    // Повертає координату лише за наявності рівно одного короля цього кольору.
    std::optional<Position> king(Color color) const;
    // Визначає світлий колір клітинки за парністю суми координат.
    static bool light(Position p);
private:
    // Перетворює XYZ на індекс 0–511 для зберігання куба в одному масиві.
    static std::size_t index(Position p);
    // Порожній std::optional означає відсутність фігури; спочатку порожні всі клітинки.
    std::array<std::optional<Piece>, 512> cells_{};
    Color turn_ = Color::White;
};
} // Кінець простору імен гри.
