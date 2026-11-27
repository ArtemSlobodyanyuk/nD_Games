#include "move.h"
#include "json.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <tuple>

namespace nd {
// Допоміжні функції цього простору імен доступні лише всередині move.cpp.
namespace {
using Array = json::Value::Array;
using Object = json::Value::Object;
// Читає траєкторії та відкидає порожні, завеликі або некоректні маршрути.
std::vector<std::vector<Position>> read_paths(const json::Value& value) {
    const auto& paths = value.as<Array>();
    if (paths.empty() || paths.size() > 512) throw std::runtime_error("Invalid number of paths");
    std::vector<std::vector<Position>> result;
    for (const auto& item : paths) {
        const auto& steps = item.as<Array>();
        if (steps.empty() || steps.size() > 7) throw std::runtime_error("Invalid path length");
        std::vector<Position> path;
        // Набір уже прочитаних зміщень не дозволяє двічі відвідати ту саму точку.
        std::set<std::tuple<int,int,int>> visited;
        for (const auto& step : steps) {
            const auto& xyz = step.as<Array>();
            if (xyz.size() != 3) throw std::runtime_error("Path offset must have three coordinates");
            Position p{xyz[0].as<int>(), xyz[1].as<int>(), xyz[2].as<int>()};
            // Між клітинками дошки зміщення по осі не перевищує 7.
            // Нульове зміщення повертало б фігуру на її початкову клітинку.
            if (p.x < -7 || p.x > 7 || p.y < -7 || p.y > 7 || p.z < -7 || p.z > 7
                || (p.x == 0 && p.y == 0 && p.z == 0)
                || !visited.emplace(p.x,p.y,p.z).second)
                throw std::runtime_error("Invalid or repeated path offset");
            path.push_back(p);
        }
        result.push_back(std::move(path));
    }
    return result;
}
// Створює однаковий для всіх перевірок результат відмови: код і пояснення.
Validation failure(MoveError error, const std::string& text) { return {error,text}; }
// Передає кожну з 512 клітинок у функцію, отриману аргументом.
// Шаблон дозволяє передавати лямбди з різними захопленими даними.
template<class F> void each_square(F function) {
    for (int z = 1; z <= 8; ++z) for (int y = 1; y <= 8; ++y) for (int x = 1; x <= 8; ++x)
        function(Position{x,y,z});
}
// Проходить один маршрут до допустимої точки зупинки та повертає відвідані клітинки.
// Порожній результат означає, що шлях перекритий, вийшов за межі або не веде до to.
// sweep дозволяє проходити через ворогів зі взяттям; defended потрібний для карти захисту.
// Викликається лише з допустимою початковою клітинкою, що містить фігуру.
std::optional<std::vector<Position>> trace_path(const Table& table, Position from, Position to,
                  const PieceRule& rule, const std::vector<Position>& path, bool sweep = false,
                  std::optional<Position> defended = std::nullopt) {
    std::vector<Position> visited;
    for (std::size_t i = 0; i < path.size(); ++i) {
        // Кожне зміщення додається до старту, а не до попередньої клітинки.
        Position p{from.x + path[i].x, from.y + path[i].y, from.z + path[i].z};
        if (!p.valid()) break;
        visited.push_back(p);
        // Кінцеву клітинку перевіряємо до перешкод: на ній може стояти ворог.
        // Для води допустима лише остання точка, для вогню — також проміжні.
        if (p == to && (rule.stop_anywhere || i + 1 == path.size())) return visited;
        const auto& cell = table.at(p);
        // Під час побудови карти захисту уявляємо, що на defended став суперник.
        bool enemy = cell && (cell->color != table.at(from)->color || defended == p);
        // Стрибок ігнорує перешкоди; множинне взяття пропускає лише ворогів.
        if (!rule.jumps && cell && !(sweep && enemy)) break;
    }
    return std::nullopt;
}
// Скорочена перевірка для випадків, коли сам список пройдених клітинок не потрібен.
bool follows_path(const Table& table, Position from, Position to,
                  const PieceRule& rule, const std::vector<Position>& path, bool sweep = false) {
    return trace_path(table,from,to,rule,path,sweep).has_value();
}
} // Кінець внутрішніх допоміжних функцій.

// Нормалізує вибір маршруту до ключа, наприклад "+Z:5".
std::string PathSelection::id() const {
    if (direction.size() != 2 || (direction[0] != '+' && direction[0] != '-')
        || (direction[1] != 'X' && direction[1] != 'Y' && direction[1] != 'Z')
        || variant < 1 || variant > 8)
        throw std::invalid_argument("Path requires direction +X/-X/+Y/-Y/+Z/-Z and variant 1..8");
    return direction + ":" + std::to_string(variant);
}
special_rules::special_rules() {
    set_module("path",std::make_shared<PathModule>());
    set_module("multicapture",std::make_shared<MultiCaptureModule>());
    set_module("home_protection",std::make_shared<HomeProtectionModule>());
}
// Модуль надає можливість лише типам зі свого списку налаштувань.
bool MultiCaptureModule::captures_along_path(PieceType type) const {
    return std::find(pieces_.begin(),pieces_.end(),type) != pieces_.end();
}

Validation HomeProtectionModule::validate_capture(Color side, Position from, Position target) const {
    const bool white = side == Color::White;
    const bool on_own_half = white ? from.y <= board_size / 2 : from.y > board_size / 2;
    const int enemy_home = white ? board_size : 1;
    if (on_own_half && target.y == enemy_home)
        return failure(MoveError::ProtectedHome,
                       "Capturing in the enemy home row requires starting on the enemy half");
    return {};
}
// Для дозволу достатньо одного підключеного модуля з потрібною можливістю.
bool special_rules::sweeps(PieceType type) const {
    for (const auto& entry : modules_)
        if (entry.second->captures_along_path(type)) return true;
    return false;
}
// Повторне підключення замінює модуль у порту; nullptr видаляє цей запис.
void special_rules::set_module(const std::string& port, std::shared_ptr<const MoveModule> module) {
    if (port.empty()) throw std::invalid_argument("Module port cannot be empty");
    if (module) modules_[port] = std::move(module);
    else modules_.erase(port);
}

bool special_rules::path_enabled() const { return modules_.find("path") != modules_.end(); }

Validation special_rules::validate_modules(const Table& table, const Move& move, const Rules& rules) const {
    if (move.path && !path_enabled())
        return failure(MoveError::MissingModule,"No module connected to port path");
    for (const auto& entry : modules_) {
        auto result = entry.second->validate(table,move,rules);
        if (!result) return result;
    }
    return {};
}

Validation special_rules::validate_capture(Color side, Position from, Position target) const {
    for (const auto& entry : modules_) {
        if (auto result = entry.second->validate_capture(side,from,target); !result) return result;
    }
    return {};
}

Validation special_rules::collect_captures(const Table& table, Position from, const std::vector<Position>& route,
                                           bool sweep, std::vector<Position>& captures) const {
    captures.clear();
    const Color side = table.at(from)->color;
    // Звичайне взяття можливе на кінцевій клітинці, множинне — уздовж маршруту.
    for (auto step = sweep ? route.begin() : std::prev(route.end()); step != route.end(); ++step) {
        const auto& victim = table.at(*step);
        if (!victim || victim->color == side) continue;
        if (auto result = validate_capture(side,from,*step); !result) return result;
        captures.push_back(*step);
    }
    return {};
}

Validation special_rules::validate_path_choice(const Piece& piece, const PieceRule& rule, const Move& move) const {
    // Маршрути до однієї клітинки можуть забирати різних ворогів.
    if (sweeps(piece.type) && !rule.path_ids.empty() && !move.path)
        return failure(MoveError::PathRequired,"Select a path explicitly for a move with path captures");
    return {};
}

bool special_rules::attacks_along_path(const Table& table, Position from, Position target,
                                      Color by, const PieceRule& rule) const {
    const Position offset{target.x-from.x,target.y-from.y,target.z-from.z};
    std::vector<Position> captures;
    for (const auto& path : rule.attacks) {
        if (std::find(path.begin(),path.end(),offset) == path.end()) continue;
        const Position last = path.back();
        const Position end = rule.stop_anywhere ? target : Position{from.x+last.x,from.y+last.y,from.z+last.z};
        if (!end.valid()) continue;
        // Проміжне взяття можливе лише на маршруті з допустимою зупинкою.
        const auto& final = table.at(end);
        if (end != target && final && final->color == by) continue;
        const auto route = trace_path(table,from,end,rule,path,true,target);
        if (route && collect_captures(table,from,*route,true,captures)) return true;
    }
    return false;
}

bool special_rules::promotes(Piece piece, Position to) {
    return piece.type == PieceType::Soldier && to.y == (piece.color == Color::White ? 8 : 1);
}

Validation special_rules::validate_promotion(Piece piece, const Move& move, const Rules& rules) {
    const bool promotion = promotes(piece,move.to);
    if (promotion && !move.promotion) return failure(MoveError::PromotionRequired,"Choose N, B or R for promotion");
    if (!move.promotion) return {};
    if (!promotion || std::find(promotion_types.begin(),promotion_types.end(),*move.promotion) == promotion_types.end())
        return failure(MoveError::InvalidPromotion,"Promotion is allowed only to N, B or R on the last rank");
    if (!rules.at(*move.promotion).defined)
        return failure(MoveError::UndefinedRule,"Promotion target has undefined movement rules");
    return {};
}

MoveEngine::MoveEngine(Rules rules) : rules_(std::move(rules)) {}

void MoveEngine::set_module(const std::string& port, std::shared_ptr<const MoveModule> module) {
    special_.set_module(port,std::move(module));
}
// Без явного вибору цей модуль не накладає додаткових обмежень.
Validation PathModule::validate(const Table& table, const Move& move, const Rules& rules) const {
    if (!move.path) return {};
    if (!move.from.valid() || !move.to.valid() || !table.at(move.from))
        return failure(MoveError::InvalidPath,"Path requires valid source and destination");
    std::string id;
    try { id = move.path->id(); }
    catch (const std::invalid_argument& error) { return failure(MoveError::InvalidPath,error.what()); }
    const auto& rule = rules.at(table.at(move.from)->type);
    for (std::size_t i = 0; i < rule.path_ids.size(); ++i) {
        if (rule.path_ids[i] != id) continue;
        const auto& path = table.at(move.to) ? rule.attacks[i] : rule.moves[i];
        // Тут потрібна лише форма маршруту. Зайнятість клітинок двигун перевіряє
        // окремо з урахуванням усіх можливостей, зокрема множинного взяття.
        PieceRule geometry = rule;
        geometry.jumps = true;
        if (follows_path(table,move.from,move.to,geometry,path)) return {};
        return failure(MoveError::InvalidPath,"Destination is not reachable along the selected path");
    }
    return failure(MoveError::InvalidPath,"Selected path does not exist for this piece");
}

// Файл читається повністю; помилки відкриття та читання відокремлені від помилок JSON.
Rules Rules::load(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open rules: " + filename);
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!file.eof() && file.fail()) throw std::runtime_error("Cannot read rules: " + filename);
    return parse(contents);
}
// Перетворює загальні значення JSON на перевірені правила фігур.
Rules Rules::parse(const std::string& text) {
    auto root = json::Parser(text).parse();
    // Двигун підтримує лише цю версію формату та дошку з ребром 8.
    if (root.at("version").as<int>() != 1 || root.at("board_size").as<int>() != board_size)
        throw std::runtime_error("Unsupported rules version or board size");
    const auto& pieces = root.at("pieces").as<Object>();
    if (pieces.size() != 10) throw std::runtime_error("Rules must contain all ten piece types");
    Rules result;
    for (const auto& entry : pieces) {
        if (entry.first.size() != 1) throw std::runtime_error("Invalid piece key");
        PieceType type = piece_type(entry.first[0]);
        const auto& data = entry.second;
        PieceRule rule;
        rule.defined = data.at("defined").as<bool>();
        if (rule.defined) {
            rule.jumps = data.at("jumps").as<bool>();
            rule.stop_anywhere = data.at("stop_anywhere").as<bool>();
            rule.moves = read_paths(data.at("move_paths"));
            rule.attacks = read_paths(data.at("attack_paths"));
            const auto& object = data.as<Object>();
            // Позначення маршрутів необов'язкові; для води та повітря вони задані.
            auto ids = object.find("path_ids");
            if (ids != object.end()) {
                const auto& labels = ids->second.as<Array>();
                // Один індекс має вказувати на той самий варіант руху та атаки.
                if (labels.size() != rule.moves.size() || labels.size() != rule.attacks.size())
                    throw std::runtime_error("Path identifiers must match both path arrays");
                std::set<std::string> unique;
                for (const auto& label : labels) {
                    const auto& id = label.as<std::string>();
                    if (id.size() != 4 || id[2] != ':' || id[3] < '1' || id[3] > '8')
                        throw std::runtime_error("Invalid path identifier");
                    (void)PathSelection{id.substr(0,2),id[3]-'0'}.id();
                    if (!unique.insert(id).second) throw std::runtime_error("Duplicate path identifier");
                    rule.path_ids.push_back(id);
                }
            }
        } else {
            // Невизначене правило зберігаємо з поясненням, замість вигадувати хід.
            rule.reason = data.at("reason").as<std::string>();
            if (rule.reason.empty()) throw std::runtime_error("Undefined rules need a reason");
        }
        result.pieces_.emplace(type, std::move(rule));
    }
    return result;
}
// Повертає посилання на правило без копіювання всіх його траєкторій.
const PieceRule& Rules::at(PieceType type) const { return pieces_.at(type); }

// Перевіряє об'єднання звичайних маршрутів руху або атаки.
bool MoveEngine::follows(const Table& table, Position from, Position to,
                         const PieceRule& rule, bool attack) const {
    for (const auto& path : (attack ? rule.attacks : rule.moves))
        if (follows_path(table,from,to,rule,path)) return true;
    return false;
}
// Варіант для перевірки двох клітинок без явного вибору маршруту та перетворення.
Validation MoveEngine::movement(const Table& table, Position from, Position to) const {
    return movement(table,Move{from,to,std::nullopt});
}
// Перевірка маршруту може обчислити жертв, але цей метод їх не знімає.
Validation MoveEngine::movement(const Table& table, const Move& move) const {
    MovePreview details;
    return resolve(table,move,details);
}
// Перший рівень перевірки: ввід → маршрут → модулі → список можливих жертв.
// Черговість, перетворення та безпека короля перевіряються пізніше в prepare().
Validation MoveEngine::resolve(const Table& table, const Move& move, MovePreview& details) const {
    Position from = move.from, to = move.to;
    if (!from.valid() || !to.valid()) return failure(MoveError::OutOfBounds,"Coordinates must be in [1,8]");
    if (from == to) return failure(MoveError::SameSquare,"A move must change the square");
    // Звертатися до клітинок безпечно лише після перевірки меж вище.
    const auto& source = table.at(from);
    if (!source) return failure(MoveError::EmptySource,"Source square is empty");
    const auto& target = table.at(to);
    if (target && target->color == source->color)
        return failure(MoveError::FriendlyTarget,"Cannot capture your own piece");
    const auto& rule = rules_.at(source->type);
    if (!rule.defined) return failure(MoveError::UndefinedRule,rule.reason);
    bool sweep = special_.sweeps(source->type);
    std::string selected;
    if (move.path) {
        try { selected = move.path->id(); }
        catch (const std::invalid_argument& error) { return failure(MoveError::InvalidPath,error.what()); }
    }
    // any відрізняє неможливий хід від помилкового вибору конкретного маршруту.
    bool any = false;
    Validation capture_result;
    // Камінь та військовий мають різні правила для порожньої й зайнятої цілі.
    const auto& paths = target ? rule.attacks : rule.moves;
    for (std::size_t i=0; i<paths.size(); ++i) {
        auto route = trace_path(table,from,to,rule,paths[i],sweep);
        if (!route) continue;
        any = true;
        if (move.path && (rule.path_ids.empty() || rule.path_ids[i] != selected)) continue;
        capture_result = special_.collect_captures(table,from,*route,sweep,details.captures);
        if (!capture_result) continue;
        if (auto result = special_.validate_modules(table,move,rules_); !result) return result;
        details.route = std::move(*route);
        return {};
    }
    if (!any) return failure(MoveError::IllegalPath,"Illegal movement, capture or blocked path");
    if (!capture_result) return capture_result;
    return failure(MoveError::InvalidPath,"Destination is not reachable along the selected path");
}
// Невизначена фігура на дошці не дозволяє достовірно перевірити безпеку короля.
bool MoveEngine::complete(const Table& table) const {
    bool result = true;
    each_square([&](Position p) {
        const auto& cell = table.at(p);
        if (cell && !rules_.at(cell->type).defined) result = false;
    });
    return result;
}
// Будує карту загроз без рекурсивної повної перевірки ходів атакувальної сторони.
// Інакше перевірка шаху викликала б сама себе через validate().
bool MoveEngine::attacked(const Table& table, Position target, Color by) const {
    if (!target.valid()) throw std::out_of_range("Attack target outside board");
    bool result = false;
    each_square([&](Position from) {
        const auto& source = table.at(from);
        if (!source || source->color != by || from == target) return;
        const auto& rule = rules_.at(source->type);
        if (!rule.defined) throw std::logic_error("Attack rules undefined: " + rule.reason);
        if (!special_.validate_capture(by,from,target)) return;
        // Враховуємо захищені свої клітинки, порожні діагоналі військового
        // та атаки зв'язаних фігур, навіть якщо їхній власний хід відкрив би шах.
        const bool threatens = special_.sweeps(source->type)
            ? special_.attacks_along_path(table,from,target,by,rule)
            : follows(table,from,target,rule,true);
        if (threatens) result = true;
    });
    return result;
}
// Шах — це атака суперника на єдиного короля вибраної сторони.
bool MoveEngine::in_check(const Table& table, Color color) const {
    auto king = table.king(color);
    if (!king) throw std::logic_error("Position must contain exactly one king for this color");
    return attacked(table,*king,opposite(color));
}
// Застосовує вже підготовлений хід. Викликається для симуляції або після успіху prepare().
void MoveEngine::apply(Table& table, const Move& move, const std::vector<Position>& captures) {
    Piece piece = *table.at(move.from);
    if (move.promotion) piece.type = *move.promotion;
    for (Position p : captures) table.remove(p);
    table.remove(move.from);
    table.put(move.to,piece);
    table.set_turn(opposite(table.turn()));
}
// Перевірка для інтерфейсу чи підказок: справжня дошка передається як const.
Validation MoveEngine::validate(const Table& table, const Move& move) const {
    MovePreview details;
    return prepare(table,move,details);
}

std::optional<MovePreview> MoveEngine::preview(const Table& table, const Move& move) const {
    MovePreview details;
    if (!prepare(table,move,details)) return std::nullopt;
    return details;
}
// Другий рівень перевірки: додає до геометрії всі правила повного ігрового ходу.
Validation MoveEngine::prepare(const Table& table, const Move& move, MovePreview& details) const {
    auto geometry = resolve(table,move,details);
    if (!geometry) return geometry;
    const Piece source = *table.at(move.from);
    if (auto result = special_.validate_path_choice(source,rules_.at(source.type),move); !result) return result;
    if (source.color != table.turn()) return failure(MoveError::WrongTurn,"It is the other player's turn");
    if (!table.king(Color::White) || !table.king(Color::Black))
        return failure(MoveError::InvalidPosition,"Exactly one king of each color is required");
    // Заборона взяття короля діє також на проміжних клітинках маршруту.
    for (Position p : details.captures)
        if (table.at(p)->type == PieceType::King)
            return failure(MoveError::KingCapture,"Kings cannot be captured, including along a path");
    if (auto result = special_rules::validate_promotion(source,move,rules_); !result) return result;
    if (!complete(table)) return failure(MoveError::IncompleteRules,"Cannot verify king safety with undefined pieces on the board");
    // Копія включає всі клітинки та чергу. Спершу знімаємо всіх жертв на копії,
    // бо їхнє зникнення може як прибрати шах, так і відкрити іншу атаку.
    Table next = table;
    apply(next,move,details.captures);
    if (in_check(next,source.color)) return failure(MoveError::SelfCheck,"Move leaves your king in check");
    return {};
}
// Справжня позиція змінюється тільки після успіху всіх перевірок.
Validation MoveEngine::play(Table& table, const Move& move) const {
    MovePreview details;
    auto result = prepare(table,move,details);
    if (result) apply(table,move,details.captures);
    return result;
}
// Перебирає початкові й кінцеві клітинки та залишає лише повністю допустимі ходи.
std::vector<Move> MoveEngine::legal_moves(const Table& table) const {
    if (!table.king(Color::White) || !table.king(Color::Black))
        throw std::logic_error("Exactly one king of each color is required");
    if (!complete(table)) throw std::logic_error("Cannot enumerate legal moves with incomplete rules");
    std::vector<Move> result;
    each_square([&](Position from) {
        const auto& source = table.at(from);
        if (!source || source->color != table.turn()) return;
        each_square([&](Position to) {
            const auto& rule = rules_.at(source->type);
            // Лямбда додає один хід або три окремі варіанти перетворення військового.
            auto append = [&](std::optional<PathSelection> path) {
                if (special_rules::promotes(*source,to)) {
                    for (auto type : special_rules::promotion_types) {
                        Move move{from,to,type,path};
                        if (validate(table,move)) result.push_back(move);
                    }
                } else {
                    Move move{from,to,std::nullopt,path};
                    if (validate(table,move)) result.push_back(move);
                }
            };
            // Маршрути зі спільним кінцем залишаються окремими ходами: жертви можуть відрізнятися.
            if (rule.path_ids.empty() || !special_.path_enabled()) append(std::nullopt);
            else {
                const auto& paths = table.at(to) ? rule.attacks : rule.moves;
                for (std::size_t i = 0; i < rule.path_ids.size(); ++i) {
                    if (!follows_path(table,from,to,rule,paths[i],special_.sweeps(source->type))) continue;
                    const auto& id = rule.path_ids[i];
                    append(PathSelection{id.substr(0,2),id[3]-'0'});
                }
            }
        });
    });
    return result;
}
// Немає дозволених ходів: за наявності шаху це мат, без шаху — пат.
// Додаткові правила нічиєї за повторенням чи лічильниками тут не реалізовані.
GameStatus MoveEngine::status(const Table& table) const {
    if (!table.king(Color::White) || !table.king(Color::Black)) return GameStatus::InvalidPosition;
    if (!complete(table)) return GameStatus::IncompleteRules;
    bool check = in_check(table,table.turn());
    if (legal_moves(table).empty()) return check ? GameStatus::Checkmate : GameStatus::Stalemate;
    return check ? GameStatus::Check : GameStatus::Active;
}
} // Кінець простору імен гри.
