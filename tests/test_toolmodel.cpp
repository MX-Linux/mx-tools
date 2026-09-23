#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include "toolmodel.h"

#include <unistd.h>

namespace
{
QString desktopFileContent(const QString &name, const QString &categories, const QString &extra = {})
{
    return QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nComment=%1 comment\n"
                           "Icon=applications-utilities\nExec=/bin/sleep 0.2\nTerminal=false\n"
                           "Categories=%2\n%3")
        .arg(name, categories, extra);
}

void writeDesktopFile(const QDir &directory, const QString &fileName, const QString &content)
{
    QFile file(directory.filePath(fileName));
    QVERIFY2(file.open(QFile::WriteOnly | QFile::Text), qPrintable(file.fileName()));
    QTextStream stream(&file);
    stream << content;
}

QString fakeXfconfPath(const QString &name)
{
    return QDir(QString::fromLocal8Bit(qgetenv("FAKE_XFCONF_DIR"))).filePath(name);
}

void writeFakeXfconf(const QString &name, const QString &content)
{
    QFile file(fakeXfconfPath(name));
    QVERIFY2(file.open(QFile::WriteOnly | QFile::Text), qPrintable(file.fileName()));
    file.write(content.toUtf8());
}

void setFavorites(const QStringList &favorites)
{
    QFile::remove(fakeXfconfPath(QStringLiteral("plugin-1-favorites.scalar")));
    writeFakeXfconf(QStringLiteral("plugin-1-favorites"), favorites.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}

void setScalarFavorite(const QString &favorite)
{
    QFile::remove(fakeXfconfPath(QStringLiteral("plugin-1-favorites")));
    writeFakeXfconf(QStringLiteral("plugin-1-favorites.scalar"), favorite + QLatin1Char('\n'));
}

QStringList favorites()
{
    QFile file(fakeXfconfPath(QStringLiteral("plugin-1-favorites")));
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void setFakeXfconfFailure(const QString &kind, bool fail)
{
    if (fail) {
        writeFakeXfconf(QStringLiteral("fail-") + kind, {});
    } else {
        QFile::remove(fakeXfconfPath(QStringLiteral("fail-") + kind));
    }
}

// Menu visibility changes run on a worker thread; starts one and waits for it to finish.
[[nodiscard]] bool setHideFromMenu(ToolModel &model, bool hide)
{
    model.setHideFromMenu(hide);
    return QTest::qWaitFor([&model] { return !model.menuBusy(); }, 10000);
}

bool menuStateActive()
{
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                             .filePath(QStringLiteral("menu-visibility.ini"));
    return QSettings(path, QSettings::IniFormat).value(QStringLiteral("active")).toBool();
}
}

class TestToolModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void discoversAndFiltersTools();
    void launchBlocksRelaunchOnlyWhileRunning();
    void readsOnlyTheDesktopEntryGroup();
    void listsMultiCategoryToolOnce();
    void launchExpandsExecFieldCodes();
    void restoreReinsertsDroppedWhiskerFavorite();
    void failedPluginQueryKeepsFavoritesSnapshot();
    void legacyScalarFavoritesAreRestoredAsArray();
    void unrelatedFavoritesSnapshotNeedsNoQuery();
    void failedFavoritesWriteKeepsSnapshot();
    void missingXfconfQueryKeepsSnapshot();
    void hideWritesOverridesToXdgDataHome();
    void subdirectoryLaunchersUseDesktopIds();
    void launchersInstalledWhileHiddenAreHidden();
    void failedNewLauncherOverrideIsRetried();
    void unflaggedWrittenOverrideIsNotRecaptured();
    void changelogIsReadInTheBackground();
    void pluginTypesComeFromOneListing();
    void menuChangesRunInTheBackground();
    void searchMatchesEveryWord();
    void sortsNamesForTheLocale();
    void filteringMovesRowsWithoutReset();

private:
    void writeMenuTool();

    QTemporaryDir m_fakeBin;
    QByteArray m_path;
    QScopedPointer<QTemporaryDir> m_home;
};

void TestToolModel::initTestCase()
{
    // Put the fake first in PATH so no test reaches a real xfce4-panel.
    // tests/fake-xfconf-query documents how it stores the panel properties.
    QVERIFY(m_fakeBin.isValid());
    const QString source = QFINDTESTDATA("fake-xfconf-query");
    QVERIFY(!source.isEmpty());
    const QString script = m_fakeBin.filePath(QStringLiteral("xfconf-query"));
    QVERIFY(QFile::copy(source, script));
    QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    m_path = (m_fakeBin.path() + QLatin1Char(':')).toUtf8() + qgetenv("PATH");
    qputenv("PATH", m_path);
}

void TestToolModel::init()
{
    m_home.reset(new QTemporaryDir());
    QVERIFY(m_home->isValid());
    // ToolModel reads and writes real user paths (menu visibility state,
    // legacy user-overridden .desktop copies) derived from HOME, so every
    // test gets a private HOME/XDG_CONFIG_HOME (and the default XDG_DATA_HOME)
    // instead of touching the developer's actual configuration.
    qputenv("HOME", m_home->path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (m_home->path() + QStringLiteral("/.config")).toUtf8());
    qunsetenv("XDG_DATA_HOME");
    qunsetenv("XDG_CURRENT_DESKTOP");
    qunsetenv("XDG_SESSION_DESKTOP");
    // Pin the live/installed state instead of inheriting it from the root
    // filesystem, which is an overlay mount inside build containers.
    qputenv("MX_TOOLS_TEST_FORCE_LIVE", "0");

    QVERIFY(QDir().mkpath(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)));

    // An xfce4-panel with one Whisker Menu, plugin-1, and no favorites yet.
    const QString xfconfDir = m_home->filePath(QStringLiteral("xfconf"));
    QVERIFY(QDir().mkpath(xfconfDir));
    qputenv("FAKE_XFCONF_DIR", xfconfDir.toUtf8());
    writeFakeXfconf(QStringLiteral("plugins"), QStringLiteral("1\n"));
    writeFakeXfconf(QStringLiteral("plugin-1"), QStringLiteral("whiskermenu\n"));
}

void TestToolModel::cleanup()
{
    QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)).removeRecursively();
    qunsetenv("MX_TOOLS_TEST_FORCE_LIVE");
    qunsetenv("FAKE_XFCONF_DIR");
    qputenv("PATH", m_path);
    m_home.reset();
}

void TestToolModel::writeMenuTool()
{
    writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("tool.desktop"),
                     desktopFileContent(QStringLiteral("Tool"), QStringLiteral("X-MX-Utilities")));
}

void TestToolModel::discoversAndFiltersTools()
{
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));

    writeDesktopFile(applications, QStringLiteral("utility.desktop"),
                      desktopFileContent(QStringLiteral("Utility Tool"), QStringLiteral("X-MX-Utilities")));
    writeDesktopFile(applications, QStringLiteral("setup-hidden.desktop"),
                      desktopFileContent(QStringLiteral("Hidden Setup Tool"), QStringLiteral("MX-Setup"),
                                         QStringLiteral("NotShowIn=XFCE;\n")));
    writeDesktopFile(applications, QStringLiteral("software-wrong-desktop.desktop"),
                      desktopFileContent(QStringLiteral("KDE-only Software"), QStringLiteral("X-MX-Software"),
                                         QStringLiteral("OnlyShowIn=KDE;\n")));
    writeDesktopFile(applications, QStringLiteral("mx-remastercc.desktop"),
                      desktopFileContent(QStringLiteral("Live Only Tool"), QStringLiteral("MX-Live")));
    // Mentioning a marker in the text no longer hides a tool.
    writeDesktopFile(applications, QStringLiteral("installed.desktop"),
                      desktopFileContent(QStringLiteral("Installed Tool"), QStringLiteral("MX-Maintenance"),
                                         QStringLiteral("Keywords=MX-OnlyLive;\n")));

    qputenv("XDG_CURRENT_DESKTOP", "XFCE");

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);

    // MX_TOOLS_TEST_FORCE_LIVE=0 means the live-only launcher is excluded;
    // NotShowIn=XFCE and OnlyShowIn=KDE both exclude their entries under
    // XDG_CURRENT_DESKTOP=XFCE, so only "Utility Tool" and "Installed Tool"
    // should survive.
    QCOMPARE(model.totalCount(), 2);
    QCOMPARE(model.categories(), QStringList({QStringLiteral("All tools"), QStringLiteral("Maintenance"),
                                               QStringLiteral("Utilities")}));
    QCOMPARE(model.rowCount(), 2);

    model.setSelectedCategory(QStringLiteral("Utilities"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ToolModel::NameRole).toString(), QStringLiteral("Utility Tool"));
    QCOMPARE(model.data(model.index(0), ToolModel::CommentRole).toString(), QStringLiteral("Utility Tool comment"));
    QVERIFY(model.data(model.index(0), ToolModel::IconSourceRole).toString().startsWith(
        QStringLiteral("image://toolicons/")));
    QCOMPARE(QFileInfo(model.data(model.index(0), ToolModel::FileNameRole).toString()).fileName(),
             QStringLiteral("utility.desktop"));

    model.setSelectedCategory({});
    model.setSearch(QStringLiteral("installed"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ToolModel::CategoryRole).toString(), QStringLiteral("Maintenance"));

    // A live session adds the live-only launcher.
    qputenv("MX_TOOLS_TEST_FORCE_LIVE", "1");
    ToolModel liveModel(&iconProvider);
    QCOMPARE(liveModel.totalCount(), 3);
    QCOMPARE(liveModel.categories(), QStringList({QStringLiteral("All tools"), QStringLiteral("Live"),
                                                   QStringLiteral("Maintenance"), QStringLiteral("Utilities")}));
}

void TestToolModel::launchBlocksRelaunchOnlyWhileRunning()
{
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));
    writeDesktopFile(applications, QStringLiteral("sleeper.desktop"),
                      desktopFileContent(QStringLiteral("Sleeper"), QStringLiteral("X-MX-Utilities")));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QCOMPARE(model.totalCount(), 1);
    const QString fileName = model.data(model.index(0), ToolModel::FileNameRole).toString();

    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    model.launch(fileName);
    model.launch(fileName);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.constFirst().at(0).toString(), QStringLiteral("Tool already running"));

    // Once the detached /bin/sleep 0.2 has exited, launching again must be
    // allowed. Note this does not exercise the zombie branch of
    // isProcessRunning(): QProcess::startDetached double-forks, so the child
    // is reparented to init and reaped as soon as it exits.
    QTest::qWait(600);
    model.launch(fileName);
    QCOMPARE(errors.count(), 1);
}

void TestToolModel::readsOnlyTheDesktopEntryGroup()
{
    // Keys from other groups must not fill in missing ones, spacing around '='
    // is allowed, and values are unescaped.
    writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("parsed.desktop"),
                     QStringLiteral("# Comment=Not a comment\n"
                                    "[Desktop Entry]\n"
                                    "Type=Application\n"
                                    "Name = Parsed\\sTool\n"
                                    "Exec=/bin/true\n"
                                    "Categories = System;X-MX-Utilities;\n"
                                    "Keywords=semi\\;colon;other;\n"
                                    "[Desktop Action extra]\n"
                                    "Name=Extra\n"
                                    "Comment=Action comment\n"));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QCOMPARE(model.totalCount(), 1);
    QCOMPARE(model.data(model.index(0), ToolModel::NameRole).toString(), QStringLiteral("Parsed Tool"));
    QCOMPARE(model.data(model.index(0), ToolModel::CommentRole).toString(), QString());

    model.setSearch(QStringLiteral("semi;colon other"));
    QCOMPARE(model.rowCount(), 1);
    model.setSearch(QStringLiteral("Action comment"));
    QCOMPARE(model.rowCount(), 0);
}

void TestToolModel::listsMultiCategoryToolOnce()
{
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));
    writeDesktopFile(applications, QStringLiteral("both.desktop"),
                     desktopFileContent(QStringLiteral("Both"), QStringLiteral("X-MX-Utilities;MX-Setup;")));
    writeDesktopFile(applications, QStringLiteral("setup.desktop"),
                     desktopFileContent(QStringLiteral("Setup Only"), QStringLiteral("X-MX-Setup;")));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QCOMPARE(model.totalCount(), 2);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.categories(), QStringList({QStringLiteral("All tools"), QStringLiteral("Setup"),
                                               QStringLiteral("Utilities")}));

    // Listed under each of its categories, labelled with the first one in MX order.
    model.setSelectedCategory(QStringLiteral("Utilities"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ToolModel::NameRole).toString(), QStringLiteral("Both"));
    QCOMPARE(model.data(model.index(0), ToolModel::CategoryRole).toString(), QStringLiteral("Setup"));
    model.setSelectedCategory(QStringLiteral("Setup"));
    QCOMPARE(model.rowCount(), 2);

    model.setSelectedCategory({});
    model.setSearch(QStringLiteral("Both"));
    QCOMPARE(model.rowCount(), 1);
}

void TestToolModel::launchExpandsExecFieldCodes()
{
    // A launcher that records its arguments, one per line, renaming the file
    // into place so the test never reads a partial write.
    const QString recorder = m_home->filePath(QStringLiteral("record-arguments"));
    const QString argumentsFile = m_home->filePath(QStringLiteral("arguments"));
    {
        QFile script(recorder);
        QVERIFY(script.open(QFile::WriteOnly | QFile::Text));
        script.write("#!/bin/sh\nfor argument in \"$@\"; do printf '%s\\n' \"$argument\"; done > \"$ARGUMENTS_FILE.tmp\"\n"
                     "mv \"$ARGUMENTS_FILE.tmp\" \"$ARGUMENTS_FILE\"\n");
    }
    QVERIFY(QFile::setPermissions(recorder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    qputenv("ARGUMENTS_FILE", argumentsFile.toUtf8());

    // File codes are dropped, %% %c %i %k expand, and quotes group arguments
    // with backslash escapes; \\ in the file is the string-level escape.
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));
    const QString desktopFile = applications.filePath(QStringLiteral("recorder.desktop"));
    writeDesktopFile(applications, QStringLiteral("recorder.desktop"),
                     QStringLiteral("[Desktop Entry]\nType=Application\nName=MX Recorder\nIcon=recorder-icon\n"
                                    "Categories=X-MX-Utilities;\nExec=\"%1\" %F \"quoted arg\" 100%% --name=%c %i %k "
                                    "\"say \\\\\"hi\\\\\"\" \"\" \"100%% done\" %d%m\n")
                         .arg(recorder));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    model.launch(desktopFile);
    QCOMPARE(errors.count(), 0);
    QTRY_VERIFY(QFileInfo::exists(argumentsFile));
    QFile recorded(argumentsFile);
    QVERIFY(recorded.open(QFile::ReadOnly | QFile::Text));
    QCOMPARE(QString::fromUtf8(recorded.readAll()).split(QLatin1Char('\n')),
             QStringList({QStringLiteral("quoted arg"), QStringLiteral("100%"), QStringLiteral("--name=MX Recorder"),
                          QStringLiteral("--icon"), QStringLiteral("recorder-icon"), desktopFile,
                          QStringLiteral("say \"hi\""), QString(), QStringLiteral("100% done"), QString()}));

    // The spec forbids running a command with an unknown field code.
    QFile::remove(argumentsFile);
    writeDesktopFile(applications, QStringLiteral("invalid.desktop"),
                     QStringLiteral("[Desktop Entry]\nType=Application\nName=Invalid\n"
                                    "Categories=X-MX-Utilities;\nExec=\"%1\" --mode=%Z\n")
                         .arg(recorder));
    ToolModel invalidModel(&iconProvider);
    QSignalSpy invalidErrors(&invalidModel, &ToolModel::errorOccurred);
    invalidModel.launch(applications.filePath(QStringLiteral("invalid.desktop")));
    QCOMPARE(invalidErrors.count(), 1);
    QTest::qWait(300);
    QVERIFY(!QFileInfo::exists(argumentsFile));
    qunsetenv("ARGUMENTS_FILE");
}

void TestToolModel::restoreReinsertsDroppedWhiskerFavorite()
{
    writeMenuTool();
    setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")});

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());

    // Whisker Menu drops favorites whose launchers become hidden.
    setFavorites({QStringLiteral("other.desktop")});
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));
    QVERIFY(!menuStateActive());
}

void TestToolModel::failedPluginQueryKeepsFavoritesSnapshot()
{
    writeMenuTool();
    setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")});

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    setFavorites({QStringLiteral("other.desktop")});

    // A failing plugin-type query must not be mistaken for a removed plugin.
    setFakeXfconfFailure(QStringLiteral("type"), true);
    QSignalSpy visibilityChanges(&model, &ToolModel::hideFromMenuChanged);
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(model.hideFromMenu());
    // The switch already flipped itself; it needs the signal to flip back.
    QVERIFY(!visibilityChanges.isEmpty());
    QCOMPARE(errors.count(), 1);
    QVERIFY(menuStateActive());
    QCOMPARE(favorites(), QStringList({QStringLiteral("other.desktop")}));

    // The kept snapshot lets the next attempt finish the restore.
    setFakeXfconfFailure(QStringLiteral("type"), false);
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));
    QVERIFY(!menuStateActive());
}

void TestToolModel::legacyScalarFavoritesAreRestoredAsArray()
{
    writeMenuTool();
    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);

    // Earlier releases stored a single restored favorite as a plain string.
    setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")});
    QVERIFY(setHideFromMenu(model, true));
    setScalarFavorite(QStringLiteral("other.desktop"));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));

    // A plain-string snapshot is restored, and a single favorite stays an array.
    setScalarFavorite(QStringLiteral("tool.desktop"));
    QVERIFY(setHideFromMenu(model, true));
    QFile::remove(fakeXfconfPath(QStringLiteral("plugin-1-favorites.scalar")));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QVERIFY(!QFile::exists(fakeXfconfPath(QStringLiteral("plugin-1-favorites.scalar"))));
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop")}));
}

void TestToolModel::unrelatedFavoritesSnapshotNeedsNoQuery()
{
    writeMenuTool();
    setFavorites({QStringLiteral("other.desktop")});

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());

    // None of our launchers were favorites, so xfconf failures can't block the restore.
    setFakeXfconfFailure(QStringLiteral("type"), true);
    setFakeXfconfFailure(QStringLiteral("favorites"), true);
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(errors.count(), 0);
    QVERIFY(!menuStateActive());

    // The same holds when xfconf-query is gone altogether.
    setFakeXfconfFailure(QStringLiteral("type"), false);
    setFakeXfconfFailure(QStringLiteral("favorites"), false);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    qputenv("PATH", m_home->path().toUtf8());
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(errors.count(), 0);
}

void TestToolModel::failedFavoritesWriteKeepsSnapshot()
{
    writeMenuTool();
    setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")});

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    setFavorites({QStringLiteral("other.desktop")});

    // A failed write must leave the current favorites and the snapshot intact.
    setFakeXfconfFailure(QStringLiteral("write"), true);
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(model.hideFromMenu());
    QCOMPARE(errors.count(), 1);
    QVERIFY(menuStateActive());
    QCOMPARE(favorites(), QStringList({QStringLiteral("other.desktop")}));

    setFakeXfconfFailure(QStringLiteral("write"), false);
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));
    QVERIFY(!menuStateActive());
}

void TestToolModel::missingXfconfQueryKeepsSnapshot()
{
    writeMenuTool();
    setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")});

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    setFavorites({QStringLiteral("other.desktop")});

    // The state file outlives the process, so a restore without xfconf-query
    // (for example after a restart with a different PATH) must keep it.
    qputenv("PATH", m_home->path().toUtf8());
    ToolModel restarted(&iconProvider);
    QVERIFY(restarted.hideFromMenu());
    QVERIFY(setHideFromMenu(restarted, false));
    QVERIFY(restarted.hideFromMenu());
    QVERIFY(menuStateActive());

    qputenv("PATH", m_path);
    QVERIFY(setHideFromMenu(restarted, false));
    QVERIFY(!restarted.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));
}

void TestToolModel::hideWritesOverridesToXdgDataHome()
{
    writeMenuTool();
    const QString dataHome = m_home->filePath(QStringLiteral("data"));
    qputenv("XDG_DATA_HOME", dataHome.toUtf8());
    const QString override = dataHome + QStringLiteral("/applications/tool.desktop");

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    QFile file(override);
    QVERIFY(file.open(QFile::ReadOnly | QFile::Text));
    QVERIFY(file.readAll().contains("NoDisplay=true"));
    file.close();
    QVERIFY(!QFileInfo::exists(m_home->filePath(QStringLiteral(".local/share/applications/tool.desktop"))));

    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QVERIFY(!QFileInfo::exists(override));
    qunsetenv("XDG_DATA_HOME");
}

void TestToolModel::subdirectoryLaunchersUseDesktopIds()
{
    // applications/mx/tool.desktop has the desktop ID mx-tool.desktop, which is
    // what menus look up overrides by and what Whisker Menu stores as a favorite.
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));
    QVERIFY(applications.mkpath(QStringLiteral("mx")));
    writeDesktopFile(QDir(applications.filePath(QStringLiteral("mx"))), QStringLiteral("tool.desktop"),
                     desktopFileContent(QStringLiteral("Nested"), QStringLiteral("X-MX-Utilities")));
    writeMenuTool();
    setFavorites({QStringLiteral("mx-tool.desktop"), QStringLiteral("tool.desktop")});

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QCOMPARE(model.totalCount(), 2);
    QVERIFY(setHideFromMenu(model, true));
    const QDir overrides(m_home->filePath(QStringLiteral(".local/share/applications")));
    QVERIFY(QFileInfo::exists(overrides.filePath(QStringLiteral("mx-tool.desktop"))));
    QVERIFY(QFileInfo::exists(overrides.filePath(QStringLiteral("tool.desktop"))));

    setFavorites({});
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QVERIFY(!QFileInfo::exists(overrides.filePath(QStringLiteral("mx-tool.desktop"))));
    QVERIFY(!QFileInfo::exists(overrides.filePath(QStringLiteral("tool.desktop"))));
    QCOMPARE(favorites(), QStringList({QStringLiteral("mx-tool.desktop"), QStringLiteral("tool.desktop")}));
}

void TestToolModel::launchersInstalledWhileHiddenAreHidden()
{
    writeMenuTool();
    ToolIconProvider iconProvider;
    {
        ToolModel model(&iconProvider);
        QVERIFY(setHideFromMenu(model, true));
        QVERIFY(model.hideFromMenu());
    }

    // A package installs another MX tool while the tools are hidden.
    writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("new.desktop"),
                     desktopFileContent(QStringLiteral("New"), QStringLiteral("X-MX-Setup")));
    const QDir overrides(m_home->filePath(QStringLiteral(".local/share/applications")));
    QVERIFY(!QFileInfo::exists(overrides.filePath(QStringLiteral("new.desktop"))));

    ToolModel restarted(&iconProvider);
    QVERIFY(restarted.hideFromMenu());
    QFile file(overrides.filePath(QStringLiteral("new.desktop")));
    QVERIFY(file.open(QFile::ReadOnly | QFile::Text));
    QVERIFY(file.readAll().contains("NoDisplay=true"));
    file.close();

    QVERIFY(setHideFromMenu(restarted, false));
    QVERIFY(!restarted.hideFromMenu());
    QVERIFY(!QFileInfo::exists(overrides.filePath(QStringLiteral("new.desktop"))));
    QVERIFY(!QFileInfo::exists(overrides.filePath(QStringLiteral("tool.desktop"))));
}

void TestToolModel::failedNewLauncherOverrideIsRetried()
{
    if (geteuid() == 0) {
        QSKIP("A read-only directory can't make writes fail for root.");
    }
    writeMenuTool();
    ToolIconProvider iconProvider;
    {
        ToolModel model(&iconProvider);
        QVERIFY(setHideFromMenu(model, true));
        QVERIFY(model.hideFromMenu());
    }
    writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("new.desktop"),
                     desktopFileContent(QStringLiteral("New"), QStringLiteral("X-MX-Setup")));
    const QString overrides = m_home->filePath(QStringLiteral(".local/share/applications"));
    const QString override = QDir(overrides).filePath(QStringLiteral("new.desktop"));

    // The first start records the new launcher but can't write its override.
    const QFileDevice::Permissions permissions = QFile::permissions(overrides);
    QVERIFY(QFile::setPermissions(overrides, QFile::ReadOwner | QFile::ExeOwner));
    {
        ToolModel restarted(&iconProvider);
        QVERIFY(restarted.hideFromMenu());
    }
    QVERIFY(QFile::setPermissions(overrides, permissions));
    QVERIFY(!QFileInfo::exists(override));

    // The next start retries it.
    ToolModel retried(&iconProvider);
    QFile file(override);
    QVERIFY(file.open(QFile::ReadOnly | QFile::Text));
    QVERIFY(file.readAll().contains("NoDisplay=true"));
    file.close();

    QVERIFY(setHideFromMenu(retried, false));
    QVERIFY(!retried.hideFromMenu());
    QVERIFY(!QFileInfo::exists(override));
}

void TestToolModel::unflaggedWrittenOverrideIsNotRecaptured()
{
    writeMenuTool();
    ToolIconProvider iconProvider;
    {
        ToolModel model(&iconProvider);
        QVERIFY(setHideFromMenu(model, true));
        QVERIFY(model.hideFromMenu());
    }
    writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("new.desktop"),
                     desktopFileContent(QStringLiteral("New"), QStringLiteral("X-MX-Setup")));
    {
        ToolModel restarted(&iconProvider);
    }
    const QString override = m_home->filePath(QStringLiteral(".local/share/applications/new.desktop"));
    QVERIFY(QFileInfo::exists(override));

    // Simulate exiting after the override was written but before it was flagged.
    const QString statePath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                                  .filePath(QStringLiteral("menu-visibility.ini"));
    {
        QSettings state(statePath, QSettings::IniFormat);
        state.setValue(QStringLiteral("Entries/new.desktop/written"), false);
    }

    ToolModel retried(&iconProvider);
    QVERIFY(QSettings(statePath, QSettings::IniFormat).value(QStringLiteral("Entries/new.desktop/written")).toBool());
    QVERIFY(setHideFromMenu(retried, false));
    QVERIFY(!retried.hideFromMenu());
    QVERIFY(!QFileInfo::exists(override));
}

void TestToolModel::changelogIsReadInTheBackground()
{
    const QString changelog = QStringLiteral(MX_TOOLS_CHANGELOG_PATH);
    QFile::remove(changelog);
    QProcess gzip;
    gzip.setStandardOutputFile(changelog);
    gzip.start(QStringLiteral("gzip"), {QStringLiteral("-c")});
    QVERIFY(gzip.waitForStarted());
    gzip.write("mx-tools (26.09) unstable; urgency=medium\n");
    gzip.closeWriteChannel();
    QVERIFY(gzip.waitForFinished());

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QSignalSpy documents(&model, &ToolModel::documentReady);
    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    model.openChangelog();
    model.openChangelog();
    // Nothing arrives until the event loop runs, and the second click is ignored.
    QCOMPARE(documents.count(), 0);
    QTRY_COMPARE(documents.count(), 1);
    QCOMPARE(documents.constFirst().at(1).toString(), QStringLiteral("mx-tools (26.09) unstable; urgency=medium\n"));
    QTest::qWait(100);
    QCOMPARE(documents.count(), 1);
    QCOMPARE(errors.count(), 0);

    // A second request works once the first is done.
    model.openChangelog();
    QTRY_COMPARE(documents.count(), 2);
    QFile::remove(changelog);
}

void TestToolModel::pluginTypesComeFromOneListing()
{
    writeMenuTool();
    writeFakeXfconf(QStringLiteral("plugins"), QStringLiteral("1\n2\n3\n"));
    writeFakeXfconf(QStringLiteral("plugin-2"), QStringLiteral("whiskermenu\n"));
    writeFakeXfconf(QStringLiteral("plugin-3"), QStringLiteral("launcher\n"));
    setFavorites({QStringLiteral("tool.desktop")});
    writeFakeXfconf(QStringLiteral("plugin-2-favorites"), QStringLiteral("tool.desktop\n"));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    QFile calls(fakeXfconfPath(QStringLiteral("calls")));
    QVERIFY(calls.open(QFile::ReadOnly | QFile::Text));
    const QStringList hideCalls = QString::fromUtf8(calls.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    calls.close();
    // One listing for all plugin types, then one favorites read per Whisker Menu.
    QCOMPARE(hideCalls.filter(QStringLiteral(" -l")).size(), 1);
    QCOMPARE(hideCalls.size(), 3);

    // Plugin 2 was replaced by another plugin type; plugin 1 is restored as usual.
    writeFakeXfconf(QStringLiteral("plugin-2"), QStringLiteral("launcher\n"));
    setFavorites({});
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop")}));

    // An empty listing looks like an unreachable xfconfd, so the snapshot is kept.
    QVERIFY(setHideFromMenu(model, true));
    setFavorites({});
    writeFakeXfconf(QStringLiteral("plugins"), {});
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(model.hideFromMenu());
    QVERIFY(menuStateActive());
}

void TestToolModel::menuChangesRunInTheBackground()
{
    writeMenuTool();
    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QSignalSpy busyChanges(&model, &ToolModel::menuBusyChanged);
    QSignalSpy visibilityChanges(&model, &ToolModel::hideFromMenuChanged);

    model.setHideFromMenu(true);
    QVERIFY(model.menuBusy());
    QCOMPARE(busyChanges.count(), 1);
    QVERIFY(!model.hideFromMenu());
    // A change requested meanwhile is ignored, but still re-syncs the switches.
    model.setHideFromMenu(false);
    QCOMPARE(visibilityChanges.count(), 1);

    QTRY_VERIFY(!model.menuBusy());
    QCOMPARE(busyChanges.count(), 2);
    QVERIFY(model.hideFromMenu());
    QCOMPARE(visibilityChanges.count(), 2);
    QVERIFY(QFileInfo::exists(m_home->filePath(QStringLiteral(".local/share/applications/tool.desktop"))));
}

void TestToolModel::searchMatchesEveryWord()
{
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));
    writeDesktopFile(applications, QStringLiteral("backup.desktop"),
                     desktopFileContent(QStringLiteral("Backup"), QStringLiteral("X-MX-Utilities")));
    writeDesktopFile(applications, QStringLiteral("cleanup.desktop"),
                     desktopFileContent(QStringLiteral("Cleanup"), QStringLiteral("X-MX-Maintenance")));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    // Surrounding and repeated whitespace is ignored, and words match in any order.
    model.setSearch(QStringLiteral(" backup  "));
    QCOMPARE(model.rowCount(), 1);
    model.setSearch(QStringLiteral("comment\tbackup"));
    QCOMPARE(model.rowCount(), 1);
    model.setSearch(QStringLiteral("utilities backup"));
    QCOMPARE(model.rowCount(), 1);
    model.setSearch(QStringLiteral("backup maintenance"));
    QCOMPARE(model.rowCount(), 0);
    // Blank searches show everything, and the category filter applies again.
    model.setSelectedCategory(QStringLiteral("Maintenance"));
    model.setSearch(QStringLiteral("   "));
    QCOMPARE(model.rowCount(), 1);
}

void TestToolModel::sortsNamesForTheLocale()
{
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));
    const QStringList names {QStringLiteral("Zeta"), QStringLiteral("\u00c9dition"), QStringLiteral("beta"),
                             QStringLiteral("Tool 10"), QStringLiteral("Tool 9"), QStringLiteral("Alpha")};
    for (qsizetype index = 0; index < names.size(); ++index) {
        writeDesktopFile(applications, QStringLiteral("tool%1.desktop").arg(index),
                         desktopFileContent(names.at(index), QStringLiteral("X-MX-Utilities")));
    }

    const QLocale previous;
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QLocale::setDefault(previous);
    QStringList sorted;
    for (int row = 0; row < model.rowCount(); ++row) {
        sorted.append(model.data(model.index(row), ToolModel::NameRole).toString());
    }
    QCOMPARE(sorted, QStringList({QStringLiteral("Alpha"), QStringLiteral("beta"), QStringLiteral("\u00c9dition"),
                                  QStringLiteral("Tool 9"), QStringLiteral("Tool 10"), QStringLiteral("Zeta")}));
}

void TestToolModel::filteringMovesRowsWithoutReset()
{
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));
    const QList<QPair<QString, QString>> tools {
        {QStringLiteral("Alpha"), QStringLiteral("X-MX-Utilities")}, {QStringLiteral("Beta"), QStringLiteral("X-MX-Setup")},
        {QStringLiteral("Gamma"), QStringLiteral("X-MX-Utilities")}, {QStringLiteral("Delta"), QStringLiteral("X-MX-Maintenance")},
        {QStringLiteral("Epsilon"), QStringLiteral("X-MX-Setup")}};
    for (const auto &[name, category] : tools) {
        writeDesktopFile(applications, name.toLower() + QStringLiteral(".desktop"), desktopFileContent(name, category));
    }

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    // Fails the test on any inconsistent insert/remove signal.
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy removals(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy insertions(&model, &QAbstractItemModel::rowsInserted);
    const auto names = [&model] {
        QStringList result;
        for (int row = 0; row < model.rowCount(); ++row) {
            result.append(model.data(model.index(row), ToolModel::NameRole).toString());
        }
        return result;
    };

    QCOMPARE(names(), QStringList({QStringLiteral("Delta"), QStringLiteral("Beta"), QStringLiteral("Epsilon"),
                                   QStringLiteral("Alpha"), QStringLiteral("Gamma")}));
    model.setSelectedCategory(QStringLiteral("Utilities"));
    QCOMPARE(names(), QStringList({QStringLiteral("Alpha"), QStringLiteral("Gamma")}));
    model.setSelectedCategory(QStringLiteral("Setup"));
    QCOMPARE(names(), QStringList({QStringLiteral("Beta"), QStringLiteral("Epsilon")}));
    // A search ignores the category.
    model.setSearch(QStringLiteral("a"));
    QCOMPARE(names(), QStringList({QStringLiteral("Delta"), QStringLiteral("Beta"), QStringLiteral("Alpha"),
                                   QStringLiteral("Gamma")}));
    model.setSearch({});
    QCOMPARE(names(), QStringList({QStringLiteral("Beta"), QStringLiteral("Epsilon")}));
    model.setSelectedCategory({});
    QCOMPARE(model.rowCount(), 5);

    QCOMPARE(resets.count(), 0);
    QVERIFY(!removals.isEmpty());
    QVERIFY(!insertions.isEmpty());

    // Changing the category while searching changes nothing, so it signals nothing.
    model.setSearch(QStringLiteral("a"));
    const qsizetype changes = removals.count() + insertions.count();
    model.setSelectedCategory(QStringLiteral("Setup"));
    QCOMPARE(removals.count() + insertions.count(), changes);
}

QTEST_MAIN(TestToolModel)
#include "test_toolmodel.moc"
