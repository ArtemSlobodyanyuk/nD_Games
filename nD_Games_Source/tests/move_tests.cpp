#include "move.h"
#ifdef ND_TEST_CONSOLE_INPUT
#include "command.h"
#endif
#include <algorithm>
#include <cmath>
#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace nd;
// Допоміжні засоби тестів відокремлені від виробничого коду гри.
namespace {
// Загальна кількість сценаріїв, які завершилися успіхом або помилкою.
int passed = 0, failed = 0;
// Власна перевірка працює і в Release: стандартний assert міг би бути вимкнений.
void expect(bool condition, const std::string& message = "Unexpected result") {
    if (!condition) throw std::runtime_error(message);
}
// Запускає один сценарій, перехоплює його помилку та продовжує решту перевірок.
// Аргумент body — лямбда з конкретними діями й очікуваними результатами.
template<class F> void test(const std::string& name, F body) {
    try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
}
// Перевіряє, що некоректний ввід спричиняє виняток, а не мовчазний успіх.
template<class F> void throws(F body) {
    bool caught = false;
    try { body(); } catch (const std::exception&) { caught = true; }
    expect(caught,"Expected an exception");
}
// Мінімальна позиція з однією фігурою для перевірки геометрії без правил шаху.
Table single(PieceType type, Color color = Color::White, Position p = {4,4,4}) {
    Table table;
    table.put(p,{type,color});
    return table;
}
// Два віддалені королі — основа повних ігрових тестів. Решту фігур додає сценарій.
Table kings() {
    Table table;
    table.put({1,1,1},{PieceType::King,Color::White});
    table.put({8,8,8},{PieceType::King,Color::Black});
    return table;
}
// Скорочений запис запиту на хід; маршрут за потреби задається окремо.
Move mv(Position from, Position to, std::optional<PieceType> promotion = std::nullopt) {
    return {from,to,promotion};
}
Move routed_move(Position from, Position to, const char* direction, int variant) {
    return {from, to, std::nullopt, PathSelection{direction, variant}};
}
// Порівнює всю позицію та черговість, щоб виявити навіть часткову зміну після відмови.
bool same_table(const Table& a, const Table& b) {
    if (a.turn() != b.turn()) return false;
    for (int x=1;x<=8;++x) for (int y=1;y<=8;++y) for (int z=1;z<=8;++z) {
        const auto& ca = a.at({x,y,z}); const auto& cb = b.at({x,y,z});
        if (ca.has_value() != cb.has_value()) return false;
        if (ca && (ca->type != cb->type || ca->color != cb->color)) return false;
    }
    return true;
}
// Змінює першу знайдену ділянку копії JSON для тестів некоректних конфігурацій.
// Файл правил на диску не змінюється; відсутність зразка означає помилку самого тесту.
std::string replaced(std::string text, const std::string& from, const std::string& to) {
    auto offset = text.find(from);
    expect(offset != std::string::npos,"Test mutation not found");
    text.replace(offset,from.size(),to);
    return text;
}
} // Кінець допоміжних засобів тестування.

// Перший аргумент — шлях до правил. Ненульовий код завершення повідомляє CTest про помилку.
int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: move_tests RULES.json\n"; return 2; }
    try {
        // Двигун і тести використовують один файл; текстова копія потрібна для його спотворень.
        Rules rules = Rules::load(argv[1]);
        MoveEngine engine(rules);
        std::ifstream input(argv[1]);
        std::string json((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());

        // Перевіряємо суворий формат координат: коректний запис відновлюється без втрат.
        for (const std::string text : {"111","888","511","124"})
            test("coordinate roundtrip " + text,[&] { expect(Position::parse(text).str() == text); });
        for (const std::string text : {"","1","11","1111","000","911","191","119","-11","1 1","abc"," 111","111 ","1.1","123\n"})
            test("reject coordinate '" + text + "'",[&] { throws([&] { Position::parse(text); }); });

        // Заповнення всього куба виявляє накладання індексів різних шарів і рядів.
        test("512 unique board cells",[] {
            Table table;
            for (int x=1;x<=8;++x) for (int y=1;y<=8;++y) for (int z=1;z<=8;++z)
                table.put({x,y,z},{(x+y+z)%2 ? PieceType::Knight : PieceType::Queen,
                                  x%2 ? Color::White : Color::Black});
            expect(table.count(Color::White)==256 && table.count(Color::Black)==256);
            for (int x=1;x<=8;++x) for (int y=1;y<=8;++y) for (int z=1;z<=8;++z)
                expect(table.at({x,y,z})->type == ((x+y+z)%2 ? PieceType::Knight : PieceType::Queen));
        });
        // Колір клітинок має чергуватися в усіх трьох вимірах і давати рівні половини.
        test("256 light and 256 dark cells",[] {
            int light=0;
            for (int x=1;x<=8;++x) for (int y=1;y<=8;++y) for (int z=1;z<=8;++z)
                light += Table::light({x,y,z}) ? 1 : 0;
            expect(light==256);
        });
        // Читання, встановлення й видалення повинні однаково захищати межі масиву.
        test("board bounds checked",[] {
            Table table;
            for (Position p : {Position{0,1,1},{9,1,1},{1,0,1},{1,9,1},{1,1,0},{1,1,9}}) {
                throws([&] { table.at(p); });
                throws([&] { table.put(p,{PieceType::King,Color::White}); });
                throws([&] { table.remove(p); });
            }
        });
        // Перевірка погодженої кількості фігур, королів, черги й симетрії розстановки.
        test("initial coordinate roster",[] {
            Table table=Table::initial();
            expect(table.count(Color::White)==34 && table.count(Color::Black)==34);
            expect(table.count(PieceType::Soldier,Color::White)==16);
            expect(table.count(PieceType::Soldier,Color::Black)==16);
            expect(table.king(Color::White)==std::optional<Position>({5,1,1}));
            expect(table.king(Color::Black)==std::optional<Position>({5,8,1}));
            expect(table.at({2,1,1})->type==PieceType::Water && table.at({2,8,1})->type==PieceType::Water);
            expect(table.at({7,1,1})->type==PieceType::Fire && table.at({7,8,1})->type==PieceType::Fire);
            for (int y : {1,8}) expect(!table.at({3,y,1}) && !table.at({6,y,1}));
            expect(table.turn()==Color::White);
            // Відображення по середині Y зберігає тип фігури та змінює її колір.
            for (int x=1;x<=8;++x) for (int y=1;y<=2;++y) for (int z=1;z<=8;++z) {
                const auto& a=table.at({x,y,z}); const auto& b=table.at({x,9-y,z});
                expect(a.has_value()==b.has_value());
                if(a) expect(a->type==b->type && a->color==Color::White && b->color==Color::Black);
            }
        });
        for (Color side : {Color::White,Color::Black})
            test(side==Color::White ? "white enemy home protection" : "black enemy home protection",[&] {
                const bool white = side==Color::White;
                const Color enemy = opposite(side);
                const Position from{4,white ? 4 : 5,4};
                const Position across{4,white ? 5 : 4,4};
                const Position home{4,white ? 8 : 1,4};
                const Position front{4,white ? 7 : 2,4};
                auto table=kings();
                table.set_turn(side);
                table.put(from,{PieceType::Rook,side});
                const auto entry=mv(from,home);
                auto empty=table;
                expect(!engine.attacked(empty,home,side));
                expect(bool(engine.play(empty,entry)));
                expect(empty.at(home) && empty.at(home)->type==PieceType::Rook);

                table.put(home,{PieceType::Soldier,enemy});
                const auto before=table;
                expect(engine.movement(table,entry).error==MoveError::ProtectedHome);
                expect(engine.play(table,entry).error==MoveError::ProtectedHome);
                const auto legal=engine.legal_moves(table);
                expect(std::none_of(legal.begin(),legal.end(),[&](const Move& move) {
                    return move.from==from && move.to==home;
                }));
                expect(same_table(table,before));
                expect(!engine.attacked(table,home,side));
                table.put(front,{PieceType::Soldier,enemy});
                expect(bool(engine.play(table,mv(from,front))));

                table=before;
                table.remove(from);
                table.put(across,{PieceType::Rook,side});
                expect(engine.attacked(table,home,side));
                expect(bool(engine.play(table,mv(across,home))));

                table=before;
                table.remove(*table.king(enemy));
                table.put(home,{PieceType::King,enemy});
                expect(!engine.in_check(table,enemy));
                table.remove(from);
                table.put(across,{PieceType::Rook,side});
                expect(engine.in_check(table,enemy));
            });
        test("home protection applies to an air capture while allowing entry on an empty square",[&] {
            auto table=kings();
            table.put({1,1,1},{PieceType::Air,Color::White});
            table.put({5,1,1},{PieceType::King,Color::White});
            table.put({1,7,1},{PieceType::Soldier,Color::Black});
            table.put({2,8,1},{PieceType::Water,Color::Black});
            const auto move=routed_move({1,1,1}, {2,8,1}, "+Y", 1);
            const auto before=table;
            expect(engine.play(table,move).error==MoveError::ProtectedHome);
            expect(same_table(table,before));
            table.remove({2,8,1});
            expect(bool(engine.play(table,move)));
            expect(!table.at({1,7,1}) && table.at({2,8,1})->type==PieceType::Air);
        });
        // Симуляція безпеки короля покладається на незалежність копії від оригіналу.
        test("board copy is independent",[] {
            auto original=kings(); auto copy=original; copy.remove({1,1,1});
            expect(original.king(Color::White).has_value() && !copy.king(Color::White));
        });

        // Для кожного з 26 напрямків королеви перевіряємо рух, взяття та блокування.
        for (int x=-1;x<=1;++x) for (int y=-1;y<=1;++y) for (int z=-1;z<=1;++z) {
            if (!x && !y && !z) continue;
            Position to{4+3*x,4+3*y,4+3*z};
            test("queen ray to " + to.str(),[&] {
                auto table=single(PieceType::Queen);
                expect(bool(engine.movement(table,{4,4,4},to)));
                table.put(to,{PieceType::Soldier,Color::Black});
                expect(bool(engine.movement(table,{4,4,4},to)));
                table.put({4+x,4+y,4+z},{PieceType::Soldier,Color::Black});
                expect(engine.movement(table,{4,4,4},to).error==MoveError::IllegalPath);
            });
        }
        // Вогонь має рівно 6 осьових, 12 площинних і 8 просторових променів.
        test("fire has 6 axis, 12 plane diagonal and 8 space diagonal rays",[&] {
            const auto& fire=rules.at(PieceType::Fire);
            expect(fire.defined && !fire.jumps && fire.stop_anywhere);
            expect(fire.moves.size()==26 && fire.attacks.size()==26);
            int counts[4]={0,0,0,0};
            for(std::size_t i=0;i<fire.moves.size();++i) {
                const auto& path=fire.moves[i];
                expect(path.size()==3 && path==fire.attacks[i]);
                Position d=path.front();
                int changed=(d.x!=0)+(d.y!=0)+(d.z!=0);
                expect(changed>=1 && changed<=3);
                ++counts[changed];
                for(int n=1;n<=3;++n)
                    expect(path[n-1]==Position{n*d.x,n*d.y,n*d.z});
                for(std::size_t j=0;j<i;++j) expect(d!=fire.moves[j].front());
            }
            expect(counts[1]==6 && counts[2]==12 && counts[3]==8);
        });
        // Зсуваємо старт так, щоб четвертий крок залишався на дошці:
        // відмова на ньому має пояснюватися дальністю вогню, а не виходом за межі.
        for(int dx=-1;dx<=1;++dx) for(int dy=-1;dy<=1;++dy) for(int dz=-1;dz<=1;++dz) {
            if(!dx && !dy && !dz) continue;
            Position from{dx<0 ? 5 : 4,dy<0 ? 5 : 4,dz<0 ? 5 : 4};
            auto step=[&](int n) { return Position{from.x+n*dx,from.y+n*dy,from.z+n*dz}; };
            test("fire ray "+std::to_string(dx)+","+std::to_string(dy)+","+std::to_string(dz),[&] {
                auto table=single(PieceType::Fire,Color::White,from);
                for(int n=1;n<=3;++n) {
                    Position to=step(n);
                    expect(bool(engine.movement(table,from,to)));
                    expect(engine.attacked(table,to,Color::White));
                    table.put(to,{PieceType::Soldier,Color::Black});
                    expect(bool(engine.movement(table,from,to)));
                    table.put(to,{PieceType::Soldier,Color::White});
                    expect(engine.movement(table,from,to).error==MoveError::FriendlyTarget);
                    table.remove(to);
                }
                expect(engine.movement(table,from,step(4)).error==MoveError::IllegalPath);
                expect(!engine.attacked(table,step(4),Color::White));
                for(Color blocker : {Color::White,Color::Black}) {
                    table.put(step(1),{PieceType::Knight,blocker});
                    for(int n : {2,3}) {
                        expect(engine.movement(table,from,step(n)).error==MoveError::IllegalPath);
                        expect(!engine.attacked(table,step(n),Color::White));
                    }
                    table.remove(step(1));
                    table.put(step(2),{PieceType::Knight,blocker});
                    expect(engine.movement(table,from,step(3)).error==MoveError::IllegalPath);
                    expect(!engine.attacked(table,step(3),Color::White));
                    table.remove(step(2));
                }
            });
        }
        // Окремо перевіряємо, як геометрія вогню впливає на шах і власного короля.
        test("fire gives space diagonal check at distance three",[&] {
            auto table=kings(); table.put({4,4,4},{PieceType::Fire,Color::Black});
            expect(engine.in_check(table,Color::White));
            table.put({2,2,2},{PieceType::Knight,Color::White});
            expect(!engine.in_check(table,Color::White));
        });
        test("fire movement still respects own king safety",[&] {
            auto table=kings(); table.put({2,2,2},{PieceType::Fire,Color::White});
            table.put({4,4,4},{PieceType::Queen,Color::Black});
            auto before=table;
            expect(engine.play(table,mv({2,2,2},{2,2,3})).error==MoveError::SelfCheck);
            expect(same_table(table,before));
        });
        // Незалежний перебір усіх Г-подібних орієнтацій коня у трьох площинах.
        for (int a=0;a<3;++a) for (int b=0;b<3;++b) if(a!=b)
            for (int signA : {-1,1}) for(int signB : {-1,1}) {
                int delta[3]={0,0,0}; delta[a]=signA*2; delta[b]=signB;
                Position to{4+delta[0],4+delta[1],4+delta[2]};
                test("knight jump to " + to.str(),[&] {
                    auto table=single(PieceType::Knight);
                    for(int x=3;x<=5;++x) for(int y=3;y<=5;++y) for(int z=3;z<=5;++z)
                        if(Position{x,y,z}!=Position{4,4,4}) table.put({x,y,z},{PieceType::Soldier,Color::White});
                    expect(bool(engine.movement(table,{4,4,4},to)));
                    table.put(to,{PieceType::Soldier,Color::Black});
                    expect(bool(engine.movement(table,{4,4,4},to)));
                    table.put(to,{PieceType::Soldier,Color::White});
                    expect(engine.movement(table,{4,4,4},to).error==MoveError::FriendlyTarget);
                });
            }

        // Порівнюємо всі 512 цілей із математичними правилами, а не зі списками JSON.
        // Це виявляє неправильні або відсутні траєкторії у самому файлі правил.
        for (auto type : {PieceType::Queen,PieceType::King,PieceType::Knight,PieceType::Fire,PieceType::Soldier,PieceType::Air})
            test(std::string("all 512 destinations for ")+symbol(type),[&] {
                auto table=single(type);
                for(int x=1;x<=8;++x) for(int y=1;y<=8;++y) for(int z=1;z<=8;++z) {
                    int d[3]={std::abs(x-4),std::abs(y-4),std::abs(z-4)};
                    int max=std::max({d[0],d[1],d[2]});
                    bool line=max>0 && (d[0]==0 || d[0]==max) && (d[1]==0 || d[1]==max) && (d[2]==0 || d[2]==max);
                    bool expected=false;
                    switch(type) {
                    case PieceType::Queen: expected=line; break;
                    case PieceType::King: expected=max==1; break;
                    case PieceType::Fire: expected=line && max<=3; break;
                    case PieceType::Soldier: expected=d[0]<=1 && d[1]==1 && d[2]==0; break;
                    case PieceType::Knight:
                        std::sort(d,d+3); expected=d[0]==0 && d[1]==1 && d[2]==2; break;
                    case PieceType::Air:
                        for(int axis=0;axis<3;++axis) {
                            int n=d[axis], a=d[(axis+1)%3], b=d[(axis+2)%3];
                            if(n>0 && (n%2 ? (a<=1 && b<=1 && a+b>0) : (a==0 && b==0)))
                                expected=true;
                        }
                        break;
                    default: break;
                    }
                    expect(bool(engine.movement(table,{4,4,4},{x,y,z}))==expected,
                           "Wrong destination "+Position{x,y,z}.str());
                }
            });

        // Військовий може рухатися прямо, але брати лише по діагоналі; обидва кольори однакові.
        for(Color color : {Color::White,Color::Black}) {
            test(color==Color::White ? "white soldier attacks" : "black soldier attacks",[&] {
                auto table=single(PieceType::Soldier,color);
                for(int dx : {-1,1}) for(int dy : {-1,1}) {
                    Position to{4+dx,4+dy,4};
                    expect(engine.attacked(table,to,color));
                    table.put(to,{PieceType::Queen,opposite(color)});
                    expect(bool(engine.movement(table,{4,4,4},to)));
                }
                expect(!engine.attacked(table,{4,5,4},color));
                table.put({4,5,4},{PieceType::Queen,opposite(color)});
                expect(engine.movement(table,{4,4,4},{4,5,4}).error==MoveError::IllegalPath);
            });
        }
        // Перешкоди перекривають дальні цілі, але свою фігуру на ближній цілі можна захищати.
        test("queen cannot jump friendly blocker",[&] {
            auto table=single(PieceType::Queen);
            table.put({5,5,5},{PieceType::Soldier,Color::White});
            expect(!engine.movement(table,{4,4,4},{7,7,7}));
        });
        test("fire cannot jump a blocker",[&] {
            auto table=single(PieceType::Fire);
            table.put({4,4,5},{PieceType::Soldier,Color::Black});
            expect(!engine.movement(table,{4,4,4},{4,4,7}));
            expect(bool(engine.movement(table,{4,4,4},{4,4,5})));
        });
        test("attack map includes friendly defended squares",[&] {
            auto table=single(PieceType::Queen);
            table.put({5,5,5},{PieceType::Soldier,Color::White});
            expect(engine.attacked(table,{5,5,5},Color::White));
            expect(!engine.attacked(table,{6,6,6},Color::White));
        });
        // Спеціально невизначене правило має давати явну помилку, а не вигадану безпеку.
        test("undefined rules remain explicit for custom configurations",[&] {
            MoveEngine pending(Rules::parse(replaced(json,"\"defined\": true",
                                                     "\"defined\": false, \"reason\": \"Test pending rule\"")));
            auto table=single(PieceType::Soldier);
            expect(pending.movement(table,{4,4,4},{4,5,4}).error==MoveError::UndefinedRule);
            throws([&] { pending.attacked(table,{1,1,1},Color::White); });
        });

        // Базові відмови вводу: межі, великі числа, та сама клітинка, порожній старт і чужа черга.
        test("out of bounds move",[&] { expect(engine.movement(kings(),{1,1,1},{0,1,1}).error==MoveError::OutOfBounds); });
        test("extreme integer input cannot overflow",[&] {
            expect(engine.movement(kings(),{1,1,1},{2147483647,-2147483647,1}).error==MoveError::OutOfBounds);
        });
        test("same square rejected",[&] { expect(engine.movement(kings(),{1,1,1},{1,1,1}).error==MoveError::SameSquare); });
        test("empty source rejected",[&] { expect(engine.movement(kings(),{2,2,2},{3,3,3}).error==MoveError::EmptySource); });
        test("black cannot move first",[&] { expect(engine.validate(kings(),mv({8,8,8},{7,8,8})).error==MoveError::WrongTurn); });
        // Успішний хід звільняє старт, займає ціль і передає чергу один раз.
        test("move switches turn and vacates source",[&] {
            auto table=kings();
            expect(bool(engine.play(table,mv({1,1,1},{2,1,1}))));
            expect(!table.at({1,1,1}) && table.at({2,1,1})->type==PieceType::King);
            expect(table.turn()==Color::Black);
            expect(bool(engine.play(table,mv({8,8,8},{7,8,8}))));
            expect(table.turn()==Color::White);
        });
        test("capture removes only target",[&] {
            auto table=kings(); table.put({4,4,4},{PieceType::Queen,Color::White});
            table.put({5,5,5},{PieceType::Soldier,Color::Black});
            expect(bool(engine.play(table,mv({4,4,4},{5,5,5}))));
            expect(table.count(Color::Black)==1 && table.count(Color::White)==2);
            expect(!table.at({4,4,4}) && table.at({5,5,5})->color==Color::White);
        });
        // Відмова та попередня перевірка не повинні змінювати справжню позицію.
        test("illegal move leaves entire board unchanged",[&] {
            auto table=kings(); auto before=table;
            expect(!engine.play(table,mv({1,1,1},{4,4,4})));
            expect(same_table(table,before));
        });
        test("validation is read only",[&] {
            auto table=kings(); auto before=table;
            expect(bool(engine.validate(table,mv({1,1,1},{2,1,1}))));
            expect(same_table(table,before));
        });
        // Повна гра потребує рівно одного короля кожної сторони; брати короля заборонено.
        test("missing king rejected",[&] {
            auto table=kings(); table.remove({8,8,8});
            expect(engine.validate(table,mv({1,1,1},{2,1,1})).error==MoveError::InvalidPosition);
            expect(engine.status(table)==GameStatus::InvalidPosition);
            throws([&] { engine.legal_moves(table); });
        });
        test("duplicate king rejected",[&] {
            auto table=kings(); table.put({3,3,3},{PieceType::King,Color::White});
            expect(engine.validate(table,mv({1,1,1},{2,1,1})).error==MoveError::InvalidPosition);
        });
        test("king cannot be captured",[&] {
            auto table=kings(); table.put({5,5,5},{PieceType::Queen,Color::White});
            expect(engine.validate(table,mv({5,5,5},{8,8,8})).error==MoveError::KingCapture);
        });
        test("kings cannot become neighbors",[&] {
            auto table=kings(); table.remove({8,8,8}); table.put({3,3,3},{PieceType::King,Color::Black});
            expect(engine.validate(table,mv({1,1,1},{2,2,2})).error==MoveError::SelfCheck);
        });
        // Зв'язана фігура не може піти, якщо після цього відкривається промінь до її короля.
        test("cannot expose own king",[&] {
            auto table=kings(); table.put({2,2,2},{PieceType::Queen,Color::White});
            table.put({4,4,4},{PieceType::Queen,Color::Black});
            expect(!engine.in_check(table,Color::White));
            auto before=table;
            expect(engine.play(table,mv({2,2,2},{2,3,2})).error==MoveError::SelfCheck);
            expect(same_table(table,before));
        });
        // Виходи з шаху: взяття атакувальної фігури або перекриття її променя.
        test("capture checking piece",[&] {
            auto table=kings(); table.put({2,2,2},{PieceType::Queen,Color::Black});
            expect(engine.in_check(table,Color::White));
            expect(bool(engine.play(table,mv({1,1,1},{2,2,2}))));
            expect(!engine.in_check(table,Color::White));
        });
        test("block a check",[&] {
            auto table=kings(); table.put({1,4,1},{PieceType::Queen,Color::Black});
            table.put({3,3,1},{PieceType::Queen,Color::White});
            expect(engine.in_check(table,Color::White));
            expect(bool(engine.play(table,mv({3,3,1},{1,3,1}))));
            expect(!engine.in_check(table,Color::White));
        });
        test("cannot ignore check",[&] {
            auto table=kings(); table.put({1,4,1},{PieceType::Queen,Color::Black});
            table.put({3,3,1},{PieceType::Queen,Color::White});
            expect(engine.validate(table,mv({3,3,1},{4,3,1})).error==MoveError::SelfCheck);
        });
        test("king capture cannot uncover a hidden attacker",[&] {
            auto table=kings(); table.put({2,2,2},{PieceType::Soldier,Color::Black});
            table.put({4,4,4},{PieceType::Queen,Color::Black});
            expect(engine.validate(table,mv({1,1,1},{2,2,2})).error==MoveError::SelfCheck);
        });
        // Перетворення перевіряється на відповідному останньому ряду обох кольорів.
        for (Color color : {Color::White,Color::Black})
            test(color==Color::White ? "white promotion" : "black promotion",[&] {
                auto table=kings(); table.set_turn(color);
                Position from{4,color==Color::White ? 7 : 2,4};
                Position to{4,color==Color::White ? 8 : 1,4};
                table.put(from,{PieceType::Soldier,color});
                expect(engine.validate(table,mv(from,to)).error==MoveError::PromotionRequired);
                expect(engine.validate(table,mv(from,to,PieceType::Queen)).error==MoveError::InvalidPromotion);
                expect(bool(engine.validate(table,mv(from,to,PieceType::Rook))));
                expect(bool(engine.validate(table,mv(from,to,PieceType::Bishop))));
                expect(bool(engine.play(table,mv(from,to,PieceType::Knight))));
                expect(table.at(to)->type==PieceType::Knight && !table.at(from));
            });
        test("promotion on capture",[&] {
            auto table=kings(); table.put({4,7,4},{PieceType::Soldier,Color::White});
            table.put({5,8,4},{PieceType::Queen,Color::Black});
            expect(bool(engine.play(table,mv({4,7,4},{5,8,4},PieceType::Knight))));
            expect(table.at({5,8,4})->type==PieceType::Knight && table.count(Color::Black)==1);
        });
        test("premature promotion rejected",[&] {
            auto table=kings(); table.put({4,4,4},{PieceType::Soldier,Color::White});
            expect(engine.validate(table,mv({4,4,4},{4,5,4},PieceType::Knight)).error==MoveError::InvalidPromotion);
        });
        test("non soldier promotion rejected",[&] {
            auto table=kings(); table.put({4,7,4},{PieceType::Queen,Color::White});
            expect(engine.validate(table,mv({4,7,4},{4,8,4},PieceType::Knight)).error==MoveError::InvalidPromotion);
        });
        // Навіть коли геометрія конкретної фігури відома, неповні правила інших блокують повну гру.
        test("incomplete custom game is explicit",[&] {
            MoveEngine pending(Rules::parse(replaced(json,"\"defined\": true",
                                                     "\"defined\": false, \"reason\": \"Test pending rule\"")));
            auto table=Table::initial(); auto before=table;
            expect(pending.status(table)==GameStatus::IncompleteRules);
            expect(pending.play(table,mv({4,1,1},{4,1,2})).error==MoveError::IncompleteRules);
            expect(same_table(table,before));
            throws([&] { pending.legal_moves(table); });
        });
        // Стандартний набір повний: початкова партія має дозволяти послідовні ходи обох сторін.
        test("all initial pieces have defined rules and the game can start",[&] {
            auto table=Table::initial();
            for(auto type : {PieceType::King,PieceType::Queen,PieceType::Rook,PieceType::Bishop,
                            PieceType::Knight,PieceType::Soldier,PieceType::Air,PieceType::Water,
                            PieceType::Stone,PieceType::Fire}) expect(rules.at(type).defined);
            expect(engine.status(table)==GameStatus::Active);
            expect(bool(engine.play(table,mv({1,2,1},{1,3,1}))));
            expect(bool(engine.play(table,mv({1,7,1},{1,6,1}))));
            expect(table.turn()==Color::White);
        });
        // Перевірки станів гри відрізняють шах, мат і пат за наявністю дозволених ходів.
        test("active game and legal moves",[&] {
            auto table=kings(); expect(engine.status(table)==GameStatus::Active);
            auto moves=engine.legal_moves(table);
            expect(moves.size()==7);
            for(const auto& move : moves) expect(bool(engine.validate(table,move)));
        });
        test("check status",[&] {
            auto table=kings(); table.put({1,4,1},{PieceType::Queen,Color::Black});
            expect(engine.status(table)==GameStatus::Check);
        });
        test("three dimensional checkmate",[&] {
            Table table; table.set_turn(Color::Black);
            table.put({1,1,1},{PieceType::King,Color::Black});
            table.put({3,3,3},{PieceType::King,Color::White});
            table.put({2,2,2},{PieceType::Queen,Color::White});
            expect(engine.status(table)==GameStatus::Checkmate);
            expect(engine.legal_moves(table).empty());
        });
        test("three dimensional stalemate",[&] {
            Table table; table.set_turn(Color::Black);
            table.put({1,1,1},{PieceType::King,Color::Black});
            table.put({3,3,3},{PieceType::King,Color::White});
            for(Position p : {Position{3,1,2},{1,3,2},{2,2,3}}) table.put(p,{PieceType::Queen,Color::White});
            expect(engine.status(table)==GameStatus::Stalemate);
            expect(engine.legal_moves(table).empty());
        });
        test("promotion appears in generated legal moves",[&] {
            auto table=kings(); table.put({4,7,4},{PieceType::Soldier,Color::White});
            auto moves=engine.legal_moves(table);
            int found=0;
            for(const auto& move : moves) if(move.from==Position{4,7,4} && move.to==Position{4,8,4}) {
                expect(move.promotion==PieceType::Knight || move.promotion==PieceType::Bishop || move.promotion==PieceType::Rook);
                ++found;
            }
            expect(found==3);
        });

        // Тура змінює одну координату; слон — дві на однакову величину.
        for(auto type : {PieceType::Rook,PieceType::Bishop})
            test(std::string("updated README geometry for ")+symbol(type),[&] {
                auto table=single(type);
                for(int x=1;x<=8;++x) for(int y=1;y<=8;++y) for(int z=1;z<=8;++z) {
                    int d[3]={std::abs(x-4),std::abs(y-4),std::abs(z-4)};
                    int nonzero=(d[0]>0)+(d[1]>0)+(d[2]>0), max=std::max({d[0],d[1],d[2]});
                    bool diagonal=(d[0]==0 || d[0]==max) && (d[1]==0 || d[1]==max) && (d[2]==0 || d[2]==max);
                    bool expected=type==PieceType::Rook ? nonzero==1 : nonzero==2 && diagonal;
                    expect(bool(engine.movement(table,{4,4,4},{x,y,z}))==expected);
                }
            });
        for(auto type : {PieceType::Rook,PieceType::Bishop})
            test(std::string("sliding captures and blockers for ")+symbol(type),[&] {
                auto table=single(type);
                Position mid=type==PieceType::Rook ? Position{4,4,5} : Position{4,5,5};
                Position to=type==PieceType::Rook ? Position{4,4,7} : Position{4,7,7};
                table.put(to,{PieceType::Soldier,Color::Black});
                expect(bool(engine.movement(table,{4,4,4},to)));
                for(Color color : {Color::White,Color::Black}) {
                    table.put(mid,{PieceType::Soldier,color});
                    expect(!engine.movement(table,{4,4,4},to));
                    expect(!engine.attacked(table,to,Color::White));
                }
            });
        test("bishop never follows a three coordinate diagonal",[&] {
            auto table=single(PieceType::Bishop);
            expect(!engine.movement(table,{4,4,4},{5,5,5}));
            expect(!engine.attacked(table,{7,7,7},Color::White));
        });

        // Камінь має різні множини звичайного руху й атак: 6 сусідів та 98 точок сфери.
        test("stone has six movement offsets and 98 attack offsets",[&] {
            const auto& stone=rules.at(PieceType::Stone);
            expect(stone.defined && stone.jumps && !stone.stop_anywhere);
            expect(stone.moves.size()==6 && stone.attacks.size()==98);
            auto table=single(PieceType::Stone);
            int moves=0, attacks=0;
            for(int x=1;x<=8;++x) for(int y=1;y<=8;++y) for(int z=1;z<=8;++z) {
                int dx=x-4,dy=y-4,dz=z-4;
                bool expected_move=std::abs(dx)+std::abs(dy)+std::abs(dz)==1;
                bool expected_attack=std::round(std::sqrt(double(dx*dx+dy*dy+dz*dz)))==3;
                bool actual_move=bool(engine.movement(table,{4,4,4},{x,y,z}));
                bool actual_attack=engine.attacked(table,{x,y,z},Color::White);
                expect(actual_move==expected_move && actual_attack==expected_attack);
                moves+=actual_move ? 1 : 0; attacks+=actual_attack ? 1 : 0;
            }
            expect(moves==6 && attacks==98);
        });
        // Явний список із малюнка перевіряє плоский зріз сфери незалежно від її генерації.
        test("stone XY ring matches the sixteen marked image cells",[&] {
            const std::vector<Position> ring{{-3,-1,0},{-3,0,0},{-3,1,0},{-2,-2,0},{-2,2,0},
                {-1,-3,0},{-1,3,0},{0,-3,0},{0,3,0},{1,-3,0},{1,3,0},
                {2,-2,0},{2,2,0},{3,-1,0},{3,0,0},{3,1,0}};
            auto table=single(PieceType::Stone);
            int marked=0;
            for(int x=1;x<=8;++x) for(int y=1;y<=8;++y) {
                Position offset{x-4,y-4,0};
                bool expected=std::find(ring.begin(),ring.end(),offset)!=ring.end();
                expect(engine.attacked(table,{x,y,4},Color::White)==expected);
                if(expected) ++marked;
            }
            expect(marked==16);
        });
        // На кожній точці сфери можна взяти ворога, але не стати на порожню чи свою клітинку.
        for(int dx=-3;dx<=3;++dx) for(int dy=-3;dy<=3;++dy) for(int dz=-3;dz<=3;++dz) {
            if(std::round(std::sqrt(double(dx*dx+dy*dy+dz*dz)))!=3) continue;
            Position to{4+dx,4+dy,4+dz};
            test("stone sphere capture at "+to.str(),[&] {
                auto table=single(PieceType::Stone);
                expect(engine.movement(table,{4,4,4},to).error==MoveError::IllegalPath);
                table.put(to,{PieceType::Soldier,Color::Black});
                expect(bool(engine.movement(table,{4,4,4},to)));
                table.put(to,{PieceType::Soldier,Color::White});
                expect(engine.movement(table,{4,4,4},to).error==MoveError::FriendlyTarget);
            });
        }
        for(Position to : {Position{3,4,4},{5,4,4},{4,3,4},{4,5,4},{4,4,3},{4,4,5}})
            test("stone adjacent move cannot capture at "+to.str(),[&] {
                auto table=kings(); table.put({4,4,4},{PieceType::Stone,Color::White});
                auto occupied=table; occupied.put(to,{PieceType::Soldier,Color::Black});
                expect(engine.validate(occupied,mv({4,4,4},to)).error==MoveError::IllegalPath);
                expect(bool(engine.play(table,mv({4,4,4},to))));
                expect(table.at(to)->type==PieceType::Stone && !table.at({4,4,4}));
            });
        // Звичайний стрибок каменя залишає всі проміжні фігури на місці.
        test("stone capture jumps over surrounding pieces without taking them",[&] {
            auto table=kings(); table.put({4,4,4},{PieceType::Stone,Color::White});
            for(int x=3;x<=5;++x) for(int y=3;y<=5;++y) for(int z=3;z<=5;++z)
                if(Position{x,y,z}!=Position{4,4,4}) table.put({x,y,z},{PieceType::Knight,Color::White});
            table.put({7,5,5},{PieceType::Soldier,Color::Black});
            expect(bool(engine.play(table,mv({4,4,4},{7,5,5}))));
            expect(table.count(Color::White)==28 && table.count(Color::Black)==1);
            expect(table.at({7,5,5})->type==PieceType::Stone);
        });
        test("stone attack sphere is clipped at the board boundary",[&] {
            auto table=single(PieceType::Stone,Color::White,{1,1,1});
            for(int x=1;x<=8;++x) for(int y=1;y<=8;++y) for(int z=1;z<=8;++z) {
                int dx=x-1,dy=y-1,dz=z-1;
                bool expected=std::round(std::sqrt(double(dx*dx+dy*dy+dz*dz)))==3;
                expect(engine.attacked(table,{x,y,z},Color::White)==expected);
            }
            expect(engine.movement(table,{1,1,1},{-2,1,1}).error==MoveError::OutOfBounds);
        });
        test("stone sphere gives check while adjacent green squares do not",[&] {
            auto table=kings(); table.remove({1,1,1});
            table.put({7,5,5},{PieceType::King,Color::White});
            table.put({4,4,4},{PieceType::Stone,Color::Black});
            expect(engine.in_check(table,Color::White));
            table.remove({7,5,5}); table.put({5,4,4},{PieceType::King,Color::White});
            expect(!engine.in_check(table,Color::White));
        });
        test("stone cannot take king on its attack sphere",[&] {
            auto table=kings(); table.remove({8,8,8});
            table.put({7,5,5},{PieceType::King,Color::Black});
            table.put({4,4,4},{PieceType::Stone,Color::White});
            auto before=table;
            expect(engine.play(table,mv({4,4,4},{7,5,5})).error==MoveError::KingCapture);
            expect(same_table(table,before));
        });
        test("stone move cannot expose its own king",[&] {
            auto table=kings(); table.put({3,3,3},{PieceType::Stone,Color::White});
            table.put({4,4,4},{PieceType::Queen,Color::Black});
            auto before=table;
            expect(engine.play(table,mv({3,3,3},{3,3,4})).error==MoveError::SelfCheck);
            expect(same_table(table,before));
        });

        // Підтверджена просторова дуга води: 444 → 555 → 556 → 447.
        test("water reference spatial crescent",[&] {
            const auto& rule=rules.at(PieceType::Water);
            expect(rule.defined && !rule.jumps && !rule.stop_anywhere);
            expect(rule.moves.size()==48 && rule.attacks==rule.moves);
            auto it=std::find(rule.path_ids.begin(),rule.path_ids.end(),"+Z:5");
            expect(it!=rule.path_ids.end());
            std::vector<Position> offsets{{1,1,1},{1,1,2},{0,0,3}};
            expect(rule.moves[static_cast<std::size_t>(it-rule.path_ids.begin())]==offsets);
            auto table=single(PieceType::Water);
            auto move=routed_move({4,4,4}, {4,4,7}, "+Z", 5);
            expect(bool(engine.movement(table,move)));
            expect(!engine.movement(table,{4,4,4},{5,5,5}));
            expect(!engine.movement(table,{4,4,4},{5,5,6}));
        });
        // Різні дуги мають лише шість кінцевих точок, віддалених на 3 по основній осі.
        test("water geometry has exactly six possible endpoints",[&] {
            auto table=single(PieceType::Water);
            int destinations=0;
            for(int x=1;x<=8;++x) for(int y=1;y<=8;++y) for(int z=1;z<=8;++z) {
                int d[3]={std::abs(x-4),std::abs(y-4),std::abs(z-4)};
                bool expected=(d[0]==3 && d[1]==0 && d[2]==0)
                           || (d[0]==0 && d[1]==3 && d[2]==0)
                           || (d[0]==0 && d[1]==0 && d[2]==3);
                bool actual=bool(engine.movement(table,{4,4,4},{x,y,z}));
                expect(actual==expected);
                if(actual) ++destinations;
            }
            expect(destinations==6);
        });
        // Вісім поперечних відхилень дають 48 маршрутів у шести основних напрямках.
        const int water_sides[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
        for(int axis=0;axis<3;++axis) for(int sign : {1,-1}) for(int variant=1;variant<=8;++variant) {
            std::string direction=(sign==1 ? "+" : "-")+std::string(1,"XYZ"[axis]);
            test("water sweep arc "+direction+":"+std::to_string(variant),[&] {
                int other[2], count=0;
                for(int i=0;i<3;++i) if(i!=axis) other[count++]=i;
                Position cells[3];
                for(int n=1;n<=3;++n) {
                    int xyz[3]={4,4,4}; xyz[axis]+=sign*n;
                    if(n<3) {
                        xyz[other[0]]+=water_sides[variant-1][0];
                        xyz[other[1]]+=water_sides[variant-1][1];
                    }
                    cells[n-1]={xyz[0],xyz[1],xyz[2]};
                }
                auto table=kings(); table.put({4,4,4},{PieceType::Water,Color::White});
                auto move=mv({4,4,4},cells[2]); move.path=PathSelection{direction,variant};
                for(int i=0;i<2;++i) expect(!engine.movement(table,{4,4,4},cells[i]));
                for(Position p : cells) {
                    expect(engine.attacked(table,p,Color::White));
                    table.put(p,{PieceType::Soldier,Color::Black});
                }
                auto before=table;
                expect(bool(engine.validate(table,move)));
                expect(same_table(table,before));
                expect(bool(engine.play(table,move)));
                expect(table.count(Color::Black)==1 && table.count(Color::White)==2);
                expect(!table.at(cells[0]) && !table.at(cells[1]) && !table.at({4,4,4}));
                expect(table.at(cells[2])->type==PieceType::Water && table.turn()==Color::Black);
            });
        }
        // Дуги зі спільним кінцем можуть мати різних жертв, тому вибір має бути явним.
        test("water requires an explicit route for full moves",[&] {
            auto table=kings(); table.put({4,4,4},{PieceType::Water,Color::White});
            auto before=table;
            expect(bool(engine.movement(table,{4,4,4},{4,4,7})));
            expect(engine.play(table,mv({4,4,4},{4,4,7})).error==MoveError::PathRequired);
            expect(same_table(table,before));
        });
        // Своя фігура на будь-якому кроці скасовує всю дію, включно з уже запланованими взяттями.
        for(Position blocker : {Position{5,5,5},{5,5,6},{4,4,7}})
            test("water ally blocker at "+blocker.str(),[&] {
                auto table=kings(); table.put({4,4,4},{PieceType::Water,Color::White});
                table.put({5,5,5},{PieceType::Soldier,Color::Black});
                table.put({5,5,6},{PieceType::Soldier,Color::Black});
                table.put(blocker,{PieceType::Soldier,Color::White});
                auto before=table;
                expect(!engine.play(table,routed_move({4,4,4}, {4,4,7}, "+Z", 5)));
                expect(same_table(table,before));
            });
        test("water captures only its selected arc",[&] {
            auto table=kings(); table.put({4,4,4},{PieceType::Water,Color::White});
            table.put({5,5,5},{PieceType::Soldier,Color::Black});
            table.put({5,5,6},{PieceType::Soldier,Color::Black});
            table.put({3,3,5},{PieceType::Soldier,Color::Black});
            expect(bool(engine.play(table,routed_move({4,4,4}, {4,4,7}, "+Z", 5))));
            expect(!table.at({5,5,5}) && !table.at({5,5,6}));
            expect(table.at({3,3,5})->color==Color::Black);
        });
        test("alternate water arcs give different captures at the same endpoint",[&] {
            auto a=kings(); a.put({4,4,4},{PieceType::Water,Color::White});
            a.put({5,5,5},{PieceType::Soldier,Color::Black});
            a.put({3,3,5},{PieceType::Soldier,Color::Black});
            auto b=a;
            expect(bool(engine.play(a,routed_move({4,4,4}, {4,4,7}, "+Z", 5))));
            expect(bool(engine.play(b,routed_move({4,4,4}, {4,4,7}, "+Z", 8))));
            expect(!a.at({5,5,5}) && a.at({3,3,5}));
            expect(!b.at({3,3,5}) && b.at({5,5,5}));
        });
        // Від'єднання модуля повертає звичайне блокування ворогами на проміжних точках.
        test("disconnecting multicapture restores ordinary water blockers",[&] {
            MoveEngine ordinary(rules); ordinary.set_module("multicapture",nullptr);
            auto table=kings(); table.put({4,4,4},{PieceType::Water,Color::White});
            table.put({5,5,5},{PieceType::Soldier,Color::Black});
            expect(!ordinary.validate(table,routed_move({4,4,4}, {4,4,7}, "+Z", 5)));
            expect(!ordinary.attacked(table,{5,5,6},Color::White));
            table.remove({5,5,5});
            table.put({4,4,7},{PieceType::Soldier,Color::Black});
            expect(bool(ordinary.play(table,routed_move({4,4,4}, {4,4,7}, "+Z", 5))));
            expect(table.count(Color::Black)==1);
        });
        test("multicapture does not make fire jump",[&] {
            auto table=single(PieceType::Fire); table.put({5,5,5},{PieceType::Soldier,Color::Black});
            expect(!engine.movement(table,{4,4,4},{7,7,7}));
        });
        // Можливість не прив'язана назавжди до води: модуль можна налаштувати на інший тип.
        test("multicapture capability can be attached to a chosen piece",[&] {
            MoveEngine custom(rules);
            custom.set_module("multicapture",std::make_shared<MultiCaptureModule>(std::vector<PieceType>{PieceType::Fire}));
            auto table=kings(); table.put({4,4,4},{PieceType::Fire,Color::White});
            table.put({5,5,5},{PieceType::Soldier,Color::Black});
            table.put({6,6,6},{PieceType::Soldier,Color::Black});
            expect(bool(custom.play(table,mv({4,4,4},{7,7,7}))));
            expect(!table.at({5,5,5}) && !table.at({6,6,6}));
        });
        // Короля не можна зняти навіть на проміжному кроці множинного взяття.
        for(Position king : {Position{5,5,5},{5,5,6},{4,4,7}})
            test("multicapture cannot take king at "+king.str(),[&] {
                auto table=kings(); table.remove({8,8,8});
                table.put(king,{PieceType::King,Color::Black});
                table.put({4,4,4},{PieceType::Water,Color::White});
                auto before=table;
                expect(engine.play(table,routed_move({4,4,4}, {4,4,7}, "+Z", 5)).error==MoveError::KingCapture);
                expect(same_table(table,before));
            });
        test("intermediate capture can give check",[&] {
            auto table=kings(); table.remove({1,1,1}); table.put({4,4,4},{PieceType::King,Color::White});
            table.put({3,3,3},{PieceType::Water,Color::Black});
            expect(engine.in_check(table,Color::White));
        });
        test("water cannot threaten an intermediate square without a legal finish",[&] {
            auto table=single(PieceType::Water);
            expect(engine.attacked(table,{5,5,6},Color::White));
            table.put({4,4,7},{PieceType::Soldier,Color::White});
            expect(!engine.attacked(table,{5,5,6},Color::White));
        });
        test("water route cannot leave board and return",[&] {
            auto table=single(PieceType::Water,Color::White,{8,4,4});
            expect(!engine.movement(table,routed_move({8,4,4}, {8,4,7}, "+Z", 1)));
            expect(bool(engine.movement(table,routed_move({8,4,4}, {8,4,7}, "+Z", 2))));
        });
        // Симуляція повинна врахувати зняття всіх проміжних жертв перед перевіркою шаху.
        test("removing an intermediate checking piece is included in safety simulation",[&] {
            auto table=kings(); table.put({4,4,4},{PieceType::Water,Color::White});
            table.put({5,5,5},{PieceType::Queen,Color::Black});
            expect(bool(engine.play(table,routed_move({4,4,4}, {4,4,7}, "+Z", 5))));
            expect(!table.at({5,5,5}) && !engine.in_check(table,Color::White));
        });
        test("intermediate removal cannot expose own king",[&] {
            auto table=kings(); table.put({2,4,2},{PieceType::Water,Color::White});
            table.put({3,3,3},{PieceType::Soldier,Color::Black});
            table.put({4,4,4},{PieceType::Queen,Color::Black});
            expect(!engine.in_check(table,Color::White));
            auto before=table;
            expect(engine.play(table,routed_move({2,4,2}, {2,4,5}, "+Z", 6)).error==MoveError::SelfCheck);
            expect(same_table(table,before));
        });
        test("water legal moves retain all available arcs",[&] {
            auto table=kings(); table.put({4,4,4},{PieceType::Water,Color::White});
            table.put({5,5,5},{PieceType::Soldier,Color::Black});
            int water=0, shared=0;
            for(const auto& move : engine.legal_moves(table)) if(move.from==Position{4,4,4}) {
                expect(move.path.has_value() && bool(engine.validate(table,move)));
                ++water; if(move.to==Position{4,4,7}) ++shared;
            }
            expect(water==48 && shared==8);
        });

        // Два маршрути повітря від автора зі старту 444; перевіряємо кожну дозволену зупинку.
        test("air reference route using two coordinates",[&] {
            auto table=single(PieceType::Air);
            for(Position to : {Position{4,5,5},{4,4,6},{4,5,7},{4,4,8}}) {
                Move move=mv({4,4,4},to); move.path=PathSelection{"+Z",3};
                expect(bool(engine.movement(table,move)));
                expect(engine.attacked(table,to,Color::White));
            }
        });
        test("air reference route using three coordinates",[&] {
            auto table=single(PieceType::Air);
            for(Position to : {Position{5,5,5},{4,4,6},{5,5,7},{4,4,8}}) {
                Move move=mv({4,4,4},to); move.path=PathSelection{"+Z",5};
                expect(bool(engine.movement(table,move)));
                expect(engine.attacked(table,to,Color::White));
            }
        });
        // Повертаємо й віддзеркалюємо зиґзаґ у всі шість основних напрямків.
        const int transverse[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
        for(int axis=0;axis<3;++axis) for(int sign : {1,-1}) for(int variant=1;variant<=8;++variant) {
            std::string direction=(sign==1 ? "+" : "-")+std::string(1,"XYZ"[axis]);
            test("air full route " + direction + ":" + std::to_string(variant),[&] {
                int other[2], count=0;
                for(int i=0;i<3;++i) if(i!=axis) other[count++]=i;
                int start[3]={4,4,4}; start[axis]=sign==1 ? 1 : 8;
                Position from{start[0],start[1],start[2]};
                auto table=single(PieceType::Air,Color::White,from);
                for(int n=1;n<=7;++n) {
                    int xyz[3]={start[0],start[1],start[2]};
                    xyz[axis]+=sign*n;
                    xyz[other[0]]+=(n%2)*transverse[variant-1][0];
                    xyz[other[1]]+=(n%2)*transverse[variant-1][1];
                    Position to{xyz[0],xyz[1],xyz[2]};
                    Move move=mv(from,to); move.path=PathSelection{direction,variant};
                    expect(bool(engine.movement(table,move)),"Cannot stop at "+to.str());
                    table.put(to,{PieceType::Soldier,Color::Black});
                    const MoveError expected = from.y<=4 && to.y==8 ? MoveError::ProtectedHome : MoveError::None;
                    expect(engine.movement(table,move).error==expected,"Unexpected capture result at "+to.str());
                    table.remove(to);
                    table.put(to,{PieceType::Soldier,Color::White});
                    expect(engine.movement(table,move).error==MoveError::FriendlyTarget);
                    table.remove(to);
                }
            });
        }
        // Повітря перестрибує свої фігури й забирає ворогів лише на вибраному маршруті.
        test("air captures enemies along its selected route and jumps over allies",[&] {
            auto table=kings(); table.put({4,4,4},{PieceType::Air,Color::White});
            table.put({5,5,5},{PieceType::Soldier,Color::White});
            table.put({4,4,6},{PieceType::Soldier,Color::Black});
            table.put({5,5,7},{PieceType::Knight,Color::Black});
            table.put({4,4,8},{PieceType::Soldier,Color::Black});
            table.put({4,5,7},{PieceType::Soldier,Color::Black});
            auto move=mv({4,4,4},{4,4,8});
            auto before=table;
            expect(engine.play(table,move).error==MoveError::PathRequired);
            expect(same_table(table,before));
            move.path=PathSelection{"+Z",5};
            expect(bool(engine.play(table,move)));
            before.remove({4,4,4});
            before.remove({4,4,6});
            before.remove({5,5,7});
            before.put({4,4,8},{PieceType::Air,Color::White});
            before.set_turn(Color::Black);
            expect(same_table(table,before));
        });
        test("air cannot take a king along the route",[&] {
            auto table=kings();
            table.remove({8,8,8});
            table.put({4,4,6},{PieceType::King,Color::Black});
            table.put({4,4,4},{PieceType::Air,Color::White});
            auto move=mv({4,4,4},{4,4,8}); move.path=PathSelection{"+Z",5};
            expect(engine.in_check(table,Color::Black));
            auto before=table;
            expect(engine.play(table,move).error==MoveError::KingCapture);
            expect(same_table(table,before));
        });
        test("air cannot switch to another path mid move",[&] {
            auto table=single(PieceType::Air);
            auto move=mv({4,4,4},{4,5,7}); move.path=PathSelection{"+Z",5};
            expect(engine.movement(table,move).error==MoveError::InvalidPath);
            move.path=PathSelection{"+Z",3};
            expect(bool(engine.movement(table,move)));
        });
        test("air can choose any compatible route to a shared endpoint",[&] {
            auto table=single(PieceType::Air);
            for(int variant=1;variant<=8;++variant) {
                auto move=mv({4,4,4},{4,4,6}); move.path=PathSelection{"+Z",variant};
                expect(bool(engine.movement(table,move)));
            }
        });
        test("air path cannot leave board then return",[&] {
            auto table=single(PieceType::Air,Color::White,{8,4,4});
            auto move=mv({8,4,4},{8,4,6}); move.path=PathSelection{"+Z",1};
            expect(engine.movement(table,move).error==MoveError::InvalidPath);
            move.path=PathSelection{"+Z",2};
            expect(bool(engine.movement(table,move)));
        });
        test("selected path cannot bypass self check",[&] {
            auto table=kings(); table.put({2,2,2},{PieceType::Air,Color::White});
            table.put({4,4,4},{PieceType::Queen,Color::Black});
            auto move=mv({2,2,2},{2,3,3}); move.path=PathSelection{"+Z",3};
            expect(engine.validate(table,move).error==MoveError::SelfCheck);
        });
        test("air checks king through intermediate blocker",[&] {
            auto table=kings(); table.put({3,1,1},{PieceType::Air,Color::Black});
            table.put({2,2,2},{PieceType::Soldier,Color::White});
            expect(engine.in_check(table,Color::White));
        });
        test("legal air moves include selectable paths",[&] {
            auto table=kings(); table.put({4,4,4},{PieceType::Air,Color::White});
            int shared=0;
            for(const auto& move : engine.legal_moves(table)) if(move.from==Position{4,4,4}) {
                expect(move.path.has_value());
                expect(bool(engine.validate(table,move)));
                if(move.to==Position{4,4,6}) ++shared;
            }
            expect(shared==8);
        });
        // Порт модуля можна від'єднати та підключити знову без перебудови двигуна.
        test("path module can be disconnected and restored",[&] {
            MoveEngine custom(rules); auto table=kings(); table.put({4,4,4},{PieceType::Air,Color::White});
            auto move=mv({4,4,4},{5,5,5}); move.path=PathSelection{"+Z",5};
            custom.set_module("path",nullptr);
            expect(custom.validate(table,move).error==MoveError::MissingModule);
            move.path.reset();
            expect(custom.validate(table,move).error==MoveError::PathRequired);
            custom.set_module("multicapture",nullptr);
            expect(bool(custom.validate(table,move)));
            bool found=false;
            for(const auto& legal : custom.legal_moves(table))
                if(legal.from==Position{4,4,4} && legal.to==Position{5,5,5}) {
                    expect(!legal.path); found=true;
                }
            expect(found);
            custom.set_module("path",std::make_shared<PathModule>());
            move.path=PathSelection{"+Z",5}; expect(bool(custom.validate(table,move)));
        });

        // Прев’ю має показувати саме клітинки та жертв, які відповідають виконаному ходу.
        auto route_scenario = [&](const std::string& name, Table table, const Move& move,
                                  MoveError error, std::vector<Position> route = {},
                                  std::vector<Position> captures = {}) {
            test("route preview: " + name,[&] {
                const auto before = table;
                const auto details = engine.preview(table,move);
                expect(same_table(table,before),"Preview changed the position");
                expect(engine.validate(table,move).error==error,"Unexpected validation result");
                expect(details.has_value()==(error==MoveError::None),"Unexpected preview availability");
                if (!details) {
                    expect(engine.play(table,move).error==error);
                    expect(same_table(table,before),"Rejected move changed the position");
                    return;
                }
                expect(details->route==route,"Incorrect sequence of visited squares");
                expect(details->captures==captures,"Incorrect list of captures");
                auto expected=before;
                for (Position victim : captures) expected.remove(victim);
                Piece moved = *expected.at(move.from);
                if (move.promotion) moved.type = *move.promotion;
                expected.remove(move.from);
                expected.put(move.to,moved);
                expected.set_turn(opposite(expected.turn()));
                expect(bool(engine.play(table,move)) && same_table(table,expected),"Move differs from its preview");
            });
        };
        auto water=kings();
        water.put({4,4,4},{PieceType::Water,Color::White});
        water.put({5,5,5},{PieceType::Soldier,Color::Black});
        water.put({5,5,6},{PieceType::Knight,Color::Black});
        water.put({3,3,5},{PieceType::Soldier,Color::Black});
        route_scenario("water takes two pieces on the right arc", water, routed_move({4,4,4}, {4,4,7}, "+Z", 5), MoveError::None,
                       {{5,5,5},{5,5,6},{4,4,7}}, {{5,5,5},{5,5,6}});
        route_scenario("water takes a different piece on the left arc", water, routed_move({4,4,4}, {4,4,7}, "+Z", 8), MoveError::None,
                       {{3,3,5},{3,3,6},{4,4,7}}, {{3,3,5}});
        route_scenario("water can choose a quiet arc to the same square", water, routed_move({4,4,4}, {4,4,7}, "+Z", 1), MoveError::None,
                       {{5,4,5},{5,4,6},{4,4,7}});
        route_scenario("water requires a route", water, mv({4,4,4}, {4,4,7}), MoveError::PathRequired);
        route_scenario("water cannot stop halfway", water, routed_move({4,4,4}, {5,5,5}, "+Z", 5), MoveError::IllegalPath);
        water.put({5,5,6},{PieceType::Knight,Color::White});
        route_scenario("water cannot cross an allied blocker", water, routed_move({4,4,4}, {4,4,7}, "+Z", 5), MoveError::InvalidPath);
        water.put({4,4,7},{PieceType::Soldier,Color::Black});
        route_scenario("water takes both a route victim and its target", water, routed_move({4,4,4}, {4,4,7}, "+Z", 8), MoveError::None,
                       {{3,3,5},{3,3,6},{4,4,7}}, {{3,3,5},{4,4,7}});

        auto air=kings();
        air.put({4,4,4},{PieceType::Air,Color::White});
        air.put({5,5,5},{PieceType::Soldier,Color::White});
        air.put({4,4,6},{PieceType::Soldier,Color::Black});
        air.put({5,5,7},{PieceType::Knight,Color::Black});
        air.put({3,3,5},{PieceType::Bishop,Color::Black});
        route_scenario("air jumps an ally and stops at its first capture", air, routed_move({4,4,4}, {4,4,6}, "+Z", 5), MoveError::None,
                       {{5,5,5},{4,4,6}}, {{4,4,6}});
        route_scenario("air reaches the same square with two captures", air, routed_move({4,4,4}, {4,4,6}, "+Z", 8), MoveError::None,
                       {{3,3,5},{4,4,6}}, {{3,3,5},{4,4,6}});
        route_scenario("longer air route includes the extra victim", air, routed_move({4,4,4}, {4,4,8}, "+Z", 5), MoveError::None,
                       {{5,5,5},{4,4,6},{5,5,7},{4,4,8}}, {{4,4,6},{5,5,7}});
        route_scenario("air cannot stop on an ally", air, routed_move({4,4,4}, {5,5,5}, "+Z", 5), MoveError::FriendlyTarget);
        route_scenario("air cannot switch routes mid-move", air, routed_move({4,4,4}, {5,5,7}, "+Z", 8), MoveError::InvalidPath);
        air.put({8,4,4},{PieceType::Air,Color::White});
        route_scenario("air cannot reenter after leaving the board", air, routed_move({8,4,4}, {8,4,6}, "+Z", 1), MoveError::InvalidPath);
        air.remove({8,8,8});
        air.put({4,4,6},{PieceType::King,Color::Black});
        route_scenario("intermediate king cannot appear as a legal capture", air, routed_move({4,4,4}, {4,4,8}, "+Z", 5), MoveError::KingCapture);

        auto opening=Table::initial();
        route_scenario("white air cannot take protected water", opening, routed_move({1,1,1}, {2,8,1}, "+Y", 1), MoveError::ProtectedHome);
        route_scenario("white air enters the empty home and takes the front soldier", opening,
                       routed_move({1,1,1}, {1,8,2}, "+Y", 3),
                       MoveError::None, {{1,2,2},{1,3,1},{1,4,2},{1,5,1},{1,6,2},{1,7,1},{1,8,2}}, {{1,7,1}});
        route_scenario("white bishop enters the former water square", opening, mv({3,1,8}, {3,8,1}), MoveError::None,
                       {{3,2,7},{3,3,6},{3,4,5},{3,5,4},{3,6,3},{3,7,2},{3,8,1}});
        route_scenario("black route is unavailable before white moves", opening, routed_move({1,8,1}, {1,1,2}, "-Y", 3), MoveError::WrongTurn);
        opening.set_turn(Color::Black);
        route_scenario("black air cannot take protected water", opening, routed_move({1,8,1}, {2,1,1}, "-Y", 1), MoveError::ProtectedHome);
        route_scenario("black air enters the empty home and takes the front soldier", opening,
                       routed_move({1,8,1}, {1,1,2}, "-Y", 3),
                       MoveError::None, {{1,7,2},{1,6,1},{1,5,2},{1,4,1},{1,3,2},{1,2,1},{1,1,2}}, {{1,2,1}});
        auto pinned=kings();
        pinned.put({2,2,2},{PieceType::Air,Color::White});
        pinned.put({4,4,4},{PieceType::Queen,Color::Black});
        route_scenario("a route that exposes the king has no preview", pinned, routed_move({2,2,2}, {2,3,3}, "+Z", 3), MoveError::SelfCheck);

        // Тестовий модуль забороняє всі ходи: це перевіряє механізм додаткових обмежень.
        test("additional module adds constraints without changing engine",[&] {
            struct Deny final : MoveModule {
                Validation validate(const Table&,const Move&,const Rules&) const override {
                    return {MoveError::IllegalPath,"Disabled by test module"};
                }
            };
            MoveEngine custom(rules); auto table=kings(); auto before=table;
            custom.set_module("test",std::make_shared<Deny>());
            expect(!custom.play(table,mv({1,1,1},{2,1,1})));
            expect(same_table(table,before));
            custom.set_module("test",nullptr);
            expect(bool(custom.play(table,mv({1,1,1},{2,1,1}))));
        });
        test("path selection on unsupported piece rejected",[&] {
            auto move=mv({1,1,1},{2,1,1}); move.path=PathSelection{"+X",1};
            expect(engine.validate(kings(),move).error==MoveError::InvalidPath);
        });
        // Некоректні значення можуть надходити і з коду, а не лише з консольного рядка.
        for(PathSelection selection : {PathSelection{"+Z",0},{"+Z",9},{"Z",1},{"+A",1},{"+Z",2147483647}})
            test("invalid programmatic path "+selection.direction+std::to_string(selection.variant),[&] {
                auto table=single(PieceType::Air);
                auto move=mv({4,4,4},{5,5,5}); move.path=selection;
                expect(engine.movement(table,move).error==MoveError::InvalidPath);
            });
#ifdef ND_TEST_CONSOLE_INPUT
        // Перевірка синтаксису маршруту та збереження старого формату звичайних ходів.
        test("console parses user's two paths",[&] {
            auto a=parse_move("444 457 path +Z 3"), b=parse_move("444 557 path +Z 5");
            expect(a.path->id()=="+Z:3" && b.path->id()=="+Z:5");
            auto table=single(PieceType::Air);
            expect(bool(engine.movement(table,a)) && bool(engine.movement(table,b)));
        });
        test("old console moves and promotion still parse",[] {
            expect(!parse_move("111 211").path);
            expect(parse_move("474 484 N").promotion==PieceType::Knight);
        });
        for(const std::string text : {"", "444", "444 999", "444 555 path", "444 555 path +Z",
              "444 555 path +Z 0", "444 555 path +Z 9", "444 555 path +Z 1junk",
              "444 555 path +Z 01", "444 555 path Z 1", "444 555 path +A 1",
              "444 555 path +Z 1 extra", "444 555 Q", "444 555 path +Z -1"})
            test("invalid console move '"+text+"'",[&] { throws([&] { parse_move(text); }); });
#endif

        // Некоректні файли, типи полів, зміщення та дублікати відхиляються під час завантаження.
        test("missing rules file",[] { throws([] { Rules::load("__missing_nd_rules__.json"); }); });
        for(const std::string malformed : {"", "{", "[]", "{}", "{\"version\":1,}", "{\"x\":1,\"x\":2}"})
            test("malformed JSON " + malformed,[&] { throws([&] { Rules::parse(malformed); }); });
        test("trailing JSON rejected",[&] { throws([&] { Rules::parse(json+"false"); }); });
        test("unsupported board size",[&] { throws([&] { Rules::parse(replaced(json,"\"board_size\": 8","\"board_size\": 9")); }); });
        test("unsupported version",[&] { throws([&] { Rules::parse(replaced(json,"\"version\": 1","\"version\": 2")); }); });
        test("wrong field type",[&] { throws([&] { Rules::parse(replaced(json,"\"version\": 1","\"version\": true")); }); });
        test("duplicate JSON key",[&] { throws([&] { Rules::parse(replaced(json,"\"version\": 1","\"version\": 1, \"version\": 1")); }); });
        test("out of range offset",[&] { throws([&] { Rules::parse(replaced(json,"[[-1,-1,0]]","[[8,-1,0]]")); }); });
        test("null move in rules",[&] { throws([&] { Rules::parse(replaced(json,"[[-1,-1,0]]","[[0,0,0]]")); }); });
        test("wrong offset dimension",[&] { throws([&] { Rules::parse(replaced(json,"[[-1,-1,0]]","[[-1,0]]")); }); });
        test("integer overflow in JSON",[&] { throws([&] { Rules::parse(replaced(json,"\"version\": 1","\"version\": 99999999999999999999")); }); });
        test("duplicate path identifier rejected",[&] { throws([&] { Rules::parse(replaced(json,"+X:1","+X:2")); }); });
        test("unknown path direction rejected",[&] { throws([&] { Rules::parse(replaced(json,"+X:1","+A:1")); }); });
        // Зміна траєкторії в копії JSON мусить змінювати дозволені ходи без зміни коду двигуна.
        test("JSON paths actually control the engine",[&] {
            auto modified=replaced(json,"[[-1,-1,0]]","[[-1,-1,1]]");
            MoveEngine custom(Rules::parse(modified));
            auto table=single(PieceType::Soldier);
            expect(!custom.movement(table,{4,4,4},{3,3,4}));
            expect(bool(custom.movement(table,{4,4,4},{3,3,5})));
        });
        // Будь-яка невдача дає ненульовий код, щоб автоматична перевірка не пропустила помилку.
        std::cout << passed << " passed, " << failed << " failed\n";
        return failed ? 1 : 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
