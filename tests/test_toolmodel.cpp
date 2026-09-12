#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
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
}

class TestToolModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void discoversAndFiltersTools();
    void launchBlocksRelaunchOnlyWhileRunning();

private:
    QScopedPointer<QTemporaryDir> m_home;
};

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
}

void TestToolModel::cleanup()
{
    QDir(QStringLiteral(MX_TOOLS_APPLICATIONS_PATH)).removeRecursively();
    qunsetenv("MX_TOOLS_TEST_FORCE_LIVE");
    m_home.reset();
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

QTEST_MAIN(TestToolModel)
#include "test_toolmodel.moc"
