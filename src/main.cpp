#include <QApplication>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Update Center");
    app.setOrganizationName("Hyggshi OS");
    app.setWindowIcon(QIcon(":/resources/updatecenter.svg"));

    MainWindow window;
    window.setWindowIcon(QIcon(":/resources/updatecenter.svg"));
    window.show();

    return app.exec();
}
