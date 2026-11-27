#pragma once

#include "table.h"
#include <array>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace nd {
// Правило одного типу фігури, прочитане з chess_rools.json.
struct PieceRule {
    // false зберігає явно невизначені правила в користувацьких конфігураціях.
    bool defined = false;
    // Пояснення, чому правило ще не задане.
    std::string reason;
    // Дозвіл перестрибувати проміжні фігури; взяття своїх усе одно заборонене.
    bool jumps = false;
    // true — зупинка на будь-якому кроці, false — лише на останньому.
    bool stop_anywhere = false;
    // Зовнішній масив містить маршрути, внутрішній — послідовні зміщення від старту.
    // Усі точки відносні до початкової клітинки, а не до попереднього кроку.
    std::vector<std::vector<Position>> moves;
    // Атаки відокремлені від звичайних ходів: це важливо для військового й каменя.
    std::vector<std::vector<Position>> attacks;
    // Необов'язкові позначення маршрутів, наприклад "+Z:1", спільні для руху й атак.
    std::vector<std::string> path_ids;
};
// Набір правил усіх десяти типів. Після завантаження надає лише читання.
class Rules {
public:
    // Читає файл та передає його вміст до parse().
    static Rules load(const std::string& filename);
    // Перевіряє структуру JSON, зміщення, прапорці та позначення маршрутів.
    static Rules parse(const std::string& json);
    // Повертає правило конкретного типу фігури.
    const PieceRule& at(PieceType type) const;
private:
    std::map<PieceType, PieceRule> pieces_;
};
// Код дозволяє програмі розрізняти причини відмови без аналізу тексту повідомлення.
// None — успіх; решта значень означають порушення вводу, геометрії або правил гри.
enum class MoveError {
    None,              // Перевірка успішна.
    OutOfBounds,       // Координати виходять за межі дошки.
    SameSquare,        // Початкова й кінцева клітинки збігаються.
    EmptySource,       // На початковій клітинці немає фігури.
    WrongTurn,         // Намагається ходити інша сторона.
    FriendlyTarget,    // Кінцева клітинка зайнята своєю фігурою.
    UndefinedRule,     // Правило фігури або результату перетворення не визначене.
    IllegalPath,       // Геометрія не дозволяє хід або маршрут перекритий.
    InvalidPosition,   // У позиції немає рівно одного короля кожного кольору.
    KingCapture,       // Заплановане заборонене взяття короля.
    SelfCheck,         // Після ходу власний король залишився б під шахом.
    PromotionRequired, // Військовий досяг останнього ряду без вибору нової фігури.
    InvalidPromotion,  // Неприпустимий тип або місце перетворення.
    IncompleteRules,   // Невизначені фігури не дозволяють перевірити всю позицію.
    InvalidPath,       // Обраний варіант маршруту відсутній або не веде до цілі.
    MissingModule,     // Явний вибір маршруту надійшов без підключеного модуля шляху.
    PathRequired,     // Для множинного взяття потрібно явно вибрати маршрут.
    ProtectedHome     // Фігура на своїй половині не може брати у ворожому домі.
};
// Результат перевірки. Порожнє значення {} означає успішну перевірку.
struct Validation {
    MoveError error = MoveError::None;
    std::string message;
    // Завдяки цьому перетворенню можна писати if (result) або if (!result).
    explicit operator bool() const { return error == MoveError::None; }
};
// Вибір одного з восьми маршрутів у напрямку +X, −X, +Y, −Y, +Z або −Z.
struct PathSelection {
    // Знак і літера осі, наприклад "+Z".
    std::string direction;
    // Номер поперечного відхилення: від 1 до 8.
    int variant;
    // Перевіряє напрямок і номер та створює ключ для пошуку в path_ids.
    std::string id() const;
};
// Запит на один хід; сам по собі не переміщує фігуру на дошці.
struct Move {
    Position from, to;
    // Новий тип військового на останньому ряду; std::nullopt означає відсутність вибору.
    std::optional<PieceType> promotion;
    // Маршрут необов'язковий, окрім повного ходу з неоднозначним множинним взяттям.
    std::optional<PathSelection> path = std::nullopt;
};
// Пройдений шлях без стартової клітинки та клітинки взяття в порядку проходження.
struct MovePreview {
    std::vector<Position> route;
    std::vector<Position> captures;
};
// Спільний інтерфейс модулів: додаткова перевірка або дозвіл множинного взяття.
// Геометрію, захист своїх фігур і безпеку короля однаково контролює основний двигун.
class MoveModule {
public:
    // Віртуальний деструктор забезпечує коректне видалення похідного модуля.
    virtual ~MoveModule() = default;
    // Похідний модуль зобов'язаний реалізувати власну перевірку.
    virtual Validation validate(const Table& table, const Move& move, const Rules& rules) const = 0;
    // За замовчуванням модулі не дають права брати проміжних ворогів.
    virtual bool captures_along_path(PieceType) const { return false; }
    // Застосовується до кожної жертви та до потенційної цілі на карті загроз.
    virtual Validation validate_capture(Color, Position, Position) const { return {}; }
};
// Перевіряє, чи існує обраний маршрут та чи належить йому кінцева клітинка.
class PathModule final : public MoveModule {
public:
    Validation validate(const Table& table, const Move& move, const Rules& rules) const override;
};
// Додає вибраним типам взяття ворогів на відвіданих клітинках маршруту.
class MultiCaptureModule final : public MoveModule {
public:
    // За замовчуванням множинне взяття доступне воді та повітрю.
    explicit MultiCaptureModule(std::vector<PieceType> pieces = {PieceType::Water, PieceType::Air})
        : pieces_(std::move(pieces)) {}
    // Окремих обмежень цей модуль не додає: проходження та жертв обчислює двигун.
    Validation validate(const Table&, const Move&, const Rules&) const override { return {}; }
    bool captures_along_path(PieceType type) const override;
private:
    std::vector<PieceType> pieces_;
};

class HomeProtectionModule final : public MoveModule {
public:
    Validation validate(const Table&, const Move&, const Rules&) const override { return {}; }
    Validation validate_capture(Color side, Position from, Position target) const override;
};

// Вибір маршруту, взяття вздовж нього та перетворення військового.
class special_rules {
public:
    special_rules();
    void set_module(const std::string& port, std::shared_ptr<const MoveModule> module);
    bool path_enabled() const;
    bool sweeps(PieceType type) const;
    Validation validate_modules(const Table& table, const Move& move, const Rules& rules) const;
    Validation validate_capture(Color side, Position from, Position target) const;
    Validation collect_captures(const Table& table, Position from, const std::vector<Position>& route,
                                bool sweep, std::vector<Position>& captures) const;
    Validation validate_path_choice(const Piece& piece, const PieceRule& rule, const Move& move) const;
    bool attacks_along_path(const Table& table, Position from, Position target,
                            Color by, const PieceRule& rule) const;
    static bool promotes(Piece piece, Position to);
    static Validation validate_promotion(Piece piece, const Move& move, const Rules& rules);
    static constexpr std::array promotion_types{PieceType::Knight, PieceType::Bishop, PieceType::Rook};
private:
    std::map<std::string, std::shared_ptr<const MoveModule>> modules_;
};

// Стани: звичайна гра, шах, мат, пат, некоректна позиція або неповні правила.
enum class GameStatus {
    Active,          // Поточна сторона має дозволені ходи та не перебуває під шахом.
    Check,           // Поточна сторона під шахом, але ще має дозволений хід.
    Checkmate,       // Шах і немає жодного дозволеного ходу.
    Stalemate,       // Шаху немає, але немає й дозволених ходів.
    InvalidPosition, // Відсутній король або є кілька королів одного кольору.
    IncompleteRules  // Для присутніх фігур бракує визначених правил.
};

// Поєднує геометрію з JSON, підключені модулі та правила безпеки короля.
class MoveEngine {
public:
    // Зберігає набір правил та підключає стандартні модулі.
    explicit MoveEngine(Rules rules);
    // Підключає модуль до іменованого порту; nullptr від'єднує його.
    // Стандартні порти: "path", "multicapture" і "home_protection".
    void set_module(const std::string& port, std::shared_ptr<const MoveModule> module);
    // Перевіряє маршрут, зайнятість клітинок і модулі, але не чергу чи шах.
    // Королі не обов'язкові: ці методи придатні для редактора та окремих фігур.
    Validation movement(const Table& table, Position from, Position to) const;
    Validation movement(const Table& table, const Move& move) const;
    // Повна перевірка без зміни дошки: черга, королі, перетворення та шах.
    Validation validate(const Table& table, const Move& move) const;
    // Повертає шлях і взяття легального ходу без зміни позиції.
    std::optional<MovePreview> preview(const Table& table, const Move& move) const;
    // Застосовує лише повністю перевірений хід; після відмови позиція не змінюється.
    Validation play(Table& table, const Move& move) const;
    // Карта геометричних атак сторони by; враховує також захищені свої клітинки.
    // Невизначені правила атакувальної фігури спричиняють виняток.
    bool attacked(const Table& table, Position target, Color by) const;
    // Перевіряє, чи атакований король зазначеного кольору.
    bool in_check(const Table& table, Color color) const;
    // Перебирає дозволені ходи поточної сторони, варіанти перетворення та маршрути.
    std::vector<Move> legal_moves(const Table& table) const;
    // Поєднує наявність шаху й дозволених ходів для визначення стану гри.
    GameStatus status(const Table& table) const;
private:
    Rules rules_;
    special_rules special_;
    // Визначає допустимий маршрут і список жертв без зміни дошки.
    Validation resolve(const Table& table, const Move& move, MovePreview& details) const;
    // Доповнює resolve() усіма ігровими перевірками та симуляцією на копії дошки.
    Validation prepare(const Table& table, const Move& move, MovePreview& details) const;
    // Перевіряє звичайний рух або атаку хоча б одним маршрутом правила.
    bool follows(const Table& table, Position from, Position to, const PieceRule& rule, bool attack) const;
    // Повна гра можлива, лише коли визначено правила всіх присутніх фігур.
    bool complete(const Table& table) const;
    // Знімає підготовлений список жертв, переміщує фігуру й передає чергу.
    static void apply(Table& table, const Move& move, const std::vector<Position>& captures);
};
} // Кінець простору імен гри.
