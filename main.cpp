/*
 * Copyright (C) 2026  zhutianhao <zhutianhao75@hotmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

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

static void dshareMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (type == QtWarningMsg && msg.startsWith(QStringLiteral("setHighDpiScaleFactorRoundingPolicy")))
        return;
    if (g_defaultMsgHandler)
        g_defaultMsgHandler(type, context, msg);
}

int main(int argc, char *argv[])
{
    g_defaultMsgHandler = qInstallMessageHandler(dshareMessageHandler);

    DApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dshare"));
    app.setApplicationDisplayName(QStringLiteral("DShare"));
    app.loadTranslator();

    Dtk::Core::DLogManager::registerConsoleAppender();
    Dtk::Core::DLogManager::registerFileAppender();

    MainWindow w;
    w.show();

    return app.exec();
}
