#pragma once
#include "move.h"
#include <QWidget>
#include <QPixmap>
#include <functional>
#include <map>

class QLabel;
class QSpinBox;
class QPushButton;
class QStackedWidget;

// Відображає один XY-зріз; стан гри належить вікну, а не віджету дошки.
class BoardWidget : public QWidget {
    Q_OBJECT
public:
    explicit BoardWidget(QWidget* parent = nullptr);
    const nd::Table* table = nullptr;
    int layer = 1;
    std::optional<nd::Position> selected;
    std::vector<nd::Move> moves;
    std::function<void(nd::Position)> clicked;
    QRectF boardRect() const;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    std::map<std::pair<nd::PieceType, nd::Color>, QPixmap> sprites_;
};

class GameWindow : public QWidget {
    Q_OBJECT
public:
    explicit GameWindow(nd::Rules rules);
    int currentLayer() const;
    const nd::Table& position() const { return table_; }
protected:
    void changeEvent(QEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
private:
    nd::MoveEngine engine_;
    nd::Table table_ = nd::Table::initial();
    QStackedWidget* pages_;
    BoardWidget* board_;
    QSpinBox* layer_;
    QLabel* turn_;
    QLabel* message_;
    QPushButton* previous_;
    QPushButton* next_;
    QPushButton* fullscreen_ = nullptr;
    bool gameStarted_ = false;
    std::vector<nd::Move> legal_;
    void updateFullscreenButton();
    void startGame(bool restart = false);
    void setLayer(int value);
    void selectSquare(nd::Position position);
    void refreshGame();
};
