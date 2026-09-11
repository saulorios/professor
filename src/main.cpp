#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QFont>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Fonte padrão da aplicação, com fallbacks até uma sans-serif genérica
    QFont font;
    font.setFamilies({"Segoe UI", "Ubuntu", "Noto Sans"});
    font.setStyleHint(QFont::SansSerif);
    app.setFont(font);

    // Carrega todo o estilo a partir do arquivo de recursos (.qrc)
    QFile styleFile(":/style.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    MainWindow window;
    window.show();

    return app.exec();
}
