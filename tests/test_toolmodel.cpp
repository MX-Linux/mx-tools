#include <QAbstractItemModelTester>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include "toolmodel.h"

#include <optional>

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

// The helpers return whether they succeeded, for the caller to QVERIFY: a QVERIFY in
// a void helper would only return from the helper and let the test carry on.
[[nodiscard]] bool writeDesktopFile(const QDir &directory, const QString &fileName, const QString &content)
{
    QFile file(directory.filePath(fileName));
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        qWarning("Could not write %s", qPrintable(file.fileName()));
        return false;
    }
    QTextStream stream(&file);
    stream << content;
    stream.flush();
    return stream.status() == QTextStream::Ok;
}

QString fakeXfconfPath(const QString &name)
{
    return QDir(QString::fromLocal8Bit(qgetenv("FAKE_XFCONF_DIR"))).filePath(name);
}

[[nodiscard]] bool writeFakeXfconf(const QString &name, const QString &content)
{
    QFile file(fakeXfconfPath(name));
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        qWarning("Could not write %s", qPrintable(file.fileName()));
        return false;
    }
    const QByteArray bytes = content.toUtf8();
    return file.write(bytes) == bytes.size();
}

[[nodiscard]] bool removeIfPresent(const QString &path)
{
    return !QFile::exists(path) || QFile::remove(path);
}

[[nodiscard]] bool setFavorites(const QStringList &favorites)
{
    return removeIfPresent(fakeXfconfPath(QStringLiteral("plugin-1-favorites.scalar")))
           && writeFakeXfconf(QStringLiteral("plugin-1-favorites"), favorites.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}

[[nodiscard]] bool setScalarFavorite(const QString &favorite)
{
    return removeIfPresent(fakeXfconfPath(QStringLiteral("plugin-1-favorites")))
           && writeFakeXfconf(QStringLiteral("plugin-1-favorites.scalar"), favorite + QLatin1Char('\n'));
}

QStringList favorites()
{
    QFile file(fakeXfconfPath(QStringLiteral("plugin-1-favorites")));
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

[[nodiscard]] bool setFakeXfconfFailure(const QString &kind, bool fail)
{
    if (fail) {
        return writeFakeXfconf(QStringLiteral("fail-") + kind, {});
    }
    return removeIfPresent(fakeXfconfPath(QStringLiteral("fail-") + kind));
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
    void userOverrideSurvivesRoundTrip();
    void editsMadeWhileHiddenAreKept();
    void legacyOverrideIsRestoredButUserOverrideIsNot();
    void failedHideRollsBack();
    void localizedNoisyXfconfQueryIsParsed();
    void showsTranslationsAndSearchesEnglish();
    void unreadableFavoritesStopHiding();
    void pendingFavoritesDontHideNewLaunchers();
    void emptyPanelListingStillHides();

private:
    [[nodiscard]] bool writeMenuTool();

    QTemporaryDir m_fakeBin;
    QByteArray m_path;
    QScopedPointer<QTemporaryDir> m_home;
};

void TestToolModel::initTestCase()
{
    // Put the fake first in PATH so no test reaches a real xfce4-panel.
    // tests/fake-xfconf-query documents how it stores the panel properties.
    QVERIFY(m_fakeBin.isValid());
    // An absolute path from CMake: QFINDTESTDATA relies on __FILE__, which Debian's
    // -ffile-prefix-map turns into a relative path it can't resolve.
    const QString source = QStringLiteral(MX_TOOLS_FAKE_XFCONF_QUERY);
    QVERIFY2(QFileInfo::exists(source), qPrintable(source));
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
    QVERIFY(writeFakeXfconf(QStringLiteral("plugins"), QStringLiteral("1\n")));
    QVERIFY(writeFakeXfconf(QStringLiteral("plugin-1"), QStringLiteral("whiskermenu\n")));
}

void TestToolModel::cleanup()
{
    QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)).removeRecursively();
    qunsetenv("MX_TOOLS_TEST_FORCE_LIVE");
    qunsetenv("FAKE_XFCONF_DIR");
    qunsetenv("FAKE_XFCONF_NOISY");
    qputenv("PATH", m_path);
    m_home.reset();
}

bool TestToolModel::writeMenuTool()
{
    return writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("tool.desktop"),
                     desktopFileContent(QStringLiteral("Tool"), QStringLiteral("X-MX-Utilities")));
}

void TestToolModel::discoversAndFiltersTools()
{
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));

    QVERIFY(writeDesktopFile(applications, QStringLiteral("utility.desktop"),
                              desktopFileContent(QStringLiteral("Utility Tool"), QStringLiteral("X-MX-Utilities"))));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("setup-hidden.desktop"),
                              desktopFileContent(QStringLiteral("Hidden Setup Tool"), QStringLiteral("MX-Setup"),
                                                 QStringLiteral("NotShowIn=XFCE;\n"))));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("software-wrong-desktop.desktop"),
                              desktopFileContent(QStringLiteral("KDE-only Software"), QStringLiteral("X-MX-Software"),
                                                 QStringLiteral("OnlyShowIn=KDE;\n"))));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("mx-remastercc.desktop"),
                              desktopFileContent(QStringLiteral("Live Only Tool"), QStringLiteral("MX-Live"))));
    // Mentioning a marker in the text no longer hides a tool.
    QVERIFY(writeDesktopFile(applications, QStringLiteral("installed.desktop"),
                              desktopFileContent(QStringLiteral("Installed Tool"), QStringLiteral("MX-Maintenance"),
                                                 QStringLiteral("Keywords=MX-OnlyLive;\n"))));

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
    // The tool runs until the test creates a release file, so "still running" doesn't
    // depend on timing. The guard releases it on any exit, so it can't be left behind.
    const QString waiter = m_home->filePath(QStringLiteral("wait-for-release"));
    const QString release = m_home->filePath(QStringLiteral("release"));
    {
        QFile script(waiter);
        QVERIFY(script.open(QFile::WriteOnly | QFile::Text));
        QVERIFY(script.write("#!/bin/sh\nwhile [ ! -e \"$1\" ]; do sleep 0.05; done\n") > 0);
    }
    QVERIFY(QFile::setPermissions(waiter, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto releaseTool = qScopeGuard([&release] { QFile(release).open(QFile::WriteOnly); });

    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("waiter.desktop"),
                             QStringLiteral("[Desktop Entry]\nType=Application\nName=Waiter\n"
                                            "Categories=X-MX-Utilities;\nExec=\"%1\" \"%2\"\n")
                                 .arg(waiter, release)));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QCOMPARE(model.totalCount(), 1);
    const QString fileName = model.data(model.index(0), ToolModel::FileNameRole).toString();

    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    model.launch(fileName);
    model.launch(fileName);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.constFirst().at(0).toString(), QStringLiteral("Tool already running"));

    // Once the tool has exited, launching again must be allowed. Poll for that: each
    // attempt while it still runs adds an error. QTRY_* evaluates its expression again
    // after it succeeds, so stop launching then. This doesn't exercise the zombie case:
    // QProcess::startDetached double-forks, so the child is reaped by init.
    QVERIFY(QFile(release).open(QFile::WriteOnly));
    bool relaunched = false;
    const auto tryRelaunch = [&] {
        if (!relaunched) {
            const qsizetype before = errors.count();
            model.launch(fileName);
            relaunched = errors.count() == before;
        }
        return relaunched;
    };
    QTRY_VERIFY_WITH_TIMEOUT(tryRelaunch(), 10000);
}

void TestToolModel::readsOnlyTheDesktopEntryGroup()
{
    // Keys from other groups must not fill in missing ones, spacing around '='
    // is allowed, and values are unescaped.
    QVERIFY(writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("parsed.desktop"),
                             QStringLiteral("# Comment=Not a comment\n"
                                            "[Desktop Entry]\n"
                                            "Type=Application\n"
                                            "Name = Parsed\\sTool\n"
                                            "Exec=/bin/true\n"
                                            "Categories = System;X-MX-Utilities;\n"
                                            "Keywords=semi\\;colon;other;\n"
                                            "[Desktop Action extra]\n"
                                            "Name=Extra\n"
                                            "Comment=Action comment\n")));

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
    QVERIFY(writeDesktopFile(applications, QStringLiteral("both.desktop"),
                             desktopFileContent(QStringLiteral("Both"), QStringLiteral("X-MX-Utilities;MX-Setup;"))));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("setup.desktop"),
                             desktopFileContent(QStringLiteral("Setup Only"), QStringLiteral("X-MX-Setup;"))));

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
    QVERIFY(writeDesktopFile(applications, QStringLiteral("recorder.desktop"),
                             QStringLiteral("[Desktop Entry]\nType=Application\nName=MX Recorder\nIcon=recorder-icon\n"
                                            "Categories=X-MX-Utilities;\nExec=\"%1\" %F \"quoted arg\" 100%% --name=%c %i %k "
                                            "\"say \\\\\"hi\\\\\"\" \"\" \"100%% done\" %d%m\n")
                                 .arg(recorder)));

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
    QVERIFY(writeDesktopFile(applications, QStringLiteral("invalid.desktop"),
                             QStringLiteral("[Desktop Entry]\nType=Application\nName=Invalid\n"
                                            "Categories=X-MX-Utilities;\nExec=\"%1\" --mode=%Z\n")
                                 .arg(recorder)));
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
    QVERIFY(writeMenuTool());
    QVERIFY(setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());

    // Whisker Menu drops favorites whose launchers become hidden.
    QVERIFY(setFavorites({QStringLiteral("other.desktop")}));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));
    QVERIFY(!menuStateActive());
}

void TestToolModel::failedPluginQueryKeepsFavoritesSnapshot()
{
    QVERIFY(writeMenuTool());
    QVERIFY(setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    QVERIFY(setFavorites({QStringLiteral("other.desktop")}));

    // A failing plugin-type query must not be mistaken for a removed plugin.
    QVERIFY(setFakeXfconfFailure(QStringLiteral("type"), true));
    QSignalSpy visibilityChanges(&model, &ToolModel::hideFromMenuChanged);
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(model.hideFromMenu());
    // The switch already flipped itself; it needs the signal to flip back.
    QVERIFY(!visibilityChanges.isEmpty());
    QCOMPARE(errors.count(), 1);
    QVERIFY(menuStateActive());
    QCOMPARE(favorites(), QStringList({QStringLiteral("other.desktop")}));

    // The kept snapshot lets the next attempt finish the restore.
    QVERIFY(setFakeXfconfFailure(QStringLiteral("type"), false));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));
    QVERIFY(!menuStateActive());
}

void TestToolModel::legacyScalarFavoritesAreRestoredAsArray()
{
    QVERIFY(writeMenuTool());
    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);

    // Earlier releases stored a single restored favorite as a plain string.
    QVERIFY(setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(setScalarFavorite(QStringLiteral("other.desktop")));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));

    // A plain-string snapshot is restored, and a single favorite stays an array.
    QVERIFY(setScalarFavorite(QStringLiteral("tool.desktop")));
    QVERIFY(setHideFromMenu(model, true));
    QFile::remove(fakeXfconfPath(QStringLiteral("plugin-1-favorites.scalar")));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QVERIFY(!QFile::exists(fakeXfconfPath(QStringLiteral("plugin-1-favorites.scalar"))));
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop")}));
}

void TestToolModel::unrelatedFavoritesSnapshotNeedsNoQuery()
{
    QVERIFY(writeMenuTool());
    QVERIFY(setFavorites({QStringLiteral("other.desktop")}));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());

    // None of our launchers were favorites, so xfconf failures can't block the restore.
    QVERIFY(setFakeXfconfFailure(QStringLiteral("type"), true));
    QVERIFY(setFakeXfconfFailure(QStringLiteral("favorites"), true));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(errors.count(), 0);
    QVERIFY(!menuStateActive());

    // The same holds when xfconf-query is gone altogether.
    QVERIFY(setFakeXfconfFailure(QStringLiteral("type"), false));
    QVERIFY(setFakeXfconfFailure(QStringLiteral("favorites"), false));
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    qputenv("PATH", m_home->path().toUtf8());
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(errors.count(), 0);
}

void TestToolModel::failedFavoritesWriteKeepsSnapshot()
{
    QVERIFY(writeMenuTool());
    QVERIFY(setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    QVERIFY(setFavorites({QStringLiteral("other.desktop")}));

    // A failed write must leave the current favorites and the snapshot intact.
    QVERIFY(setFakeXfconfFailure(QStringLiteral("write"), true));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(model.hideFromMenu());
    QCOMPARE(errors.count(), 1);
    QVERIFY(menuStateActive());
    QCOMPARE(favorites(), QStringList({QStringLiteral("other.desktop")}));

    QVERIFY(setFakeXfconfFailure(QStringLiteral("write"), false));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));
    QVERIFY(!menuStateActive());
}

void TestToolModel::missingXfconfQueryKeepsSnapshot()
{
    QVERIFY(writeMenuTool());
    QVERIFY(setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    QVERIFY(setFavorites({QStringLiteral("other.desktop")}));

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
    QVERIFY(writeMenuTool());
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
    QVERIFY(writeDesktopFile(QDir(applications.filePath(QStringLiteral("mx"))), QStringLiteral("tool.desktop"),
                             desktopFileContent(QStringLiteral("Nested"), QStringLiteral("X-MX-Utilities"))));
    QVERIFY(writeMenuTool());
    QVERIFY(setFavorites({QStringLiteral("mx-tool.desktop"), QStringLiteral("tool.desktop")}));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QCOMPARE(model.totalCount(), 2);
    QVERIFY(setHideFromMenu(model, true));
    const QDir overrides(m_home->filePath(QStringLiteral(".local/share/applications")));
    QVERIFY(QFileInfo::exists(overrides.filePath(QStringLiteral("mx-tool.desktop"))));
    QVERIFY(QFileInfo::exists(overrides.filePath(QStringLiteral("tool.desktop"))));

    QVERIFY(setFavorites({}));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QVERIFY(!QFileInfo::exists(overrides.filePath(QStringLiteral("mx-tool.desktop"))));
    QVERIFY(!QFileInfo::exists(overrides.filePath(QStringLiteral("tool.desktop"))));
    QCOMPARE(favorites(), QStringList({QStringLiteral("mx-tool.desktop"), QStringLiteral("tool.desktop")}));
}

void TestToolModel::launchersInstalledWhileHiddenAreHidden()
{
    QVERIFY(writeMenuTool());
    ToolIconProvider iconProvider;
    {
        ToolModel model(&iconProvider);
        QVERIFY(setHideFromMenu(model, true));
        QVERIFY(model.hideFromMenu());
    }

    // A package installs another MX tool while the tools are hidden.
    QVERIFY(writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("new.desktop"),
                             desktopFileContent(QStringLiteral("New"), QStringLiteral("X-MX-Setup"))));
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
    QVERIFY(writeMenuTool());
    ToolIconProvider iconProvider;
    {
        ToolModel model(&iconProvider);
        QVERIFY(setHideFromMenu(model, true));
        QVERIFY(model.hideFromMenu());
    }
    QVERIFY(writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("new.desktop"),
                             desktopFileContent(QStringLiteral("New"), QStringLiteral("X-MX-Setup"))));
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
    QVERIFY(writeMenuTool());
    ToolIconProvider iconProvider;
    {
        ToolModel model(&iconProvider);
        QVERIFY(setHideFromMenu(model, true));
        QVERIFY(model.hideFromMenu());
    }
    QVERIFY(writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("new.desktop"),
                             desktopFileContent(QStringLiteral("New"), QStringLiteral("X-MX-Setup"))));
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
    QVERIFY(writeMenuTool());
    QVERIFY(writeFakeXfconf(QStringLiteral("plugins"), QStringLiteral("1\n2\n3\n")));
    QVERIFY(writeFakeXfconf(QStringLiteral("plugin-2"), QStringLiteral("whiskermenu\n")));
    QVERIFY(writeFakeXfconf(QStringLiteral("plugin-3"), QStringLiteral("launcher\n")));
    QVERIFY(setFavorites({QStringLiteral("tool.desktop")}));
    QVERIFY(writeFakeXfconf(QStringLiteral("plugin-2-favorites"), QStringLiteral("tool.desktop\n")));

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
    QVERIFY(writeFakeXfconf(QStringLiteral("plugin-2"), QStringLiteral("launcher\n")));
    QVERIFY(setFavorites({}));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop")}));

    // An empty listing looks like an unreachable xfconfd, so the snapshot is kept.
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(setFavorites({}));
    QVERIFY(writeFakeXfconf(QStringLiteral("plugins"), {}));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(model.hideFromMenu());
    QVERIFY(menuStateActive());
}

void TestToolModel::menuChangesRunInTheBackground()
{
    QVERIFY(writeMenuTool());
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
    QVERIFY(writeDesktopFile(applications, QStringLiteral("backup.desktop"),
                             desktopFileContent(QStringLiteral("Backup"), QStringLiteral("X-MX-Utilities"))));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("cleanup.desktop"),
                             desktopFileContent(QStringLiteral("Cleanup"), QStringLiteral("X-MX-Maintenance"))));

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
        QVERIFY(writeDesktopFile(applications, QStringLiteral("tool%1.desktop").arg(index),
                                 desktopFileContent(names.at(index), QStringLiteral("X-MX-Utilities"))));
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
        QVERIFY(writeDesktopFile(applications, name.toLower() + QStringLiteral(".desktop"), desktopFileContent(name, category)));
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

namespace
{
QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QFile::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    return QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QFile::WriteOnly)
           && file.write(content) == content.size();
}
}

void TestToolModel::userOverrideSurvivesRoundTrip()
{
    QVERIFY(writeMenuTool());
    const QString override = m_home->filePath(QStringLiteral(".local/share/applications/tool.desktop"));
    const QByteArray userOverride = "[Desktop Entry]\nType=Application\nName=My Tool\nNoDisplay=false\n"
                                    "Exec=/bin/true\n\n[Desktop Action extra]\nNoDisplay=true\n";
    QVERIFY(writeFile(override, userOverride));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QVERIFY(setHideFromMenu(model, true));
    const QByteArray hidden = readFile(override);
    QVERIFY(hidden.contains("NoDisplay=true\nType=Application\nName=My Tool"));
    QVERIFY(!hidden.contains("NoDisplay=false"));

    QVERIFY(setHideFromMenu(model, false));
    QCOMPARE(readFile(override), userOverride);
}

void TestToolModel::editsMadeWhileHiddenAreKept()
{
    QVERIFY(writeMenuTool());
    QVERIFY(writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("second.desktop"),
                             desktopFileContent(QStringLiteral("Second"), QStringLiteral("X-MX-Setup"))));
    const QDir overrides(m_home->filePath(QStringLiteral(".local/share/applications")));
    // tool.desktop starts with the user's own override; second.desktop has none.
    QVERIFY(writeFile(overrides.filePath(QStringLiteral("tool.desktop")),
                      "[Desktop Entry]\nType=Application\nName=Tool\nHidden=false\nExec=/bin/true\n"));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QVERIFY(setHideFromMenu(model, true));

    // The user edits both overrides while the tools are hidden.
    for (const QString &name : {QStringLiteral("tool.desktop"), QStringLiteral("second.desktop")}) {
        const QString path = overrides.filePath(name);
        QVERIFY(writeFile(path, readFile(path) + "Comment=Edited\n"));
    }

    QVERIFY(setHideFromMenu(model, false));
    const QByteArray tool = readFile(overrides.filePath(QStringLiteral("tool.desktop")));
    QVERIFY(tool.contains("Comment=Edited"));
    QVERIFY(tool.contains("Hidden=false"));
    QVERIFY(!tool.contains("NoDisplay"));
    // Our own override is kept once edited, without the visibility lines we added.
    const QByteArray second = readFile(overrides.filePath(QStringLiteral("second.desktop")));
    QVERIFY(second.contains("Comment=Edited"));
    QVERIFY(!second.contains("NoDisplay"));
}

void TestToolModel::legacyOverrideIsRestoredButUserOverrideIsNot()
{
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));
    const QString legacyContent = desktopFileContent(QStringLiteral("Legacy"), QStringLiteral("X-MX-Setup"));
    const QString userContent = desktopFileContent(QStringLiteral("Mine"), QStringLiteral("X-MX-Setup"));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("legacy.desktop"), legacyContent));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("mine.desktop"), userContent));
    const QDir overrides(m_home->filePath(QStringLiteral(".local/share/applications")));
    // Old releases copied the launcher with NoDisplay=true right after the group header.
    QVERIFY(writeFile(overrides.filePath(QStringLiteral("legacy.desktop")),
                      QString(legacyContent)
                          .replace(QStringLiteral("[Desktop Entry]\n"), QStringLiteral("[Desktop Entry]\nNoDisplay=true\n"))
                          .toUtf8()));
    // The user's own hiding override differs from that transformation.
    const QByteArray userOverride = "[Desktop Entry]\nType=Application\nName=Mine\nNoDisplay=true\n";
    QVERIFY(writeFile(overrides.filePath(QStringLiteral("mine.desktop")), userOverride));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QVERIFY(model.hideFromMenu());
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QVERIFY(!QFileInfo::exists(overrides.filePath(QStringLiteral("legacy.desktop"))));
    QCOMPARE(readFile(overrides.filePath(QStringLiteral("mine.desktop"))), userOverride);

    // With only the user's override left, nothing looks hidden by us.
    ToolModel restarted(&iconProvider);
    QVERIFY(!restarted.hideFromMenu());
}

void TestToolModel::failedHideRollsBack()
{
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("first.desktop"),
                             desktopFileContent(QStringLiteral("First"), QStringLiteral("X-MX-Setup"))));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("second.desktop"),
                             desktopFileContent(QStringLiteral("Second"), QStringLiteral("X-MX-Setup"))));
    // Make the launcher processed last fail: a directory where its override goes can't
    // be read or replaced. Discovery walks the directory in this same order.
    QStringList order;
    QDirIterator iterator(applications.path(), {QStringLiteral("*.desktop")}, QDir::Files);
    while (iterator.hasNext()) {
        order.append(QFileInfo(iterator.next()).fileName());
    }
    QCOMPARE(order.size(), 2);
    const QDir overrides(m_home->filePath(QStringLiteral(".local/share/applications")));
    QVERIFY(overrides.mkpath(order.constLast()));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(errors.count(), 1);
    // The override written before the failure was rolled back, and no state remains.
    QVERIFY(!QFileInfo::exists(overrides.filePath(order.constFirst())));
    QVERIFY(!menuStateActive());
}

void TestToolModel::localizedNoisyXfconfQueryIsParsed()
{
    QVERIFY(writeMenuTool());
    QVERIFY(setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));
    // A German session whose xfconf-query also prints GLib warnings. LC_ALL must be
    // German too: builders such as dh_auto_test export LC_ALL=C.UTF-8, which would keep
    // the fake in English even without the override in runXfconfQuery.
    const QList<QByteArray> localeVariables {"LC_ALL", "LANGUAGE", "LANG"};
    QList<std::optional<QByteArray>> previous;
    for (const QByteArray &name : localeVariables) {
        previous.append(qEnvironmentVariableIsSet(name.constData()) ? std::optional(qgetenv(name.constData()))
                                                                    : std::nullopt);
    }
    const auto restoreLocale = qScopeGuard([&] {
        for (qsizetype index = 0; index < localeVariables.size(); ++index) {
            if (previous.at(index)) {
                qputenv(localeVariables.at(index).constData(), *previous.at(index));
            } else {
                qunsetenv(localeVariables.at(index).constData());
            }
        }
    });
    qputenv("LC_ALL", "de_DE.UTF-8");
    qputenv("LANGUAGE", "de");
    qputenv("LANG", "de_DE.UTF-8");
    qputenv("FAKE_XFCONF_NOISY", "1");

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    QVERIFY(setFavorites({QStringLiteral("other.desktop")}));
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));
}

void TestToolModel::showsTranslationsAndSearchesEnglish()
{
    const QDir applications(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("cleanup.desktop"),
                             desktopFileContent(QStringLiteral("MX Cleanup"), QStringLiteral("X-MX-Maintenance"),
                                                QStringLiteral("Name[de]=MX Aufräumen\nComment[de]=Speicher freigeben\n"
                                                               "Keywords=disk;\nKeywords[de]=Platte;\n"))));
    QVERIFY(writeDesktopFile(applications, QStringLiteral("untranslated.desktop"),
                             desktopFileContent(QStringLiteral("Untranslated"), QStringLiteral("X-MX-Setup"))));

    const QLocale previous;
    const auto restoreLocale = qScopeGuard([&previous] { QLocale::setDefault(previous); });
    ToolIconProvider iconProvider;
    // de_AT has no entries of its own, so it falls back to the de ones.
    for (const QLocale &locale : {QLocale(QLocale::German, QLocale::Germany), QLocale(QLocale::German, QLocale::Austria)}) {
        QLocale::setDefault(locale);
        ToolModel model(&iconProvider);
        QCOMPARE(model.totalCount(), 2);
        model.setSearch(QStringLiteral("Aufräumen"));
        QCOMPARE(model.rowCount(), 1);
        // The "MX " prefix is dropped from the translated name as well.
        QCOMPARE(model.data(model.index(0), ToolModel::NameRole).toString(), QStringLiteral("Aufräumen"));
        QCOMPARE(model.data(model.index(0), ToolModel::CommentRole).toString(), QStringLiteral("Speicher freigeben"));
        // The English name, comment and keywords still find it, as do the German keywords.
        for (const QString &search : {QStringLiteral("cleanup"), QStringLiteral("Cleanup comment"),
                                      QStringLiteral("disk"), QStringLiteral("Platte")}) {
            model.setSearch(search);
            QCOMPARE(model.rowCount(), 1);
            QCOMPARE(model.data(model.index(0), ToolModel::FileNameRole).toString(),
                     applications.filePath(QStringLiteral("cleanup.desktop")));
        }
        // Without a translation the English text is shown.
        model.setSearch(QStringLiteral("Untranslated"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), ToolModel::CommentRole).toString(), QStringLiteral("Untranslated comment"));
    }
}

void TestToolModel::unreadableFavoritesStopHiding()
{
    QVERIFY(writeMenuTool());
    QVERIFY(setFavorites({QStringLiteral("tool.desktop")}));
    const QString override = m_home->filePath(QStringLiteral(".local/share/applications/tool.desktop"));

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QSignalSpy errors(&model, &ToolModel::errorOccurred);
    // Hiding would make Whisker Menu drop the favorite, and an unreadable snapshot could
    // never put it back, so neither a failed read nor a failed plugin listing may hide.
    for (const QString &kind : {QStringLiteral("favorites"), QStringLiteral("type")}) {
        QVERIFY(setFakeXfconfFailure(kind, true));
        QVERIFY(setHideFromMenu(model, true));
        QVERIFY2(!model.hideFromMenu(), qPrintable(kind));
        QVERIFY(!QFileInfo::exists(override));
        QVERIFY(!menuStateActive());
        QVERIFY(setFakeXfconfFailure(kind, false));
    }
    QCOMPARE(errors.count(), 2);
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop")}));

    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
}

void TestToolModel::pendingFavoritesDontHideNewLaunchers()
{
    QVERIFY(writeMenuTool());
    QVERIFY(setFavorites({QStringLiteral("tool.desktop")}));
    const QDir overrides(m_home->filePath(QStringLiteral(".local/share/applications")));

    ToolIconProvider iconProvider;
    {
        ToolModel model(&iconProvider);
        QVERIFY(setHideFromMenu(model, true));
        QVERIFY(model.hideFromMenu());
        // The launchers come back, but the favorites can't be restored yet.
        QVERIFY(setFavorites({}));
        QVERIFY(setFakeXfconfFailure(QStringLiteral("type"), true));
        QVERIFY(setHideFromMenu(model, false));
        QVERIFY(!QFileInfo::exists(overrides.filePath(QStringLiteral("tool.desktop"))));
        QVERIFY(menuStateActive());
    }

    // A tool installed meanwhile must not be hidden while only the favorites are pending.
    QVERIFY(writeDesktopFile(QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)), QStringLiteral("new.desktop"),
                             desktopFileContent(QStringLiteral("New"), QStringLiteral("X-MX-Setup"))));
    ToolModel restarted(&iconProvider);
    QVERIFY(!QFileInfo::exists(overrides.filePath(QStringLiteral("new.desktop"))));

    // Once xfconf-query works again, the retry restores the favorite and clears the state.
    QVERIFY(setFakeXfconfFailure(QStringLiteral("type"), false));
    QVERIFY(setHideFromMenu(restarted, false));
    QVERIFY(!restarted.hideFromMenu());
    QVERIFY(!menuStateActive());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop")}));
}

void TestToolModel::emptyPanelListingStillHides()
{
    // A desktop without an Xfce panel has an empty xfce4-panel channel: nothing to
    // snapshot, so hiding and restoring work without Whisker Menu state.
    QVERIFY(writeMenuTool());
    QVERIFY(writeFakeXfconf(QStringLiteral("plugins"), {}));
    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    QVERIFY(setHideFromMenu(model, true));
    QVERIFY(model.hideFromMenu());
    QVERIFY(setHideFromMenu(model, false));
    QVERIFY(!model.hideFromMenu());
    QVERIFY(!menuStateActive());
}

QTEST_MAIN(TestToolModel)
#include "test_toolmodel.moc"
