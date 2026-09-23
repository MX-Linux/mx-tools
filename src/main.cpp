/**********************************************************************
 * Copyright (C) 2014-2026 MX Authors
 *
 * This file is part of MX Tools and is licensed under GPL-3.0-or-later.
 **********************************************************************/
#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTranslator>
#include <QUrl>
#include <QtGlobal>

#include "toolmodel.h"

#ifndef VERSION
    #define VERSION "?.?.?.?"
#endif

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qWarning("mx-tools: no display available (DISPLAY and WAYLAND_DISPLAY are both unset); "
                "a graphical session is required to run this program.");
        return EXIT_FAILURE;
    }

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY")
        && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }

    // QApplication initializes the desktop QStyle palette. On MX this lets
    // qt6gtk2 expose the active GTK theme to QML's SystemPalette.
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("MX-Linux"));
    QApplication::setApplicationName(QStringLiteral("mx-tools"));
    QApplication::setApplicationDisplayName(QStringLiteral("MX Tools"));
    QApplication::setApplicationVersion(QStringLiteral(VERSION));
    const auto windowIconName = QStringLiteral("mx-tools");
    if (QIcon::hasThemeIcon(windowIconName)) {
        QApplication::setWindowIcon(QIcon::fromTheme(windowIconName));
    } else {
        QApplication::setWindowIcon(QIcon(QStringLiteral(MX_TOOLS_LOGO_RESOURCE)));
    }

    // qt_ is a meta catalog that already includes qtbase_; the latter is only a fallback
    // for languages that ship no meta catalog.
    const QString qtTranslationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    QTranslator qtTranslator;
    if (qtTranslator.load(QStringLiteral("qt_") + QLocale::system().name(), qtTranslationsPath)
        || qtTranslator.load(QStringLiteral("qtbase_") + QLocale::system().name(), qtTranslationsPath)) {
        QApplication::installTranslator(&qtTranslator);
    }
    // A development build finds its catalogs next to the binary; an installed one has none
    // there and uses the packaged ones.
    const QString appCatalog = QApplication::applicationName() + QLatin1Char('_') + QLocale::system().name();
    QTranslator appTranslator;
    if (appTranslator.load(appCatalog, QApplication::applicationDirPath())
        || appTranslator.load(appCatalog, QStringLiteral("/usr/share/mx-tools/locale"))) {
        QApplication::installTranslator(&appTranslator);
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")
        && qgetenv("QT_STYLE_OVERRIDE").toLower() == "gtk2") {
        // QT_STYLE_OVERRIDE=gtk2 is a widget-only style name with no corresponding Qt Quick
        // Controls style module. Left alone, the platform theme's style hint propagates it to
        // Quick Controls, which then fails to load. Only override in that specific case, so
        // other environments still get native platform styling.
        QQuickStyle::setStyle(QStringLiteral("Fusion"));
    }

    auto *iconProvider = new ToolIconProvider;
    ToolModel toolModel(iconProvider);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("toolicons"), iconProvider);
    engine.setInitialProperties({{QStringLiteral("backend"), QVariant::fromValue(&toolModel)},
                                 {QStringLiteral("version"), QStringLiteral(VERSION)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule(QStringLiteral("MxTools"), QStringLiteral("Main"));
#else
    engine.load(QUrl(QStringLiteral("qrc:/MxTools/qml/Main.qml")));
#endif

    return QApplication::exec();
}
