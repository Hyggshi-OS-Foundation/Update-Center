#include <QApplication>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Update Center");
    app.setOrganizationName("Hyggshi OS");

    MainWindow window;
    window.show();

    return app.exec();
}
