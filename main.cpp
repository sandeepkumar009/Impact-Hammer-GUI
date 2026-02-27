#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QString>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // --- LOAD STYLE SHEET ---
    // The path starts with ":/" because it is a resource
    QFile file(":/theme.qss");
    if(file.open(QFile::ReadOnly | QFile::Text)) {
        QString styleSheet = QLatin1String(file.readAll());
        a.setStyleSheet(styleSheet);
        file.close();
    } else {
        qDebug() << "Warning: Could not load theme.qss";
    }
    // ------------------------

    MainWindow w;
    w.show();
    return a.exec();
}
