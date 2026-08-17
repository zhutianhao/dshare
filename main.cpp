#include <DApplication>
#include <DMainWindow>
#include <LogManager.h>

#include "mainwindow.h"

DWIDGET_USE_NAMESPACE

int main(int argc, char *argv[])
{
    DApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("fileshare"));
    app.setApplicationDisplayName(QStringLiteral("文件共享"));
    app.loadTranslator();

    Dtk::Core::DLogManager::registerConsoleAppender();
    Dtk::Core::DLogManager::registerFileAppender();

    MainWindow w;
    w.show();

    return app.exec();
}
