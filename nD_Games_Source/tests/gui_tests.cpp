#include "gui.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <iostream>
#include <stdexcept>

// Перевірка реальних взаємодій віджетів: ввід Z, клавіші, межі та ходи між шарами.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        auto require = [](bool value, const char* message) {
            if (!value) throw std::runtime_error(message);
        };
#ifdef Q_OS_WIN
        // Offscreen-плагін не шукає шрифти Windows автоматично, на відміну від qwindows.
        if (QGuiApplication::platformName() == "offscreen") {
            const auto fonts = QDir(qEnvironmentVariable("WINDIR", "C:/Windows")).filePath("Fonts/");
            require(QFontDatabase::addApplicationFont(fonts + "segoeui.ttf") >= 0, "Load system font for offscreen rendering");
            QFontDatabase::addApplicationFont(fonts + "segoeuib.ttf");
        }
#endif
        GameWindow window(nd::Rules::load(argv[1]));
        window.show();
        app.processEvents();
        const bool screenshots = argc > 2 && std::string(argv[2]) == "--screenshots";
        auto capture = [&](const char* filename) {
            if (!screenshots) return;
            app.processEvents();
            require(window.grab().save(QDir(QCoreApplication::applicationDirPath()).filePath(filename)), "Screenshot export");
        };
        capture("gui-menu.png");
        auto* logo = window.findChild<QLabel*>("crownLogo");
        require(logo && logo->isVisible() && !logo->pixmap().isNull(), "Menu crown logo is missing");
        auto* play = window.findChild<QPushButton*>("play");
        require(play && play->isVisible(), "Menu play button is missing");
        auto* restartButton = window.findChild<QPushButton*>("restartButton");
        require(restartButton && !restartButton->isVisible(), "Restart appears once a game has started");
        auto* rulesButton = window.findChild<QPushButton*>("rulesButton");
        require(rulesButton && rulesButton->isVisible() && rulesButton->y() > play->y(),
                "Rules button must be below Play");
        auto* exitButton = window.findChild<QPushButton*>("exitButton");
        require(exitButton && exitButton->isVisible() && exitButton->y() > rulesButton->y(),
                "Exit button must be below Rules");
        auto* backToMenu = window.findChild<QPushButton*>("backToMenu");
        require(backToMenu && !backToMenu->isVisible(), "Menu button must be hidden in the main menu");
        auto* fullscreen = window.findChild<QPushButton*>("fullscreen");
        require(fullscreen && fullscreen->isVisible(), "Menu fullscreen button is missing");
        const QPoint buttonOrigin = fullscreen->mapTo(&window, QPoint{});
        require(buttonOrigin.y() < 32 && window.width() - buttonOrigin.x() - fullscreen->width() < 32,
                "Fullscreen button must be in the upper right");
        const auto enterIcon = fullscreen->icon().pixmap(fullscreen->iconSize()).toImage();
        require(!enterIcon.isNull() && fullscreen->text().isEmpty() && !fullscreen->accessibleName().isEmpty(),
                "Fullscreen control must use an icon with an accessible name");
        QTest::mouseClick(fullscreen, Qt::LeftButton);
        const auto restoreIcon = fullscreen->icon().pixmap(fullscreen->iconSize()).toImage();
        require(window.isFullScreen() && fullscreen->isVisible() && !restoreIcon.isNull() && restoreIcon != enterIcon,
                "Enter fullscreen from the menu");
        QTest::mouseClick(fullscreen, Qt::LeftButton);
        require(!window.isFullScreen() && fullscreen->icon().pixmap(fullscreen->iconSize()).toImage() == enterIcon,
                "Exit fullscreen with the button");
        QTest::mouseClick(rulesButton, Qt::LeftButton);
        auto* tabs = window.findChild<QTabWidget*>("rulesTabs");
        require(tabs && tabs->isVisible() && tabs->count() == 5 && backToMenu->isVisible(),
                "The rules page must have separate game and piece tabs");
        require(tabs->tabText(0) == QStringLiteral("Правила гри"), "Game rules must have their own tab");
        const QSize normalSize = window.size();
        window.resize(window.minimumSize());
        app.processEvents();
        require(tabs->tabBar()->rect().contains(tabs->tabBar()->tabRect(tabs->count() - 1)),
                "All five tabs should fit at the minimum window size");
        window.resize(normalSize);
        capture("gui-rules-game.png");
        const QList<QStringList> ruleTopics = {
            {QStringLiteral("Захист дому"), QStringLiteral("Шах, мат і пат")},
            {QStringLiteral("Військовий"), QStringLiteral("перетворюється")},
            {QStringLiteral("Тура"), QStringLiteral("Кінь"), QStringLiteral("Слон")},
            {QStringLiteral("Літойд"), QStringLiteral("Вода"), QStringLiteral("Інферно"), QStringLiteral("Повітря")},
            {QStringLiteral("Король"), QStringLiteral("Королева")}
        };
        for (int i = 0; i < tabs->count(); ++i) {
            QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier, tabs->tabBar()->tabRect(i).center());
            auto* text = qobject_cast<QTextBrowser*>(tabs->currentWidget());
            require(tabs->currentIndex() == i && text && text->isVisible(), "Rules tab must open by click");
            for (const auto& topic : ruleTopics[i])
                require(text->toPlainText().contains(topic), "Rules tab is missing an expected topic");
            if (i == 3) capture("gui-rules-pieces.png");
        }
        QTest::mouseClick(backToMenu, Qt::LeftButton);
        require(play->isVisible() && !tabs->isVisible(), "Return from rules to the main menu");
        QTest::mouseClick(play, Qt::LeftButton);
        const QPoint buttonCenter = fullscreen->mapTo(&window, fullscreen->rect().center());
        require(window.childAt(buttonCenter) == fullscreen, "Game page must not cover the fullscreen button");
        const QPoint menuCenter = backToMenu->mapTo(&window, backToMenu->rect().center());
        require(window.childAt(menuCenter) == backToMenu && menuCenter.y() == buttonCenter.y() &&
                menuCenter.x() < buttonCenter.x(), "Game menu button must sit next to the fullscreen button");
        capture("gui-board-z1.png");
        auto* layer = window.findChild<QSpinBox*>("layer");
        auto* previous = window.findChild<QPushButton*>("previousLayer");
        auto* next = window.findChild<QPushButton*>("nextLayer");
        if (screenshots) {
            layer->setValue(4);
            capture("gui-board-z4.png");
            layer->setValue(1);
        }
        require(layer->isVisible() && window.currentLayer() == 1, "Initial layer");
        require(!previous->isEnabled(), "Lower boundary arrow");
        QTest::mouseClick(next, Qt::LeftButton);
        require(window.currentLayer() == 2, "Next arrow");
        QTest::mouseClick(previous, Qt::LeftButton);
        require(window.currentLayer() == 1, "Previous arrow");
        auto* input = layer->findChild<QLineEdit*>();
        QTest::mouseClick(input, Qt::LeftButton);
        app.processEvents();
        QTest::keyClicks(input, "8");
        QTest::keyClick(input, Qt::Key_Return);
        require(window.currentLayer() == 8 && !next->isEnabled(), "Typed layer and upper boundary");
        QTest::keyClick(input, Qt::Key_Plus);
        require(window.currentLayer() == 8, "Plus must not exceed 8");
        QTest::keyClick(input, Qt::Key_Minus);
        require(window.currentLayer() == 7, "Minus with editor focus");
        QTest::keyClick(input, Qt::Key_Plus);
        require(window.currentLayer() == 8, "Plus with editor focus");
        layer->setValue(1);
        QTest::keyClick(input, Qt::Key_Minus);
        require(window.currentLayer() == 1, "Minus must not go below 1");
        auto* board = window.findChild<BoardWidget*>("board");
        auto clickSquare = [&](nd::Position pos) {
            layer->setValue(pos.z);
            const auto rect = board->boardRect();
            const auto size = rect.width()/8;
            const QPoint center(qRound(rect.left()+(pos.x-.5)*size), qRound(rect.top()+(8.5-pos.y)*size));
            QTest::mouseClick(board, Qt::LeftButton, Qt::NoModifier, center);
        };
        auto chooseRoute = [&](nd::Position target, const QStringList& expectedRows, const QString& firstStep = {}) {
            bool showedOptions = false;
            bool saved = true;
            QTimer inspect;
            inspect.setSingleShot(true);
            QObject::connect(&inspect, &QTimer::timeout, &window, [&] {
                auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                if (!dialog) return;
                const auto rows = dialog->comboBoxItems();
                auto* list = dialog->findChild<QListView*>();
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                showedOptions = list && list->isVisible() && buttons && rows.size() == expectedRows.size() &&
                                list->horizontalScrollBar()->maximum() == 0 &&
                                list->verticalScrollBar()->maximum() == 0;
                for (const auto& row : expectedRows)
                    showedOptions = showedOptions && rows.contains(QStringLiteral("Перший крок ") + row);
                if (showedOptions && screenshots && !firstStep.isEmpty())
                    saved = dialog->grab().save(QDir(QCoreApplication::applicationDirPath()).filePath("gui-route-picker.png"));
                if (!showedOptions) {
                    dialog->reject();
                    return;
                }
                if (firstStep.isEmpty()) {
                    QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel), Qt::LeftButton);
                    return;
                }
                for (qsizetype i = 0; i < rows.size(); ++i) {
                    if (!rows[i].startsWith(QStringLiteral("Перший крок ") + firstStep)) continue;
                    list->setCurrentIndex(list->model()->index(static_cast<int>(i), 0));
                    QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
                    return;
                }
                showedOptions = false;
                dialog->reject();
            });
            inspect.start(0);
            clickSquare(target);
            inspect.stop();
            require(showedOptions && saved, "Route dialog must show distinct first steps and capture details");
        };
        clickSquare({1,1,1});
        chooseRoute({1,7,1}, {QStringLiteral("221  ·  +Y:1  ·  Взяття: 171"),
                              QStringLiteral("122  ·  +Y:3  ·  Взяття: 171"),
                              QStringLiteral("222  ·  +Y:5  ·  Взяття: 171")});
        require(board->selected == nd::Position{1,1,1} &&
                window.position().at({1,1,1})->type == nd::PieceType::Air &&
                window.position().at({1,7,1})->type == nd::PieceType::Soldier &&
                window.position().turn() == nd::Color::White, "Cancel leaves the move and captures unapplied");
        clickSquare({2,2,1});
        require(board->selected == nd::Position{2,2,1}, "Select soldier");
        clickSquare({2,3,1});
        require(window.position().at({2,3,1}).has_value() && !window.position().at({2,2,1}), "White move applied");
        require(window.position().turn() == nd::Color::Black, "Black turn");
        require(window.findChild<QLabel*>("turn")->text().contains(QStringLiteral("чорних")), "Turn indicator");
        clickSquare({1,7,1});
        clickSquare({1,6,1});
        require(window.position().turn() == nd::Color::White, "Black move applied");
        clickSquare({1,1,4});
        require(board->selected == nd::Position{1,1,4}, "Select rook");
        layer->setValue(5);
        window.showMaximized();
        require(fullscreen->isVisible(), "In-game fullscreen button is missing");
        QTest::mouseClick(fullscreen, Qt::LeftButton);
        require(window.isFullScreen(), "Enter fullscreen during the game");
        QTest::mouseClick(backToMenu, Qt::LeftButton);
        require(play->isVisible() && play->text() == QStringLiteral("Продовжити") && window.isFullScreen(),
                "Return to the menu without leaving fullscreen");
        QTest::mouseClick(rulesButton, Qt::LeftButton);
        require(tabs->isVisible(), "Open rules while a game is paused");
        QTest::mouseClick(backToMenu, Qt::LeftButton);
        QTest::mouseClick(play, Qt::LeftButton);
        require(board->isVisible() && window.isFullScreen() && window.currentLayer() == 5 &&
                board->selected == nd::Position{1,1,4} && window.position().at({2,3,1}) &&
                window.position().at({1,6,1}) && window.position().turn() == nd::Color::White,
                "Resume preserves the position, turn, layer and selection");
        QTest::keyClick(fullscreen, Qt::Key_Escape);
        require(!window.isFullScreen() && window.isMaximized(), "Escape restores the maximized window");
        require(window.currentLayer() == 5, "Fullscreen preserves the current layer");
        require(board->selected == nd::Position{1,1,4}, "Selection survives layer change");
        clickSquare({1,1,5});
        require(!window.position().at({1,1,4}) && window.position().at({1,1,5}), "Move between layers");
        clickSquare({1,1,5});
        require(window.position().turn() == nd::Color::Black, "Wrong side cannot move");
        clickSquare({1,8,1});
        chooseRoute({1,4,1}, {QStringLiteral("271  ·  -Y:1  ·  Без взяття"),
                              QStringLiteral("172  ·  -Y:3  ·  Без взяття"),
                              QStringLiteral("272  ·  -Y:5  ·  Без взяття")});
        chooseRoute({1,2,1}, {QStringLiteral("271  ·  -Y:1  ·  Взяття: 231, 121"),
                              QStringLiteral("172  ·  -Y:3  ·  Взяття: 121"),
                              QStringLiteral("272  ·  -Y:5  ·  Взяття: 121")}, QStringLiteral("271"));
        require(!window.position().at({1,8,1}) && !window.position().at({2,3,1}) &&
                window.position().at({1,2,1})->type == nd::PieceType::Air &&
                window.position().turn() == nd::Color::White, "Confirm applies the selected route and both captures");
        clickSquare({1,1,5});
        require(board->selected == nd::Position{1,1,5}, "Select a piece before restarting");
        layer->setValue(6);
        QTest::mouseClick(backToMenu, Qt::LeftButton);
        require(restartButton->isVisible() && restartButton->y() > play->y() &&
                restartButton->y() < rulesButton->y(), "Restart must be below Continue");
        QTest::mouseClick(restartButton, Qt::LeftButton);
        require(board->isVisible() && window.currentLayer() == 1 && !board->selected && board->moves.empty() &&
                window.position().turn() == nd::Color::White, "Restart resets the layer, selection and turn");
        const auto initial = nd::Table::initial();
        for (int x = 1; x <= nd::board_size; ++x)
            for (int y = 1; y <= nd::board_size; ++y)
                for (int z = 1; z <= nd::board_size; ++z) {
                    const auto& actual = window.position().at({x,y,z});
                    const auto& expected = initial.at({x,y,z});
                    require(actual.has_value() == expected.has_value() &&
                            (!actual || (actual->type == expected->type && actual->color == expected->color)),
                            "Restart must restore the starting pieces");
                }
        QTest::mouseClick(backToMenu, Qt::LeftButton);
        QTest::mouseClick(exitButton, Qt::LeftButton);
        require(!window.isVisible(), "Exit must close the game window");
        std::cout << "GUI interaction checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
