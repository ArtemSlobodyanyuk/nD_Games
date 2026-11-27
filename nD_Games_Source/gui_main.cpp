#include "gui.h"
#include <QApplication>
#include <QDir>
#include <QMessageBox>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        // Абсолютний шлях забезпечує запуск ярликом з будь-якого робочого каталогу.
        const QString filename = argc > 1 ? QString::fromLocal8Bit(argv[1]) :
            QDir(QCoreApplication::applicationDirPath()).filePath("chess_rools.json");
        GameWindow window(nd::Rules::load(filename.toStdString()));
        window.show();
        return app.exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr, QStringLiteral("Не вдалося запустити гру"), QString::fromUtf8(error.what()));
        return 1;
    }
}
