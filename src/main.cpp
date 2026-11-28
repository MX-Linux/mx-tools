/**********************************************************************
 * Copyright (C) 2014-2026 MX Authors
 *
 * This file is part of MX Tools and is licensed under GPL-3.0-or-later.
 **********************************************************************/
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTranslator>
#include <QUrl>
#include <QtGlobal>

#include <memory>

#include "toolmodel.h"

#ifndef VERSION
    #define VERSION "?.?.?.?"
#endif

int main(int argc, char *argv[])
{
    const QByteArray option = argc == 2 ? QByteArray(argv[1]) : QByteArray();
    const bool informational = option == "-h" || option == "--help" || option == "--help-all"
                               || option == "-v" || option == "--version";
    std::unique_ptr<QCoreApplication> app;
    if (informational) {
        app = std::make_unique<QCoreApplication>(argc, argv);
    } else {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
            if (qEnvironmentVariableIsEmpty("DISPLAY")) {
                qWarning("mx-tools: no display available (DISPLAY and WAYLAND_DISPLAY are both unset); "
                         "a graphical session is required to run this program.");
                return EXIT_FAILURE;
            }
            qputenv("QT_QPA_PLATFORM", "xcb");
        }
        // QApplication initializes the desktop QStyle palette. On MX this lets
        // qt6gtk2 expose the active GTK theme to QML's SystemPalette.
        app = std::make_unique<QApplication>(argc, argv);
    }

    QApplication::setOrganizationName(QStringLiteral("MX-Linux"));
    QApplication::setApplicationName(QStringLiteral("mx-tools"));
    QApplication::setApplicationVersion(QStringLiteral(VERSION));

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
        || appTranslator.load(appCatalog, QStringLiteral(MX_TOOLS_LOCALE_DIR))) {
        QApplication::installTranslator(&appTranslator);
    }

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(*app);

    // The dashboard uses only 2D controls. On X11 it can draw without a working
    // GLX or EGL configuration. Keep the renderer choice local to this launcher.
    if (QApplication::platformName() == QLatin1String("xcb")
        && qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND") && qEnvironmentVariableIsEmpty("QMLSCENE_DEVICE")
        && qEnvironmentVariableIsEmpty("QSG_RHI_BACKEND")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    QApplication::setApplicationDisplayName(QStringLiteral("MX Tools"));
    const auto windowIconName = QStringLiteral("mx-tools");
    if (QIcon::hasThemeIcon(windowIconName)) {
        QApplication::setWindowIcon(QIcon::fromTheme(windowIconName));
    } else {
        QApplication::setWindowIcon(QIcon(QStringLiteral(MX_TOOLS_LOGO_RESOURCE)));
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
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, app.get(),
                     [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    engine.loadFromModule(QStringLiteral("MxTools"), QStringLiteral("Main"));
#else
    engine.load(QUrl(QStringLiteral("qrc:/MxTools/qml/Main.qml")));
#endif

    return QApplication::exec();
}
