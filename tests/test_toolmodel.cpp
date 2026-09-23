#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include "toolmodel.h"

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
    void restoreReinsertsDroppedWhiskerFavorite();
    void failedPluginQueryKeepsFavoritesSnapshot();
    void legacyScalarFavoritesAreRestoredAsArray();
    void unrelatedFavoritesSnapshotNeedsNoQuery();
    void failedFavoritesWriteKeepsSnapshot();
    void missingXfconfQueryKeepsSnapshot();

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
    // test gets a private HOME/XDG_CONFIG_HOME instead of touching the
    // developer's actual configuration.
    qputenv("HOME", m_home->path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (m_home->path() + QStringLiteral("/.config")).toUtf8());
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
    writeDesktopFile(applications, QStringLiteral("live-only.desktop"),
                      desktopFileContent(QStringLiteral("Live Only Tool"), QStringLiteral("MX-Live"),
                                         QStringLiteral("MX-OnlyLive=true\n")));
    writeDesktopFile(applications, QStringLiteral("installed-only.desktop"),
                      desktopFileContent(QStringLiteral("Installed Only Tool"), QStringLiteral("MX-Maintenance"),
                                         QStringLiteral("MX-OnlyInstalled=true\n")));

    qputenv("XDG_CURRENT_DESKTOP", "XFCE");

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);

    // MX_TOOLS_TEST_FORCE_LIVE=0 means MX-OnlyLive is excluded and
    // MX-OnlyInstalled is kept; NotShowIn=XFCE and OnlyShowIn=KDE both exclude
    // their entries under XDG_CURRENT_DESKTOP=XFCE, so only "Utility Tool" and
    // "Installed Only Tool" should survive.
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

    // On a live session the MX-OnlyLive/MX-OnlyInstalled pair swaps over.
    qputenv("MX_TOOLS_TEST_FORCE_LIVE", "1");
    ToolModel liveModel(&iconProvider);
    QCOMPARE(liveModel.totalCount(), 2);
    QCOMPARE(liveModel.categories(), QStringList({QStringLiteral("All tools"), QStringLiteral("Live"),
                                                   QStringLiteral("Utilities")}));
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

void TestToolModel::restoreReinsertsDroppedWhiskerFavorite()
{
    writeMenuTool();
    setFavorites({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")});

    ToolIconProvider iconProvider;
    ToolModel model(&iconProvider);
    model.setHideFromMenu(true);
    QVERIFY(model.hideFromMenu());

    // Whisker Menu drops favorites whose launchers become hidden.
    setFavorites({QStringLiteral("other.desktop")});
    model.setHideFromMenu(false);
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
    model.setHideFromMenu(true);
    QVERIFY(model.hideFromMenu());
    setFavorites({QStringLiteral("other.desktop")});

    // A failing plugin-type query must not be mistaken for a removed plugin.
    setFakeXfconfFailure(QStringLiteral("type"), true);
    model.setHideFromMenu(false);
    QVERIFY(model.hideFromMenu());
    QCOMPARE(errors.count(), 1);
    QVERIFY(menuStateActive());
    QCOMPARE(favorites(), QStringList({QStringLiteral("other.desktop")}));

    // The kept snapshot lets the next attempt finish the restore.
    setFakeXfconfFailure(QStringLiteral("type"), false);
    model.setHideFromMenu(false);
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
    model.setHideFromMenu(true);
    setScalarFavorite(QStringLiteral("other.desktop"));
    model.setHideFromMenu(false);
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));

    // A plain-string snapshot is restored, and a single favorite stays an array.
    setScalarFavorite(QStringLiteral("tool.desktop"));
    model.setHideFromMenu(true);
    QFile::remove(fakeXfconfPath(QStringLiteral("plugin-1-favorites.scalar")));
    model.setHideFromMenu(false);
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
    model.setHideFromMenu(true);
    QVERIFY(model.hideFromMenu());

    // None of our launchers were favorites, so xfconf failures can't block the restore.
    setFakeXfconfFailure(QStringLiteral("type"), true);
    setFakeXfconfFailure(QStringLiteral("favorites"), true);
    model.setHideFromMenu(false);
    QVERIFY(!model.hideFromMenu());
    QCOMPARE(errors.count(), 0);
    QVERIFY(!menuStateActive());

    // The same holds when xfconf-query is gone altogether.
    setFakeXfconfFailure(QStringLiteral("type"), false);
    setFakeXfconfFailure(QStringLiteral("favorites"), false);
    model.setHideFromMenu(true);
    QVERIFY(model.hideFromMenu());
    qputenv("PATH", m_home->path().toUtf8());
    model.setHideFromMenu(false);
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
    model.setHideFromMenu(true);
    QVERIFY(model.hideFromMenu());
    setFavorites({QStringLiteral("other.desktop")});

    // A failed write must leave the current favorites and the snapshot intact.
    setFakeXfconfFailure(QStringLiteral("write"), true);
    model.setHideFromMenu(false);
    QVERIFY(model.hideFromMenu());
    QCOMPARE(errors.count(), 1);
    QVERIFY(menuStateActive());
    QCOMPARE(favorites(), QStringList({QStringLiteral("other.desktop")}));

    setFakeXfconfFailure(QStringLiteral("write"), false);
    model.setHideFromMenu(false);
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
    model.setHideFromMenu(true);
    QVERIFY(model.hideFromMenu());
    setFavorites({QStringLiteral("other.desktop")});

    // The state file outlives the process, so a restore without xfconf-query
    // (for example after a restart with a different PATH) must keep it.
    qputenv("PATH", m_home->path().toUtf8());
    ToolModel restarted(&iconProvider);
    QVERIFY(restarted.hideFromMenu());
    restarted.setHideFromMenu(false);
    QVERIFY(restarted.hideFromMenu());
    QVERIFY(menuStateActive());

    qputenv("PATH", m_path);
    restarted.setHideFromMenu(false);
    QVERIFY(!restarted.hideFromMenu());
    QCOMPARE(favorites(), QStringList({QStringLiteral("tool.desktop"), QStringLiteral("other.desktop")}));
}

QTEST_MAIN(TestToolModel)
#include "test_toolmodel.moc"
