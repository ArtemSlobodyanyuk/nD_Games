#include "command.h"
#include <iostream>
#include <sstream>
#include <stdexcept>

// Консольний вхід у гру. Перший аргумент програми може задавати шлях до правил JSON.
int main(int argc, char** argv) {
    try {
        // Спочатку завантажуємо правила, потім створюємо двигун і початкову позицію.
        nd::MoveEngine engine(nd::Rules::load(argc > 1 ? argv[1] : "chess_rools.json"));
        nd::Table table = nd::Table::initial();
        std::cout << "3D chess, coordinates XYZ in [1,8]. Initial board: "
                  << table.count(nd::Color::White) << " white, "
                  << table.count(nd::Color::Black) << " black pieces.\n";
        if (engine.status(table) == nd::GameStatus::IncompleteRules)
            std::cout << "README rules are incomplete; full game moves are blocked until clarified.\n";
        std::cout << "Commands: show XYZ | move XYZ XYZ [N/B/R] [path DIRECTION 1..8] | quit\n";
        // Кожен рядок — окрема команда: show, move або quit.
        std::string line;
        while (std::getline(std::cin,line)) {
            try {
                // Потік дозволяє послідовно читати слова з одного введеного рядка.
                std::istringstream input(line);
                std::string command, from, extra;
                input >> command;
                // Команди quit та show не приймають зайвих аргументів.
                if (command == "quit" && !(input >> extra)) break;
                if (command == "show" && (input >> from) && !(input >> extra)) {
                    // show лише читає клітинку: колір і позначення фігури або порожній стан.
                    const auto& piece = table.at(nd::Position::parse(from));
                    if (!piece) std::cout << "Empty\n";
                    else std::cout << (piece->color == nd::Color::White ? "White " : "Black ")
                                   << nd::symbol(piece->type) << '\n';
                } else if (command == "move") {
                    std::string arguments;
                    // Решта рядка містить координати, перетворення та маршрут.
                    std::getline(input,arguments);
                    nd::Move move = nd::parse_move(arguments);
                    // play сам перевіряє хід; консоль не має окремої копії шахових правил.
                    auto result = engine.play(table,move);
                    std::cout << (result ? "OK" : result.message) << '\n';
                } else std::cout << "Invalid command\n";
            // Помилка однієї команди не закриває гру: можна одразу ввести наступну.
            } catch (const std::exception& error) { std::cout << error.what() << '\n'; }
        }
        return 0;
    // Помилка початкового завантаження правил не дозволяє запустити гру.
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
