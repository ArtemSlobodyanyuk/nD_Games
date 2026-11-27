#include "gui.h"
#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>

namespace {
enum Page { MainMenuPage, GamePage, RulesPage };

QIcon fullscreenIcon(bool restore, qreal pixelRatio) {
    const QSize size(16, 16);
    QPixmap sprite(size * pixelRatio);
    sprite.setDevicePixelRatio(pixelRatio);
    sprite.fill(Qt::transparent);
    QPainter painter(&sprite);
    painter.setPen(QColor("#f4f4f4"));
    if (restore) {
        const QPointF rear[] = {{4.5, 5.5}, {4.5, 2.5}, {13.5, 2.5}, {13.5, 11.5}, {10.5, 11.5}};
        painter.drawPolyline(rear, 5);
        painter.drawRect(QRectF(1.5, 5.5, 9, 9));
    } else {
        painter.drawRect(QRectF(2.5, 2.5, 11, 11));
    }
    painter.end();
    return QIcon(sprite);
}

QString pieceName(nd::PieceType type) {
    switch (type) {
    case nd::PieceType::Soldier: return QStringLiteral("Військовий");
    case nd::PieceType::Rook: return QStringLiteral("Тура");
    case nd::PieceType::Bishop: return QStringLiteral("Слон");
    case nd::PieceType::Knight: return QStringLiteral("Кінь");
    case nd::PieceType::Stone: return QStringLiteral("Камінь");
    case nd::PieceType::Water: return QStringLiteral("Вода");
    case nd::PieceType::Fire: return QStringLiteral("Вогонь");
    case nd::PieceType::Air: return QStringLiteral("Повітря");
    case nd::PieceType::King: return QStringLiteral("Король");
    case nd::PieceType::Queen: return QStringLiteral("Королева");
    }
    return {};
}

QWidget* makeRulesPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 24);
    auto* heading = new QLabel(QStringLiteral("Правила"));
    heading->setObjectName("rulesTitle");
    layout->addWidget(heading);
    auto* tabs = new QTabWidget;
    tabs->setObjectName("rulesTabs");
    layout->addWidget(tabs, 1);

    auto addTab = [tabs](const QString& title, const char* content) {
        auto* text = new QTextBrowser;
        text->setAccessibleName(title);
        text->setHtml(QString::fromUtf8(content));
        tabs->addTab(text, title);
    };
    addTab(QStringLiteral("Правила гри"), R"(
        <h3>Дошка й початок партії</h3>
        <p>Дошка має 8 × 8 × 8 клітинок. Координати записують у порядку XYZ. У грі видно один шар Z,
        який можна змінити стрілками або клавішами − і +. У кожної сторони по 34 фігури.
        Білі починають на рядах Y=1–2, чорні – на Y=7–8.</p>
        <h3>Хід і взяття</h3>
        <p>Першими ходять білі. Далі гравці ходять по черзі, переміщуючи по одній фігурі.
        Пропускати хід і ставати на клітинку зі своєю фігурою заборонено. За звичайного взяття
        фігура стає на клітинку суперника, а взята фігура знімається з дошки.</p>
        <p>Воду й повітря можна провести різними маршрутами, беручи ворогів по дорозі.
        Якщо до цілі веде кілька маршрутів, оберіть один за першим кроком і списком взяттів.</p>
        <h3>Захист дому</h3>
        <p>Дім білих – ряд Y=1, дім чорних – Y=8 на всіх шарах. Білі з Y=1–4 і чорні з Y=5–8
        можуть зайти на порожню клітинку ворожого дому за правилами руху. Брати там фігури й
        загрожувати королю можна після переходу на половину суперника.</p>
        <h3>Шах, мат і пат</h3>
        <p>Короля не можна взяти. Жоден хід не може залишати власного короля під шахом. Якщо король під шахом
        і сторона не має легального ходу, це мат. Якщо легальних ходів немає і король поза шахом,
        це пат, партія завершується нічиєю.</p>
    )");
    addTab(QStringLiteral("1-й рівень"), R"(
        <h3>Військовий</h3>
        <p>Ходить на одну клітинку вперед або назад, прямо чи по діагоналі в межах шару XY.
        Бере лише по діагоналі. Досягнувши домашнього ряду суперника, обов’язково перетворюється
        на коня, слона або туру.</p>
    )");
    addTab(QStringLiteral("2-й рівень"), R"(
        <h3>Тура</h3>
        <p>Ходить на будь-яку відстань уздовж однієї осі X, Y або Z. Інші фігури перекривають шлях.</p>
        <h3>Кінь</h3>
        <p>Стрибає на дві клітинки вздовж однієї осі та на одну вздовж іншої. Третя координата
        залишається сталою. Фігури між початком і кінцем стрибка йому не заважають.</p>
        <h3>Слон</h3>
        <p>Ходить по діагоналі на будь-яку відстань у площині XY, XZ або YZ. За хід змінює
        дві координати на однакову відстань. Інші фігури перекривають шлях.</p>
    )");
    addTab(QStringLiteral("3-й рівень"), R"(
        <h3>Літойд (камінь)</h3>
        <p>Ходить на одну сусідню порожню клітинку вздовж осі. Бере стрибком на ворожу клітинку,
        якщо відстань до неї по прямій округлюється до трьох клітинок.</p>
        <h3>Вода</h3>
        <p>Ходить дугою з трьох кроків і зупиняється в її кінці. Бере всіх ворогів на відвіданих
        клітинках. Свої фігури перекривають маршрут.</p>
        <h3>Інферно (вогонь)</h3>
        <p>Ходить на одну, дві або три клітинки по прямій чи діагоналі, зокрема просторовій.
        Інші фігури перекривають шлях.</p>
        <h3>Повітря</h3>
        <p>Ходить зиґзаґом і може зупинитися на будь-якому кроці маршруту. Перестрибує свої фігури
        на проміжних клітинках і бере ворогів на всіх відвіданих клітинках.</p>
    )");
    addTab(QStringLiteral("4-й рівень"), R"(
        <h3>Король</h3>
        <p>Ходить на будь-яку з 26 сусідніх клітинок, якщо вона доступна і перебування на ній
        не ставить короля під шах.</p>
        <h3>Королева</h3>
        <p>Ходить на будь-яку відстань по прямій чи діагоналі, зокрема просторовій.
        Інші фігури перекривають шлях.</p>
    )");
    return page;
}
}

BoardWidget::BoardWidget(QWidget* parent) : QWidget(parent) {
    setObjectName("board");
    setMinimumSize(320, 320);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("Шахова дошка вибраного шару"));
    const QPixmap sheet(":/assets/elemental.png");
    if (sheet.isNull()) throw std::runtime_error("Elemental sprites are missing");
    // Межі спрайтів у наданому атласі. Зображення зберігається без перемальовування.
    const QRect regions[] = {
        {130, 5, 310, 435}, {525, 5, 320, 435},
        {890, 25, 370, 415}, {1320, 25, 390, 415},
        {130, 450, 310, 420}, {525, 450, 320, 420},
        {890, 450, 370, 420}, {1320, 450, 390, 420}
    };
    for (auto color : {nd::Color::White, nd::Color::Black}) {
        for (auto type : {nd::PieceType::Soldier, nd::PieceType::Rook,
                nd::PieceType::Bishop, nd::PieceType::Knight, nd::PieceType::Stone,
                nd::PieceType::Water, nd::PieceType::Fire, nd::PieceType::Air,
                nd::PieceType::King, nd::PieceType::Queen}) {
            int column = -1;
            if (type == nd::PieceType::Stone) column = 0;
            if (type == nd::PieceType::Fire) column = 1;
            if (type == nd::PieceType::Water) column = 2;
            if (type == nd::PieceType::Air) column = 3;
            QPixmap sprite;
            if (column >= 0) {
                sprite = sheet.copy(regions[column + (color == nd::Color::Black ? 4 : 0)]);
            } else {
                const QString path = QString(":/assets/pieces/%1%2.svg")
                    .arg(color == nd::Color::White ? 'w' : 'b').arg(QChar(nd::symbol(type)));
                QSvgRenderer renderer(path);
                if (!renderer.isValid()) throw std::runtime_error("Chess sprites are missing");
                sprite = QPixmap(256, 256);
                sprite.fill(Qt::transparent);
                QPainter painter(&sprite);
                renderer.render(&painter);
            }
            sprites_[{type, color}] = sprite;
        }
    }
}

QRectF BoardWidget::boardRect() const {
    const qreal size = std::min(width(), height()) - 48;
    return {(width()-size)/2, (height()-size)/2, size, size};
}

void BoardWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto rect = boardRect();
    const qreal cell = rect.width()/8;
    painter.setFont(QFont("Segoe UI", 10));
    for (int y = 1; y <= 8; ++y) for (int x = 1; x <= 8; ++x) {
        const nd::Position pos{x, y, layer};
        const QRectF tile(rect.left()+(x-1)*cell, rect.top()+(8-y)*cell, cell, cell);
        painter.fillRect(tile, nd::Table::light(pos) ? QColor("#f4f4f4") : QColor("#a5a19a"));
        if (selected && *selected == pos) painter.fillRect(tile, QColor("#ffca00"));
        const bool target = std::any_of(moves.begin(), moves.end(), [pos](const auto& m){return m.to == pos;});
        if (target) {
            painter.setPen(QPen(QColor("#614800"), 2));
            painter.setBrush(QColor("#ffca00"));
            painter.drawEllipse(tile.center(), cell*.13, cell*.13);
        }
        if (table && table->at(pos)) {
            const auto piece = *table->at(pos);
            const auto& sprite = sprites_.at({piece.type, piece.color});
            QSizeF size = sprite.size();
            size.scale(QSizeF(cell*.88, cell*.88), Qt::KeepAspectRatio);
            const QRectF destination(tile.center()-QPointF(size.width()/2, size.height()/2), size);
            painter.drawPixmap(destination, sprite, QRectF(sprite.rect()));
            if (target) {
                painter.setPen(QPen(QColor("#614800"), 3));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(tile.adjusted(3,3,-3,-3));
            }
        }
    }
    painter.setPen(palette().color(QPalette::WindowText));
    for (int i = 1; i <= 8; ++i) {
        painter.drawText(QRectF(rect.left()+(i-1)*cell, rect.bottom()+3, cell, 22), Qt::AlignCenter, QString::number(i));
        painter.drawText(QRectF(rect.left()-24, rect.top()+(8-i)*cell, 20, cell), Qt::AlignCenter, QString::number(i));
    }
}

void BoardWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !boardRect().contains(event->position())) return;
    const auto rect = boardRect();
    const qreal cell = rect.width()/8;
    const int x = int((event->position().x()-rect.left())/cell)+1;
    const int y = 8-int((event->position().y()-rect.top())/cell);
    if (nd::Position{x,y,layer}.valid() && clicked) clicked({x,y,layer});
}

GameWindow::GameWindow(nd::Rules rules) : engine_(std::move(rules)) {
    setWindowTitle(QStringLiteral("nD_Games"));
    resize(920, 820);
    setMinimumSize(540, 600);
    setStyleSheet(QStringLiteral(
        "QWidget { background: #202020; color: #f4f4f4; font-family: 'Segoe UI'; font-size: 15px; }"
        "QPushButton, QSpinBox { background: #fafbf9; color: #202020; border: 1px solid #cdcdca; border-radius: 8px; }"
        "QPushButton { padding: 10px 20px; }"
        "QPushButton#fullscreen, QPushButton#backToMenu { background: transparent; color: #f4f4f4; "
        "border: 1px solid transparent; border-radius: 4px; padding: 0; }"
        "QPushButton#fullscreen:hover, QPushButton#backToMenu:hover { background: #383838; }"
        "QPushButton#fullscreen:pressed, QPushButton#backToMenu:pressed { background: #484848; }"
        "QPushButton#fullscreen:focus, QPushButton#backToMenu:focus { border-color: #828282; }"
        "QPushButton:hover { background: #e5e3df; } QPushButton:pressed { background: #d8d5cf; }"
        "QPushButton:disabled { color: #767676; }"
        "QPushButton[menuSecondary=\"true\"] { background: #282828; color: #f4f4f4; border-color: #626262; }"
        "QPushButton[menuSecondary=\"true\"]:hover { background: #383838; }"
        "QPushButton[menuSecondary=\"true\"]:pressed { background: #484848; }"
        "QSpinBox { padding: 8px; font-size: 18px; selection-background-color: #ffca00; selection-color: #202020; }"
        "QLabel#title { font-size: 44px; font-weight: 600; }"
        "QLabel#rulesTitle { font-size: 28px; font-weight: 600; }"
        "QTabWidget::pane { border: 1px solid #454545; }"
        "QTabBar::tab { background: #202020; color: #d4d4d4; padding: 9px 10px; border-bottom: 2px solid transparent; }"
        "QTabBar::tab:hover { background: #303030; }"
        "QTabBar::tab:selected { background: #282828; color: #f4f4f4; border-bottom-color: #ffca00; }"
        "QTextBrowser { background: #282828; border: none; padding: 12px; }"
        "QLabel#turn { font-size: 20px; font-weight: 600; }"));
    auto* root = new QGridLayout(this);
    root->setContentsMargins(0,0,0,0);
    pages_ = new QStackedWidget;
    root->addWidget(pages_, 0, 0);
    auto* menu = new QWidget;
    auto* menuLayout = new QVBoxLayout(menu);
    menuLayout->addStretch();
    auto* logo = new QLabel;
    logo->setObjectName("crownLogo");
    logo->setAccessibleName(QStringLiteral("Золота корона"));
    const qreal pixelRatio = devicePixelRatioF();
    QPixmap crown(QStringLiteral(":/assets/crown-logo.png"));
    crown = crown.scaled(QSize(300, 200) * pixelRatio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    crown.setDevicePixelRatio(pixelRatio);
    logo->setPixmap(crown);
    menuLayout->addWidget(logo, 0, Qt::AlignHCenter);
    menuLayout->addSpacing(12);
    auto* title = new QLabel(windowTitle());
    title->setObjectName("title");
    title->setAlignment(Qt::AlignCenter);
    menuLayout->addWidget(title);
    menuLayout->addSpacing(28);
    auto* play = new QPushButton(QStringLiteral("Грати"));
    play->setObjectName("play");
    play->setFixedSize(200,52);
    menuLayout->addWidget(play, 0, Qt::AlignHCenter);
    auto* restartButton = new QPushButton(QStringLiteral("Заново"));
    restartButton->setObjectName("restartButton");
    restartButton->setProperty("menuSecondary", true);
    restartButton->setFixedSize(play->size());
    menuLayout->addWidget(restartButton, 0, Qt::AlignHCenter);
    restartButton->hide();
    auto* rulesButton = new QPushButton(QStringLiteral("Правила"));
    rulesButton->setObjectName("rulesButton");
    rulesButton->setProperty("menuSecondary", true);
    rulesButton->setFixedSize(play->size());
    menuLayout->addSpacing(4);
    menuLayout->addWidget(rulesButton, 0, Qt::AlignHCenter);
    auto* exitButton = new QPushButton(QStringLiteral("Вихід"));
    exitButton->setObjectName("exitButton");
    exitButton->setProperty("menuSecondary", true);
    exitButton->setFixedSize(play->size());
    menuLayout->addWidget(exitButton, 0, Qt::AlignHCenter);
    menuLayout->addStretch();
    pages_->addWidget(menu);
    auto* game = new QWidget;
    auto* layout = new QVBoxLayout(game);
    layout->setContentsMargins(24,24,24,24);
    turn_ = new QLabel;
    turn_->setObjectName("turn");
    turn_->setAlignment(Qt::AlignCenter);
    layout->addWidget(turn_);
    board_ = new BoardWidget;
    board_->table = &table_;
    layout->addWidget(board_, 1);
    auto* controls = new QHBoxLayout;
    controls->addStretch();
    controls->addWidget(new QLabel(QStringLiteral("Шар Z")));
    previous_ = new QPushButton(QStringLiteral("←"));
    previous_->setObjectName("previousLayer");
    previous_->setAccessibleName(QStringLiteral("Попередній шар"));
    controls->addWidget(previous_);
    layer_ = new QSpinBox;
    layer_->setObjectName("layer");
    layer_->setAccessibleName(QStringLiteral("Координата Z від 1 до 8"));
    layer_->setRange(1,8);
    layer_->setAlignment(Qt::AlignCenter);
    layer_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    layer_->setKeyboardTracking(false);
    layer_->setFixedWidth(76);
    controls->addWidget(layer_);
    next_ = new QPushButton(QStringLiteral("→"));
    next_->setObjectName("nextLayer");
    next_->setAccessibleName(QStringLiteral("Наступний шар"));
    controls->addWidget(next_);
    controls->addStretch();
    layout->addLayout(controls);
    auto* hint = new QLabel(QStringLiteral("− / + — зміна шару • натисни фігуру, потім клітинку"));
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint);
    message_ = new QLabel;
    message_->setObjectName("message");
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    message_->setMinimumHeight(44);
    layout->addWidget(message_);
    pages_->addWidget(game);
    pages_->addWidget(makeRulesPage());

    auto* windowControls = new QHBoxLayout;
    windowControls->setContentsMargins(16,16,16,0);
    auto* backToMenu = new QPushButton(QStringLiteral("До меню"));
    backToMenu->setObjectName("backToMenu");
    backToMenu->setFixedSize(88, 32);
    backToMenu->hide();
    windowControls->addWidget(backToMenu);
    fullscreen_ = new QPushButton;
    fullscreen_->setObjectName("fullscreen");
    fullscreen_->setFixedSize(44, 32);
    fullscreen_->setIconSize(QSize(16, 16));
    fullscreen_->setFocusPolicy(Qt::TabFocus);
    updateFullscreenButton();
    windowControls->addWidget(fullscreen_);
    root->addLayout(windowControls, 0, 0, Qt::AlignTop | Qt::AlignRight);
    connect(pages_, &QStackedWidget::currentChanged, this, [this, backToMenu](int page) {
        backToMenu->setVisible(page != MainMenuPage);
        backToMenu->raise();
        fullscreen_->raise();
    });
    connect(fullscreen_, &QPushButton::clicked, this, [this] {
        setWindowState(windowState() ^ Qt::WindowFullScreen);
    });

    connect(backToMenu, &QPushButton::clicked, this, [this] { pages_->setCurrentIndex(MainMenuPage); });
    connect(rulesButton, &QPushButton::clicked, this, [this] { pages_->setCurrentIndex(RulesPage); });
    connect(exitButton, &QPushButton::clicked, this, &QWidget::close);
    connect(play, &QPushButton::clicked, this, [this, play, restartButton] {
        startGame();
        play->setText(QStringLiteral("Продовжити"));
        restartButton->show();
    });
    connect(restartButton, &QPushButton::clicked, this, [this] { startGame(true); });
    connect(previous_, &QPushButton::clicked, this, [this]{setLayer(currentLayer()-1);});
    connect(next_, &QPushButton::clicked, this, [this]{setLayer(currentLayer()+1);});
    connect(layer_, &QSpinBox::valueChanged, this, [this](int value){setLayer(value);});
    board_->clicked = [this](nd::Position pos){selectSquare(pos);};
    qApp->installEventFilter(this);
}

int GameWindow::currentLayer() const { return layer_->value(); }

void GameWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && fullscreen_)
        updateFullscreenButton();
}

void GameWindow::updateFullscreenButton() {
    const bool full = isFullScreen();
    const auto label = full ? QStringLiteral("Вийти з повного екрана") : QStringLiteral("На весь екран");
    fullscreen_->setIcon(fullscreenIcon(full, devicePixelRatioF()));
    fullscreen_->setAccessibleName(label);
    fullscreen_->setToolTip(full ? label + QStringLiteral(" (Esc)") : label);
}

// Фільтр перехоплює +/− також коли фокус у полі Z, включно з цифровою клавіатурою.
bool GameWindow::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (isFullScreen() && widget && widget->window() == this &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        if (event->type() == QEvent::KeyPress)
            setWindowState(windowState().setFlag(Qt::WindowFullScreen, false));
        event->accept();
        return true;
    }
    if (pages_->currentIndex() == GamePage && event->type() == QEvent::MouseButtonRelease &&
        watched == layer_->findChild<QLineEdit*>()) {
        // Одне натискання дозволяє одразу замінити цифру шару.
        QTimer::singleShot(0, layer_, [this]{layer_->selectAll();});
    }
    if (pages_->currentIndex() == GamePage && widget && (widget == this || isAncestorOf(widget)) &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (!(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
            (key->key() == Qt::Key_Plus || key->key() == Qt::Key_Minus || key->key() == Qt::Key_Equal)) {
            if (event->type() == QEvent::KeyPress)
                setLayer(currentLayer() + (key->key() == Qt::Key_Minus ? -1 : 1));
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched,event);
}

void GameWindow::setLayer(int value) {
    value = std::clamp(value, 1, 8);
    layer_->setValue(value);
    board_->layer = value;
    previous_->setEnabled(value > 1);
    next_->setEnabled(value < 8);
    board_->update();
}

void GameWindow::startGame(bool restart) {
    pages_->setCurrentIndex(GamePage);
    if (restart || !gameStarted_) {
        if (restart) table_ = nd::Table::initial();
        refreshGame();
        setLayer(1);
        gameStarted_ = true;
    }
}

void GameWindow::refreshGame() {
    legal_ = engine_.legal_moves(table_);
    board_->selected.reset();
    board_->moves.clear();
    const auto status = engine_.status(table_);
    const QString side = table_.turn() == nd::Color::White ? QStringLiteral("білих") : QStringLiteral("чорних");
    turn_->setText(QStringLiteral("Хід %1").arg(side));
    message_->clear();
    if (status == nd::GameStatus::Check) message_->setText(QStringLiteral("Шах! Захисти короля."));
    if (status == nd::GameStatus::Checkmate) {
        turn_->setText(QStringLiteral("Перемогли %1").arg(table_.turn() == nd::Color::White ? QStringLiteral("чорні") : QStringLiteral("білі")));
        message_->setText(QStringLiteral("Мат. Гру завершено."));
    }
    if (status == nd::GameStatus::Stalemate) message_->setText(QStringLiteral("Пат. Нічия."));
    board_->update();
}

void GameWindow::selectSquare(nd::Position pos) {
    if (board_->selected && *board_->selected == pos) {
        board_->selected.reset();
        board_->moves.clear();
        message_->clear();
        board_->update();
        return;
    }
    // Список уже перевірених ходів дозволяє обрати перетворення й неоднозначний маршрут.
    std::vector<nd::Move> choices;
    if (board_->selected) for (const auto& move : board_->moves)
        if (move.to == pos) choices.push_back(move);
    if (!choices.empty()) {
        int index = 0;
        if (choices.size() > 1) {
            QStringList labels;
            for (const auto& move : choices) {
                QStringList parts;
                if (move.path) {
                    if (const auto details = engine_.preview(table_, move)) {
                        parts << QStringLiteral("Перший крок %1").arg(QString::fromStdString(details->route.front().str()));
                        parts << QString::fromStdString(move.path->id());
                        QStringList victims;
                        for (const auto cell : details->captures)
                            victims << QString::fromStdString(cell.str());
                        parts << (victims.isEmpty() ? QStringLiteral("Без взяття")
                                                   : QStringLiteral("Взяття: %1").arg(victims.join(QStringLiteral(", "))));
                    } else {
                        parts << QString::fromStdString(move.path->id());
                    }
                }
                if (move.promotion) parts << pieceName(*move.promotion);
                labels << parts.join(QStringLiteral("  ·  "));
            }
            const auto& first = choices.front();
            QInputDialog dialog(this);
            dialog.setWindowTitle(first.path ? QStringLiteral("Вибір маршруту") : QStringLiteral("Перетворення військового"));
            dialog.setLabelText(QStringLiteral("Хід %1 → %2").arg(QString::fromStdString(first.from.str()),
                                                                QString::fromStdString(first.to.str())));
            dialog.setOption(QInputDialog::UseListViewForComboBoxItems);
            dialog.setComboBoxItems(labels);
            dialog.setTextValue(labels.first());
            if (auto* list = dialog.findChild<QListView*>()) {
                list->ensurePolished();
                const int border = 2 * list->frameWidth();
                list->setMinimumWidth(list->sizeHintForColumn(0) + border + 16);
                list->setFixedHeight(list->sizeHintForRow(0) * static_cast<int>(labels.size()) + border + 8);
            }
            dialog.setOkButtonText(QStringLiteral("Обрати"));
            dialog.setCancelButtonText(QStringLiteral("Скасувати"));
            if (dialog.exec() != QDialog::Accepted) return;
            index = labels.indexOf(dialog.textValue());
            if (index < 0) return;
        }
        const auto result = engine_.play(table_, choices.at(index));
        if (result) refreshGame();
        else message_->setText(QString::fromStdString(result.message));
        return;
    }
    if (table_.at(pos) && table_.at(pos)->color == table_.turn() && !legal_.empty()) {
        board_->selected = pos;
        board_->moves.clear();
        for (const auto& move : legal_) if (move.from == pos) board_->moves.push_back(move);
        message_->setText(QStringLiteral("%1 • XYZ %2. Обери клітинку; для іншого шару зміни Z.")
            .arg(pieceName(table_.at(pos)->type), QString::fromStdString(pos.str())));
    } else {
        message_->setText(QStringLiteral("Обери свою фігуру або підсвічену клітинку."));
    }
    board_->update();
}
