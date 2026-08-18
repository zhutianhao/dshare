#include <DApplication>
#include <DMainWindow>
#include <LogManager.h>

#include <QtGlobal>
#include <QMessageLogContext>

#include "mainwindow.h"

DWIDGET_USE_NAMESPACE

// Dtk's DApplication constructor calls QGuiApplication::setHighDpiScaleFactorRoundingPolicy()
// after the QGuiApplication base is already constructed, which makes Qt print a benign warning
// ("setHighDpiScaleFactorRoundingPolicy must be called before creating the QGuiApplication
// instance"). There is no app-level API to prevent it, so we drop exactly that one line and
// forward every other message unchanged.
static QtMessageHandler g_defaultMsgHandler = nullptr;

static void fileshareMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (type == QtWarningMsg && msg.startsWith(QStringLiteral("setHighDpiScaleFactorRoundingPolicy")))
        return;
    if (g_defaultMsgHandler)
        g_defaultMsgHandler(type, context, msg);
}

int main(int argc, char *argv[])
{
    g_defaultMsgHandler = qInstallMessageHandler(fileshareMessageHandler);

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
