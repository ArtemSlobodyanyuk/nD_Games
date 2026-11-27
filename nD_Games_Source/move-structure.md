# Структура `move.cpp`

Файл працює у просторі імен `nd`. Оголошення типів, чиї методи реалізовані тут, містяться в [`move.h`](move.h).

`MovePreview` зберігає координати пройдених клітинок і взяття для легального ходу.

| Тип | Роль |
| --- | --- |
| `PathSelection` | Структура вибору маршруту. |
| `MoveEngine` | Клас перевірки та виконання ходів. |
| `MultiCaptureModule` | Модуль множинного взяття, похідний від `MoveModule`. |
| `HomeProtectionModule` | Модуль захисту домашнього ряду, похідний від `MoveModule`. |
| `special_rules` | Клас особливих механік, який керує модулями маршруту, взяття й захисту домашнього ряду та перевіряє перетворення військового. |
| `PathModule` | Модуль перевірки маршруту, похідний від `MoveModule`. |
| `Rules` | Клас завантаження та читання правил фігур. |

## Анонімний простір імен усередині `nd`

```cpp
using Array = json::Value::Array;
using Object = json::Value::Object;

std::vector<std::vector<Position>> read_paths(const json::Value& value);
Validation failure(MoveError error, const std::string& text);

template<class F>
void each_square(F function);

std::optional<std::vector<Position>> trace_path(
    const Table& table, Position from, Position to,
    const PieceRule& rule, const std::vector<Position>& path, bool sweep = false,
    std::optional<Position> defended = std::nullopt);

bool follows_path(const Table& table, Position from, Position to,
                  const PieceRule& rule, const std::vector<Position>& path, bool sweep = false);
```

## Функція та методи в `nd` у порядку появи

```cpp
std::string PathSelection::id() const;
Move parse_move(const std::string& text);

special_rules::special_rules();
bool MultiCaptureModule::captures_along_path(PieceType type) const;
Validation HomeProtectionModule::validate_capture(Color side, Position from, Position target) const;
bool special_rules::sweeps(PieceType type) const;
void special_rules::set_module(const std::string& port, std::shared_ptr<const MoveModule> module);
bool special_rules::path_enabled() const;
Validation special_rules::validate_modules(const Table& table, const Move& move, const Rules& rules) const;
Validation special_rules::validate_capture(Color side, Position from, Position target) const;
Validation special_rules::collect_captures(const Table& table, Position from, const std::vector<Position>& route,
                                          bool sweep, std::vector<Position>& captures) const;
Validation special_rules::validate_path_choice(const Piece& piece, const PieceRule& rule, const Move& move) const;
bool special_rules::attacks_along_path(const Table& table, Position from, Position target,
                                      Color by, const PieceRule& rule) const;
bool special_rules::promotes(Piece piece, Position to);
Validation special_rules::validate_promotion(Piece piece, const Move& move, const Rules& rules);

MoveEngine::MoveEngine(Rules rules);
void MoveEngine::set_module(const std::string& port, std::shared_ptr<const MoveModule> module);
Validation PathModule::validate(const Table& table, const Move& move, const Rules& rules) const;

Rules Rules::load(const std::string& filename);
Rules Rules::parse(const std::string& text);
const PieceRule& Rules::at(PieceType type) const;

bool MoveEngine::follows(const Table& table, Position from, Position to,
                         const PieceRule& rule, bool attack) const;
Validation MoveEngine::movement(const Table& table, Position from, Position to) const;
Validation MoveEngine::movement(const Table& table, const Move& move) const;
Validation MoveEngine::resolve(const Table& table, const Move& move, MovePreview& details) const;
bool MoveEngine::complete(const Table& table) const;
bool MoveEngine::attacked(const Table& table, Position target, Color by) const;
bool MoveEngine::in_check(const Table& table, Color color) const;
void MoveEngine::apply(Table& table, const Move& move, const std::vector<Position>& captures);
Validation MoveEngine::validate(const Table& table, const Move& move) const;
std::optional<MovePreview> MoveEngine::preview(const Table& table, const Move& move) const;
Validation MoveEngine::prepare(const Table& table, const Move& move, MovePreview& details) const;
Validation MoveEngine::play(Table& table, const Move& move) const;
std::vector<Move> MoveEngine::legal_moves(const Table& table) const;
GameStatus MoveEngine::status(const Table& table) const;
```
