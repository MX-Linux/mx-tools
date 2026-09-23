/**********************************************************************
 * Copyright (C) 2014-2026 MX Authors
 *
 * This file is part of MX Tools and is licensed under GPL-3.0-or-later.
 **********************************************************************/
#include "toolmodel.h"

#include <algorithm>

#include <QCollator>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFutureWatcher>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>
#include <QtConcurrentRun>

namespace
{
#ifdef MX_TOOLS_APPLICATIONS_PATH
constexpr auto applicationsPath = MX_TOOLS_APPLICATIONS_PATH;
#else
constexpr auto applicationsPath = "/usr/share/applications";
#endif
// Where releases before the state file wrote their overrides, whatever XDG_DATA_HOME says.
constexpr auto legacyUserApplicationsPath = "/.local/share/applications";
constexpr auto manualPath = "/usr/share/mx-docs/mxum_en.pdf";
constexpr auto licensePath = "/usr/share/doc/mx-tools/license.html";
#ifdef MX_TOOLS_CHANGELOG_PATH
constexpr auto changelogPath = MX_TOOLS_CHANGELOG_PATH;
#else
constexpr auto changelogPath = "/usr/share/doc/mx-tools/changelog.gz";
#endif
constexpr auto menuStateFileName = "menu-visibility.ini";

// Tools that only make sense on a live system, hidden once MX is installed.
const QStringList liveOnlyDesktopIds {QStringLiteral("mx-remastercc.desktop"),
                                      QStringLiteral("live-kernel-updater.desktop")};

const QList<QPair<QString, QStringList>> categoryDefinitions {
    {QStringLiteral("Live"), {QStringLiteral("MX-Live"), QStringLiteral("X-MX-Live")}},
    {QStringLiteral("Maintenance"), {QStringLiteral("MX-Maintenance"), QStringLiteral("X-MX-Maintenance")}},
    {QStringLiteral("Setup"), {QStringLiteral("MX-Setup"), QStringLiteral("X-MX-Setup")}},
    {QStringLiteral("Software"), {QStringLiteral("MX-Software"), QStringLiteral("X-MX-Software")}},
    {QStringLiteral("Utilities"), {QStringLiteral("MX-Utilities"), QStringLiteral("X-MX-Utilities")}}
};

QStringList currentDesktops()
{
    QStringList desktops = QString::fromUtf8(qgetenv("XDG_CURRENT_DESKTOP")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (desktops.isEmpty()) {
        desktops = QString::fromUtf8(qgetenv("XDG_SESSION_DESKTOP")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    }
    for (QString &desktop : desktops) {
        desktop = desktop.trimmed().toUpper();
    }
    return desktops;
}

bool isLiveEnvironment()
{
#ifdef MX_TOOLS_TESTING
    // The test suite must not depend on the build host's root filesystem: a
    // container root is an overlay mount, which would otherwise be detected
    // as a live session and silently invert the environment filtering.
    const QByteArray forcedLive = qgetenv("MX_TOOLS_TEST_FORCE_LIVE");
    if (!forcedLive.isEmpty()) {
        return forcedLive != "0";
    }
#endif
    const QByteArray fileSystem = QStorageInfo(QStringLiteral("/")).fileSystemType();
    return fileSystem == "aufs" || fileSystem == "overlay";
}

// The start time of a live process, in clock ticks since boot (field 22 of
// /proc/<pid>/stat), or nullopt if it has exited or is a zombie. Comparing it
// tells a tool we launched apart from an unrelated process that reused its PID.
std::optional<quint64> processStartTime(qint64 pid)
{
    QFile statFile(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!statFile.open(QFile::ReadOnly | QFile::Text)) {
        return std::nullopt;
    }
    // The command name in field 2 may contain spaces, so count fields after its ")".
    const QString stat = QString::fromLocal8Bit(statFile.readAll());
    const qsizetype closingParen = stat.lastIndexOf(QLatin1Char(')'));
    if (closingParen < 0) {
        return std::nullopt;
    }
    const QStringList fields = stat.mid(closingParen + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // fields[0] is the state (field 3), so the start time (field 22) is fields[19].
    if (fields.size() < 20 || fields.at(0) == QLatin1String("Z") || fields.at(0) == QLatin1String("X")) {
        return std::nullopt;
    }
    bool ok = false;
    const quint64 startTime = fields.at(19).toULongLong(&ok);
    return ok ? std::optional(startTime) : std::nullopt;
}

QString translatedCategory(const QString &category)
{
    if (category == QLatin1String("Live")) {
        return ToolModel::tr("Live");
    }
    if (category == QLatin1String("Maintenance")) {
        return ToolModel::tr("Maintenance");
    }
    if (category == QLatin1String("Setup")) {
        return ToolModel::tr("Setup");
    }
    if (category == QLatin1String("Software")) {
        return ToolModel::tr("Software");
    }
    return ToolModel::tr("Utilities");
}

// Unescapes a Desktop Entry value (\s, \n, \t, \r, \\). For lists, also splits on
// unescaped semicolons, keeping \; as a literal semicolon.
QStringList unescapeValue(const QString &value, bool isList)
{
    QStringList items;
    QString current;
    for (qsizetype index = 0; index < value.size(); ++index) {
        const QChar character = value.at(index);
        if (character == QLatin1Char('\\') && index + 1 < value.size()) {
            const QChar escaped = value.at(++index);
            switch (escaped.unicode()) {
            case 's':
                current += QLatin1Char(' ');
                break;
            case 'n':
                current += QLatin1Char('\n');
                break;
            case 't':
                current += QLatin1Char('\t');
                break;
            case 'r':
                current += QLatin1Char('\r');
                break;
            case '\\':
                current += QLatin1Char('\\');
                break;
            case ';':
                if (isList) {
                    current += QLatin1Char(';');
                    break;
                }
                [[fallthrough]];
            default:
                current += character;
                current += escaped;
            }
        } else if (isList && character == QLatin1Char(';')) {
            items.append(current);
            current.clear();
        } else {
            current += character;
        }
    }
    if (!isList || !current.isEmpty()) {
        items.append(current);
    }
    return items;
}

QStringList listValue(const ToolModel::DesktopEntry &entry, const QString &key)
{
    QStringList result = unescapeValue(entry.value(key), true);
    result.removeAll(QString());
    return result;
}

QStringList upperCaseListValue(const ToolModel::DesktopEntry &entry, const QString &key)
{
    QStringList result = listValue(entry, key);
    for (QString &item : result) {
        item = item.trimmed().toUpper();
    }
    return result;
}

// Splits an unescaped Exec value into arguments as the Desktop Entry spec describes:
// double quotes group an argument, and inside them a backslash escapes the next
// character. %% is a literal percent everywhere; other field codes are expanded outside
// quotes, with file, URL and deprecated codes dropped since tools are launched without
// files. The spec says a command with an unknown field code must not be run, so that
// returns nullopt.
std::optional<QStringList> execArguments(const QString &exec, const QString &name, const QString &icon,
                                         const QString &fileName)
{
    static const QString droppedCodes = QStringLiteral("fFuUdDnNvm");
    QStringList arguments;
    QString current;
    bool inArgument = false;
    bool quoted = false;
    bool hadQuotes = false;
    bool iconCode = false;
    const auto finishArgument = [&] {
        if (iconCode && current.isEmpty() && !hadQuotes) {
            if (!icon.isEmpty()) {
                arguments << QStringLiteral("--icon") << icon;
            }
        } else if (!current.isEmpty() || hadQuotes) {
            arguments.append(current);
        }
        current.clear();
        inArgument = hadQuotes = iconCode = false;
    };
    for (qsizetype index = 0; index < exec.size(); ++index) {
        const QChar character = exec.at(index);
        if (quoted) {
            if (character == QLatin1Char('\\') && index + 1 < exec.size()) {
                current += exec.at(++index);
            } else if (character == QLatin1Char('"')) {
                quoted = false;
            } else if (character == QLatin1Char('%') && index + 1 < exec.size()
                       && exec.at(index + 1) == QLatin1Char('%')) {
                current += QLatin1Char('%');
                ++index;
            } else {
                current += character;
            }
        } else if (character == QLatin1Char('"')) {
            quoted = inArgument = hadQuotes = true;
        } else if (character.isSpace()) {
            if (inArgument) {
                finishArgument();
            }
        } else if (character == QLatin1Char('%')) {
            if (index + 1 >= exec.size()) {
                return std::nullopt;
            }
            inArgument = true;
            const QChar code = exec.at(++index);
            switch (code.unicode()) {
            case '%':
                current += QLatin1Char('%');
                break;
            case 'c':
                current += name;
                break;
            case 'k':
                current += fileName;
                break;
            case 'i':
                iconCode = true;
                break;
            default:
                if (!droppedCodes.contains(code)) {
                    return std::nullopt;
                }
            }
        } else {
            current += character;
            inArgument = true;
        }
    }
    if (inArgument) {
        finishArgument();
    }
    return arguments;
}

// The Desktop Entry ID: the path below the applications directory with '/' replaced by
// '-', so applications/foo/bar.desktop is foo-bar.desktop. Menus and Whisker Menu
// favorites use it, and a user override must be named after it.
QString desktopId(const QString &fileName)
{
    return QDir(QString::fromLatin1(applicationsPath))
        .relativeFilePath(fileName)
        .replace(QLatin1Char('/'), QLatin1Char('-'));
}

QString menuStateFilePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QString::fromLatin1(menuStateFileName));
}

bool isVisibilityLine(const QString &line)
{
    static const QRegularExpression expression(QStringLiteral(R"(^\s*(NoDisplay|Hidden)\s*=)"),
                                                QRegularExpression::CaseInsensitiveOption);
    return expression.match(line).hasMatch();
}

QStringList visibilityLines(const QString &text)
{
    QStringList result;
    bool inDesktopEntry = false;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('['))) {
            inDesktopEntry = trimmed.compare(QStringLiteral("[Desktop Entry]"), Qt::CaseInsensitive) == 0;
        } else if (inDesktopEntry && isVisibilityLine(line)) {
            result.append(line);
        }
    }
    return result;
}

QString replaceVisibilityLines(const QString &text, const QStringList &replacement)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    bool inDesktopEntry = false;
    qsizetype header = -1;
    for (qsizetype index = 0; index < lines.size();) {
        const QString trimmed = lines.at(index).trimmed();
        if (trimmed.startsWith(QLatin1Char('['))) {
            inDesktopEntry = trimmed.compare(QStringLiteral("[Desktop Entry]"), Qt::CaseInsensitive) == 0;
            if (inDesktopEntry) {
                header = index;
            }
            ++index;
        } else if (inDesktopEntry && isVisibilityLine(lines.at(index))) {
            lines.removeAt(index);
        } else {
            ++index;
        }
    }
    if (header < 0) {
        lines.prepend(QStringLiteral("[Desktop Entry]"));
        header = 0;
    }
    for (auto iterator = replacement.crbegin(); iterator != replacement.crend(); ++iterator) {
        lines.insert(header + 1, *iterator);
    }
    return lines.join(QLatin1Char('\n'));
}

QByteArray legacyHiddenDesktopEntry(const QByteArray &original)
{
    // Reproduce the legacy transformation exactly, including its treatment of
    // visibility keys outside [Desktop Entry], to recognize unmodified copies.
    QStringList lines = QString::fromUtf8(original).split(QLatin1Char('\n'));
    lines.removeIf([](const QString &line) {
        return line.startsWith(QStringLiteral("NoDisplay=")) || line.startsWith(QStringLiteral("Hidden="));
    });
    const qsizetype header = lines.indexOf(QStringLiteral("[Desktop Entry]"));
    lines.insert(header >= 0 ? header + 1 : 0, QStringLiteral("NoDisplay=true"));
    return lines.join(QLatin1Char('\n')).toUtf8();
}

bool writeFileAtomically(const QString &path, const QByteArray &content)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

constexpr auto xfconfPanelChannel = QLatin1String("xfce4-panel");
constexpr auto whiskerMenuFavoritesProperty = QLatin1String("favorites");

bool xfconfQueryAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("xfconf-query")).isEmpty();
}

struct XfconfResult {
    bool success = false;
    int exitCode = -1;
    QString output;
    QString errorOutput;
};

XfconfResult runXfconfQuery(const QStringList &arguments)
{
    QProcess process;
    // The output is parsed by message text, which xfconf-query localizes. C.UTF-8 keeps the
    // messages untranslated without mangling non-ASCII values the way plain C would.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral("xfconf-query"), arguments);
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished();
        return {};
    }
    XfconfResult result;
    result.exitCode = process.exitCode();
    result.success = process.exitStatus() == QProcess::NormalExit;
    // Keep stderr apart so GLib/D-Bus warnings are never parsed as values.
    result.output = QString::fromUtf8(process.readAllStandardOutput());
    result.errorOutput = QString::fromUtf8(process.readAllStandardError());
    return result;
}

// Every panel plugin's type by instance number, from one xfconf-query call instead of
// one per plugin, or nullopt if the query failed.
std::optional<QHash<int, QString>> panelPluginTypes()
{
    const XfconfResult result = runXfconfQuery({QStringLiteral("-c"), xfconfPanelChannel, QStringLiteral("-p"),
                                                QStringLiteral("/plugins"), QStringLiteral("-l"),
                                                QStringLiteral("-v")});
    if (!result.success || result.exitCode != 0) {
        return std::nullopt;
    }
    // -lv prints each property padded to a column, then its value.
    static const QRegularExpression pluginPattern(QStringLiteral(R"(^/plugins/plugin-(\d+)\s+(\S+)\s*$)"));
    QHash<int, QString> types;
    for (const QString &line : result.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match = pluginPattern.match(line);
        if (match.hasMatch()) {
            types.insert(match.captured(1).toInt(), match.captured(2));
        }
    }
    return types;
}

QList<int> discoverWhiskerMenuInstances()
{
    const std::optional<QHash<int, QString>> types = panelPluginTypes();
    if (!types) {
        return {};
    }
    QList<int> instances;
    for (auto it = types->cbegin(); it != types->cend(); ++it) {
        if (it.value() == QLatin1String("whiskermenu")) {
            instances.append(it.key());
        }
    }
    std::ranges::sort(instances);
    return instances;
}

std::optional<QStringList> readXfconfArray(int instance, const QString &property)
{
    const QString propertyPath = QStringLiteral("/plugins/plugin-%1/%2").arg(instance).arg(property);
    const XfconfResult result = runXfconfQuery(
        {QStringLiteral("-c"), xfconfPanelChannel, QStringLiteral("-p"), propertyPath});
    if (!result.success) {
        return std::nullopt;
    }
    if (result.exitCode != 0) {
        if (result.errorOutput.contains(QStringLiteral("does not exist"))) {
            return QStringList {};
        }
        return std::nullopt;
    }
    const QStringList lines = result.output.split(QLatin1Char('\n'));
    const auto headerIt = std::ranges::find_if(
        lines, [](const QString &line) { return line.startsWith(QStringLiteral("Value is an array")); });
    if (headerIt == lines.cend()) {
        // Earlier releases wrote a single favorite as a plain string; read it as a one-item array.
        QString scalar = result.output;
        if (scalar.endsWith(QLatin1Char('\n'))) {
            scalar.chop(1);
        }
        if (scalar.isEmpty() || scalar.contains(QLatin1Char('\n'))) {
            return std::nullopt;
        }
        return QStringList {scalar};
    }
    QStringList items;
    for (auto it = std::next(headerIt); it != lines.cend(); ++it) {
        if (!it->isEmpty()) {
            items.append(*it);
        }
    }
    return items;
}

bool writeXfconfArray(int instance, const QString &property, const QStringList &values)
{
    const QString propertyPath = QStringLiteral("/plugins/plugin-%1/%2").arg(instance).arg(property);

    // xfconf-query can't store an empty array, so resetting is the only way to empty it.
    if (values.isEmpty()) {
        const XfconfResult result = runXfconfQuery({QStringLiteral("-c"), xfconfPanelChannel,
                                                    QStringLiteral("-p"), propertyPath, QStringLiteral("-r")});
        return result.success && result.exitCode == 0;
    }
    // Replace the array in a single --set so a failure leaves the old favorites in place.
    // -n is needed by older xfconf-query releases to create a missing property, and -a
    // keeps a single value an array.
    QStringList arguments {QStringLiteral("-c"), xfconfPanelChannel, QStringLiteral("-p"),
                           propertyPath, QStringLiteral("-n"), QStringLiteral("-a")};
    for (const QString &value : values) {
        arguments << QStringLiteral("-t") << QStringLiteral("string") << QStringLiteral("-s") << value;
    }
    const XfconfResult result = runXfconfQuery(arguments);
    return result.success && result.exitCode == 0;
}

bool reconcileOneWhiskerMenuArray(int instance, const QString &property, const QStringList &before,
                                  const QSet<QString> &ourDesktopIds)
{
    const std::optional<QStringList> currentOpt = readXfconfArray(instance, property);
    if (!currentOpt) {
        return false;
    }
    QStringList current = *currentOpt;
    bool changed = false;
    for (int index = 0; index < before.size(); ++index) {
        const QString &id = before.at(index);
        if (!ourDesktopIds.contains(id) || current.contains(id)) {
            continue;
        }
        current.insert(std::min(index, static_cast<int>(current.size())), id);
        changed = true;
    }
    return !changed || writeXfconfArray(instance, property, current);
}

// The per-user menu visibility state and the operations that change it. It works on its
// own copy of the launcher list and collects errors instead of emitting them, so that an
// operation can run on a worker thread.
class MenuVisibility
{
public:
    MenuVisibility() = default;
    explicit MenuVisibility(QStringList menuFiles)
        : m_menuFiles(std::move(menuFiles))
    {
    }

    void detectMenuVisibility();
    void hideNewMenuEntries();
    void setHideFromMenu(bool hide);
    [[nodiscard]] bool hideFromMenu() const
    {
        return m_hideFromMenu;
    }
    [[nodiscard]] const QList<QPair<QString, QString>> &errors() const
    {
        return m_errors;
    }

private:
    void error(const QString &title, const QString &message)
    {
        m_errors.append({title, message});
    }
    [[nodiscard]] static bool hideMenuEntry(QSettings &state, const QDir &directory, const QString &fileName);
    [[nodiscard]] bool hideMenuEntries();
    [[nodiscard]] bool restoreMenuEntries();
    [[nodiscard]] bool restoreLegacyMenuEntries();
    void snapshotWhiskerMenuFavorites(QSettings &state);
    [[nodiscard]] bool reconcileWhiskerMenuFavorites(QSettings &state);

    QStringList m_menuFiles;
    bool m_hideFromMenu = false;
    bool m_legacyMenuState = false;
    QList<QPair<QString, QString>> m_errors;
};
}

// The fallback is a member rather than a static so that it is destroyed with the
// provider, before QApplication, as Qt expects of GUI objects.
ToolIconProvider::ToolIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap),
      m_fallbackIcon(QIcon::hasThemeIcon(QStringLiteral("applications-utilities"))
                         ? QIcon::fromTheme(QStringLiteral("applications-utilities"))
                         : QIcon(QStringLiteral(MX_TOOLS_LOGO_RESOURCE)))
{
}

void ToolIconProvider::insert(const QString &key, const QIcon &icon)
{
    m_icons.insert(key, icon);
}

QPixmap ToolIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QSize target = requestedSize.isEmpty() ? QSize(48, 48) : requestedSize;
    QPixmap pixmap = m_icons.value(id).pixmap(target);
    if (pixmap.isNull()) {
        pixmap = m_fallbackIcon.pixmap(target);
    }
    if (pixmap.isNull()) {
        // Guarantee a valid pixmap even if the bundled fallback icon itself
        // could not be rendered, so the image provider never reports failure.
        pixmap = QPixmap(target);
        pixmap.fill(Qt::transparent);
    }
    if (size != nullptr) {
        *size = pixmap.size();
    }
    return pixmap;
}

ToolModel::ToolModel(ToolIconProvider *iconProvider, QObject *parent)
    : QAbstractListModel(parent),
      m_iconProvider(iconProvider)
{
    loadTools();
    MenuVisibility visibility(m_menuFiles);
    visibility.detectMenuVisibility();
    visibility.hideNewMenuEntries();
    m_hideFromMenu = visibility.hideFromMenu();
    refilter();
}

int ToolModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_visibleRows.size());
}

QVariant ToolModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size()) {
        return {};
    }
    const ToolInfo &tool = m_allTools.at(m_visibleRows.at(index.row()));
    switch (role) {
    case NameRole:
        return tool.name;
    case CommentRole:
        return tool.comment;
    case CategoryRole:
        return tool.category;
    case IconSourceRole:
        return tool.iconSource;
    case FileNameRole:
        return tool.fileName;
    default:
        return {};
    }
}

QHash<int, QByteArray> ToolModel::roleNames() const
{
    return {{NameRole, "name"}, {CommentRole, "comment"}, {CategoryRole, "category"},
            {IconSourceRole, "iconSource"}, {FileNameRole, "fileName"}};
}

QString ToolModel::search() const
{
    return m_search;
}

void ToolModel::setSearch(const QString &search)
{
    if (m_search == search) {
        return;
    }
    m_search = search;
    emit searchChanged();
    refilter();
}

QString ToolModel::selectedCategory() const
{
    return m_selectedCategory;
}

void ToolModel::setSelectedCategory(const QString &category)
{
    if (m_selectedCategory == category) {
        return;
    }
    m_selectedCategory = category;
    emit selectedCategoryChanged();
    refilter();
}

QStringList ToolModel::categories() const
{
    return m_categories;
}

int ToolModel::totalCount() const
{
    return static_cast<int>(m_allTools.size());
}

bool ToolModel::hideFromMenu() const
{
    return m_hideFromMenu;
}

void ToolModel::setHideFromMenu(bool hide)
{
    // The switches are disabled while an operation runs; a change that arrives anyway
    // just re-syncs them, since they flip themselves when clicked.
    if (m_menuBusy) {
        emit hideFromMenuChanged();
        return;
    }
    m_menuBusy = true;
    emit menuBusyChanged();
    // Spawning xfconf-query and rewriting launchers can take a while, so run it on a
    // worker thread with its own copy of the state and apply the outcome here.
    auto *watcher = new QFutureWatcher<MenuVisibility>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const MenuVisibility visibility = watcher->result();
        watcher->deleteLater();
        m_hideFromMenu = visibility.hideFromMenu();
        m_menuBusy = false;
        for (const auto &[title, message] : visibility.errors()) {
            emit errorOccurred(title, message);
        }
        emit menuBusyChanged();
        // Re-sync the switches on every outcome, failures included.
        emit hideFromMenuChanged();
    });
    watcher->setFuture(QtConcurrent::run([menuFiles = m_menuFiles, hide] {
        MenuVisibility visibility(menuFiles);
        visibility.setHideFromMenu(hide);
        return visibility;
    }));
}

bool ToolModel::menuBusy() const
{
    return m_menuBusy;
}

void ToolModel::loadTools()
{
    m_categories = {tr("All tools")};
    const bool live = isLiveEnvironment();
    const QStringList desktops = currentDesktops();
    // Tools grouped by their first MX category, in categoryDefinitions order.
    QVector<QVector<ToolInfo>> toolsByCategory(categoryDefinitions.size());
    QVector<bool> categoryHasTools(categoryDefinitions.size(), false);
    int iconNumber = 0;

    QDirIterator iterator(QString::fromLatin1(applicationsPath), {QStringLiteral("*.desktop")}, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString fileName = iterator.next();
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const DesktopEntry entry = parseDesktopEntry(QString::fromUtf8(file.readAll()));
        const QStringList fileCategories = listValue(entry, QStringLiteral("Categories"));
        QList<qsizetype> memberCategories;
        for (qsizetype index = 0; index < categoryDefinitions.size(); ++index) {
            const QStringList &tokens = categoryDefinitions.at(index).second;
            if (std::ranges::any_of(tokens, [&](const QString &token) { return fileCategories.contains(token); })) {
                memberCategories.append(index);
            }
        }
        if (memberCategories.isEmpty()) {
            continue;
        }
        m_menuFiles.append(fileName);
        if (!visibleInCurrentEnvironment(entry, desktops)
            || (!live && liveOnlyDesktopIds.contains(desktopId(fileName)))) {
            continue;
        }

        ToolInfo tool;
        tool.fileName = fileName;
        const QString englishName = value(entry, QStringLiteral("Name"));
        tool.name = value(entry, translatedKey(entry, QStringLiteral("Name")));
        static const QRegularExpression mxPrefix(QStringLiteral("^MX "));
        tool.name.remove(mxPrefix);
        const QString englishComment = value(entry, QStringLiteral("Comment"));
        tool.comment = value(entry, translatedKey(entry, QStringLiteral("Comment")));
        const QString englishKeywords = listValue(entry, QStringLiteral("Keywords")).join(QLatin1Char(' '));
        tool.keywords = listValue(entry, translatedKey(entry, QStringLiteral("Keywords"))).join(QLatin1Char(' '));
        tool.runInTerminal = value(entry, QStringLiteral("Terminal")).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
        QStringList englishCategories;
        for (const qsizetype index : std::as_const(memberCategories)) {
            englishCategories.append(categoryDefinitions.at(index).first);
            tool.categories.append(translatedCategory(categoryDefinitions.at(index).first));
            categoryHasTools[index] = true;
        }
        tool.category = tool.categories.constFirst();

        // Always keep the English strings searchable alongside the
        // localized ones, so a tool can be found by its English name
        // even when a translation replaces it in the UI.
        tool.searchText = (QStringList {tool.name, englishName, tool.comment, englishComment, tool.keywords,
                                        englishKeywords}
                           + tool.categories + englishCategories)
                              .join(QLatin1Char(' '));

        const QString iconName = value(entry, QStringLiteral("Icon"));
        // %c is the translated Name as written, before the "MX " prefix is dropped for display.
        tool.arguments = execArguments(value(entry, QStringLiteral("Exec")),
                                       value(entry, translatedKey(entry, QStringLiteral("Name"))), iconName, fileName);
        const QString iconKey = QString::number(iconNumber++);
        // A tool without a usable icon gets the provider's fallback when it is drawn.
        m_iconProvider->insert(iconKey, lookupIcon(iconName).value_or(QIcon()));
        tool.iconSource = QStringLiteral("image://toolicons/") + iconKey;
        toolsByCategory[memberCategories.constFirst()].append(tool);
    }

    // Sort as the user's language does, so accented or lower-case names don't land after "Z".
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    for (qsizetype index = 0; index < categoryDefinitions.size(); ++index) {
        QVector<ToolInfo> &categoryTools = toolsByCategory[index];
        std::ranges::sort(categoryTools, [&collator](const ToolInfo &left, const ToolInfo &right) {
            return collator.compare(left.name, right.name) < 0;
        });
        m_allTools.append(categoryTools);
        if (categoryHasTools.at(index)) {
            m_categories.append(translatedCategory(categoryDefinitions.at(index).first));
        }
    }
}

void ToolModel::refilter()
{
    QVector<int> visibleRows;
    const QString allTools = m_categories.value(0);
    // Every word must match somewhere, in any order, so "backup " and "tool backup" work.
    const QStringList terms = m_search.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 0; i < m_allTools.size(); ++i) {
        const ToolInfo &tool = m_allTools.at(i);
        const bool categoryMatches = !terms.isEmpty() || m_selectedCategory.isEmpty()
                                     || m_selectedCategory == allTools
                                     || tool.categories.contains(m_selectedCategory);
        const bool textMatches = std::ranges::all_of(terms, [&tool](const QString &term) {
            return tool.searchText.contains(term, Qt::CaseInsensitive);
        });
        if (categoryMatches && textMatches) {
            visibleRows.append(i);
        }
    }

    // Apply the change as row removals and insertions instead of a reset, so the view
    // keeps its delegates and focus and can animate. Both lists hold ascending indexes
    // into m_allTools, so removing first leaves the old list a subsequence of the new.
    for (qsizetype last = m_visibleRows.size() - 1; last >= 0; --last) {
        if (std::ranges::binary_search(visibleRows, m_visibleRows.at(last))) {
            continue;
        }
        qsizetype first = last;
        while (first > 0 && !std::ranges::binary_search(visibleRows, m_visibleRows.at(first - 1))) {
            --first;
        }
        beginRemoveRows({}, static_cast<int>(first), static_cast<int>(last));
        m_visibleRows.remove(first, last - first + 1);
        endRemoveRows();
        last = first;
    }
    for (qsizetype first = 0; first < visibleRows.size(); ++first) {
        if (first < m_visibleRows.size() && m_visibleRows.at(first) == visibleRows.at(first)) {
            continue;
        }
        qsizetype last = first;
        while (last + 1 < visibleRows.size()
               && !std::ranges::binary_search(m_visibleRows, visibleRows.at(last + 1))) {
            ++last;
        }
        beginInsertRows({}, static_cast<int>(first), static_cast<int>(last));
        for (qsizetype row = first; row <= last; ++row) {
            m_visibleRows.insert(row, visibleRows.at(row));
        }
        endInsertRows();
        first = last;
    }
}

ToolModel::DesktopEntry ToolModel::parseDesktopEntry(const QString &text)
{
    DesktopEntry entry;
    bool inDesktopEntry = false;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (trimmed.startsWith(QLatin1Char('['))) {
            inDesktopEntry = trimmed.compare(QStringLiteral("[Desktop Entry]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        const qsizetype separator = trimmed.indexOf(QLatin1Char('='));
        if (!inDesktopEntry || separator <= 0) {
            continue;
        }
        // The spec forbids duplicate keys; keep the first, as the menus do.
        const QString key = trimmed.left(separator).trimmed();
        if (!entry.contains(key)) {
            entry.insert(key, trimmed.mid(separator + 1).trimmed());
        }
    }
    return entry;
}

QString ToolModel::value(const DesktopEntry &entry, const QString &key)
{
    return unescapeValue(entry.value(key), false).constFirst();
}

QString ToolModel::translatedKey(const DesktopEntry &entry, const QString &key)
{
    const QLocale locale;
    const QStringList localeNames {locale.name(), locale.name().section(QLatin1Char('_'), 0, 0)};
    for (const QString &localeName : localeNames) {
        const QString localizedKey = key + QLatin1Char('[') + localeName + QLatin1Char(']');
        if (!entry.value(localizedKey).isEmpty()) {
            return localizedKey;
        }
    }
    return key;
}

bool ToolModel::visibleInCurrentEnvironment(const DesktopEntry &entry, const QStringList &desktops)
{
    const QStringList onlyShowIn = upperCaseListValue(entry, QStringLiteral("OnlyShowIn"));
    if (!onlyShowIn.isEmpty()
        && std::ranges::none_of(onlyShowIn, [&desktops](const QString &desktop) { return desktops.contains(desktop); })) {
        return false;
    }
    const QStringList notShowIn = upperCaseListValue(entry, QStringLiteral("NotShowIn"));
    return std::ranges::none_of(notShowIn, [&desktops](const QString &desktop) { return desktops.contains(desktop); });
}

std::optional<QIcon> ToolModel::lookupIcon(const QString &iconName)
{
    // Only regular files count: an empty name would otherwise match an icon directory.
    if (iconName.isEmpty()) {
        return std::nullopt;
    }
    if (QFileInfo(iconName).isAbsolute() && QFileInfo(iconName).isFile()) {
        return QIcon(iconName);
    }
    QString name = iconName;
    static const QRegularExpression imageExtension(QStringLiteral(R"(\.(png|svg|xpm)$)"));
    name.remove(imageExtension);
    if (!name.isEmpty() && QIcon::hasThemeIcon(name)) {
        return QIcon::fromTheme(name);
    }
    const QStringList roots {QDir::homePath() + QStringLiteral("/.local/share/icons/"),
                             QStringLiteral("/usr/share/pixmaps/"), QStringLiteral("/usr/local/share/icons/"),
                             QStringLiteral("/usr/share/icons/hicolor/scalable/apps/"),
                             QStringLiteral("/usr/share/icons/hicolor/48x48/apps/"),
                             QStringLiteral("/usr/share/icons/Adwaita/48x48/legacy/")};
    for (const QString &root : roots) {
        for (const QString &extension : {QString(), QStringLiteral(".svg"), QStringLiteral(".png"), QStringLiteral(".xpm")}) {
            const QString candidate = root + (extension.isEmpty() ? iconName : name + extension);
            if (QFileInfo(candidate).isFile()) {
                return QIcon(candidate);
            }
        }
    }
    return std::nullopt;
}

void ToolModel::launch(const QString &fileName)
{
    const auto iterator = std::ranges::find(m_allTools, fileName, &ToolInfo::fileName);
    if (iterator == m_allTools.cend()) {
        emit errorOccurred(tr("Unable to launch tool"), tr("The selected tool is no longer available."));
        return;
    }
    const auto running = m_runningTools.constFind(fileName);
    if (running != m_runningTools.cend() && processStartTime(running->processId) == running->startTime) {
        emit errorOccurred(tr("Tool already running"), tr("%1 is already running.").arg(iterator->name));
        return;
    }
    m_runningTools.remove(fileName);

    if (!iterator->arguments) {
        emit errorOccurred(tr("Unable to launch tool"), tr("Could not start %1.").arg(iterator->name));
        return;
    }
    QStringList arguments = *iterator->arguments;
    if (arguments.isEmpty()) {
        emit errorOccurred(tr("Unable to launch tool"), tr("The selected tool has no launch command."));
        return;
    }
    QString program = arguments.takeFirst();
    if (!arguments.isEmpty() && arguments.constLast() == QLatin1String("&")) {
        arguments.removeLast();
    }
    if (iterator->runInTerminal) {
        arguments.prepend(program);
        program = QStringLiteral("x-terminal-emulator");
        arguments.prepend(QStringLiteral("-e"));
    }
    qint64 processId = 0;
    if (!QProcess::startDetached(program, arguments, {}, &processId)) {
        emit errorOccurred(tr("Unable to launch tool"), tr("Could not start %1.").arg(iterator->name));
    } else if (processId > 0) {
        // A tool that has already exited has no start time and isn't tracked.
        if (const std::optional<quint64> startTime = processStartTime(processId)) {
            m_runningTools.insert(fileName, {processId, *startTime});
        }
    }
}

void ToolModel::openLocalOrReport(const QString &path, ToolModel *model, const QString &title)
{
    if (!QFileInfo::exists(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        emit model->errorOccurred(title, tr("Could not open %1.").arg(path));
    }
}

void ToolModel::openManual()
{
    openLocalOrReport(QString::fromLatin1(manualPath), this, tr("Manual unavailable"));
}

void ToolModel::openLicense()
{
    openLocalOrReport(QString::fromLatin1(licensePath), this, tr("License unavailable"));
}

void ToolModel::openWebsite()
{
    if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://mxlinux.org")))) {
        emit errorOccurred(tr("Website unavailable"), tr("Could not open the MX Linux website."));
    }
}

void ToolModel::openChangelog()
{
    if (!QFileInfo::exists(QString::fromLatin1(changelogPath))) {
        emit errorOccurred(tr("Changelog unavailable"), tr("Could not open %1.").arg(QString::fromLatin1(changelogPath)));
        return;
    }
    // Decompress in the background so a slow disk can't freeze the window; a second
    // click while that runs is ignored.
    if (m_changelogProcess) {
        return;
    }
    m_changelogProcess = new QProcess(this);
    const auto finish = [this](bool success) {
        if (success) {
            emit documentReady(tr("Changelog"), QString::fromUtf8(m_changelogProcess->readAllStandardOutput()));
        } else {
            emit errorOccurred(tr("Changelog unavailable"), tr("Could not read the application changelog."));
        }
        m_changelogProcess->deleteLater();
        m_changelogProcess = nullptr;
    };
    connect(m_changelogProcess, &QProcess::finished, this, [finish](int exitCode, QProcess::ExitStatus status) {
        finish(status == QProcess::NormalExit && exitCode == 0);
    });
    connect(m_changelogProcess, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        // Only a failed start ends without finished().
        if (error == QProcess::FailedToStart) {
            finish(false);
        }
    });
    m_changelogProcess->start(QStringLiteral("zcat"), {QString::fromLatin1(changelogPath)}, QIODevice::ReadOnly);
}

void MenuVisibility::setHideFromMenu(bool hide)
{
    const QString statePath = menuStateFilePath();
    if (!QDir().mkpath(QFileInfo(statePath).absolutePath())) {
        error(ToolModel::tr("Menu setting failed"), ToolModel::tr("Could not create %1.").arg(QFileInfo(statePath).absolutePath()));
        return;
    }
    // Use a separate lock from QSettings' own .lock file, and hold it across
    // the state refresh, desktop-file changes, and any rollback.
    QLockFile operationLock(statePath + QStringLiteral(".operation.lock"));
    operationLock.setStaleLockTime(0);
    if (!operationLock.tryLock()) {
        error(ToolModel::tr("Menu setting failed"), ToolModel::tr("Could not update %1.").arg(statePath));
        return;
    }
    detectMenuVisibility();
    if (m_hideFromMenu == hide) {
        return;
    }
    const bool updated = hide ? hideMenuEntries()
                              : (m_legacyMenuState ? restoreLegacyMenuEntries() : restoreMenuEntries());
    if (!updated) {
        return;
    }
    m_hideFromMenu = hide;
    m_legacyMenuState = false;
}

void MenuVisibility::detectMenuVisibility()
{
    m_hideFromMenu = false;
    m_legacyMenuState = false;
    QSettings state(menuStateFilePath(), QSettings::IniFormat);
    state.sync();
    if (state.value(QStringLiteral("active"), false).toBool()) {
        m_hideFromMenu = true;
        return;
    }

    // Only recognize legacy overrides that can be safely removed. A user's
    // custom NoDisplay=true entry alone is not evidence of a legacy operation.
    const QDir directory(QDir::homePath() + QString::fromLatin1(legacyUserApplicationsPath));
    for (const QString &fileName : std::as_const(m_menuFiles)) {
        // Legacy releases named overrides after the basename, not the desktop ID.
        QFile currentFile(directory.filePath(QFileInfo(fileName).fileName()));
        QFile systemFile(fileName);
        if (!currentFile.open(QIODevice::ReadOnly | QIODevice::Text)
            || !systemFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QByteArray current = currentFile.readAll();
        const QByteArray original = systemFile.readAll();
        if (currentFile.error() == QFileDevice::NoError && systemFile.error() == QFileDevice::NoError
            && current == legacyHiddenDesktopEntry(original)) {
            m_hideFromMenu = true;
            m_legacyMenuState = true;
            return;
        }
    }
}

// Records how to restore one launcher in the state's Entries group, then writes its
// NoDisplay override. The record comes first so a failed write can still be undone,
// and is marked written only once the override is in place.
bool MenuVisibility::hideMenuEntry(QSettings &state, const QDir &directory, const QString &fileName)
{
    const QString id = desktopId(fileName);
    const QString destination = directory.filePath(id);
    const bool originalExists = QFileInfo::exists(destination);
    QFile input(originalExists ? destination : fileName);
    if (!input.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray original = input.readAll();
    const QByteArray hidden = replaceVisibilityLines(QString::fromUtf8(original),
                                                     {QStringLiteral("NoDisplay=true")})
                                  .toUtf8();

    state.beginGroup(id);
    state.setValue(QStringLiteral("path"), destination);
    state.setValue(QStringLiteral("originalExists"), originalExists);
    state.setValue(QStringLiteral("original"), original);
    state.setValue(QStringLiteral("hidden"), hidden);
    state.setValue(QStringLiteral("written"), false);
    state.sync();
    if (state.status() != QSettings::NoError || !writeFileAtomically(destination, hidden)) {
        state.endGroup();
        return false;
    }
    state.setValue(QStringLiteral("written"), true);
    state.endGroup();
    state.sync();
    return state.status() == QSettings::NoError;
}

// While tools are hidden, hide MX launchers installed since then as well, and record
// them so restoring brings them back too. An override that was never written is
// retried on the next start; records from before the written flag count as written.
void MenuVisibility::hideNewMenuEntries()
{
    if (!m_hideFromMenu || m_legacyMenuState) {
        return;
    }
    const QString statePath = menuStateFilePath();
    QLockFile operationLock(statePath + QStringLiteral(".operation.lock"));
    operationLock.setStaleLockTime(0);
    const QDir directory(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation));
    if (!operationLock.tryLock() || !QDir().mkpath(directory.absolutePath())) {
        return;
    }
    QSettings state(statePath, QSettings::IniFormat);
    if (!state.value(QStringLiteral("active"), false).toBool()) {
        return;
    }
    state.beginGroup(QStringLiteral("Entries"));
    const QStringList recorded = state.childGroups();
    for (const QString &fileName : std::as_const(m_menuFiles)) {
        const QString id = desktopId(fileName);
        if (recorded.contains(id)) {
            state.beginGroup(id);
            const bool written = state.value(QStringLiteral("written"), true).toBool();
            QFile current(state.value(QStringLiteral("path")).toString());
            // The override may have been written just before the flag could be saved;
            // recapturing it then would record the hidden file as the user's original.
            const bool overrideInPlace = !written && current.open(QIODevice::ReadOnly)
                                         && current.readAll() == state.value(QStringLiteral("hidden")).toByteArray();
            if (overrideInPlace) {
                state.setValue(QStringLiteral("written"), true);
            }
            state.endGroup();
            if (written || overrideInPlace) {
                continue;
            }
        }
        // A failure leaves the entry marked unwritten, so it is retried next time.
        static_cast<void>(hideMenuEntry(state, directory, fileName));
    }
    state.endGroup();
    state.sync();
}

bool MenuVisibility::hideMenuEntries()
{
    // Menus read user overrides from $XDG_DATA_HOME/applications.
    const QDir directory(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation));
    if (!QDir().mkpath(directory.absolutePath())) {
        error(ToolModel::ToolModel::tr("Menu setting failed"), ToolModel::tr("Could not create %1.").arg(directory.absolutePath()));
        return false;
    }
    const QString statePath = menuStateFilePath();
    if (!QDir().mkpath(QFileInfo(statePath).absolutePath())) {
        error(ToolModel::ToolModel::tr("Menu setting failed"), ToolModel::tr("Could not create %1.").arg(QFileInfo(statePath).absolutePath()));
        return false;
    }

    QSettings state(statePath, QSettings::IniFormat);
    state.clear();
    state.setValue(QStringLiteral("active"), true);
    snapshotWhiskerMenuFavorites(state);
    state.beginGroup(QStringLiteral("Entries"));
    bool success = true;
    for (const QString &fileName : std::as_const(m_menuFiles)) {
        if (!hideMenuEntry(state, directory, fileName)) {
            success = false;
            break;
        }
    }
    state.endGroup();
    state.sync();
    success = state.status() == QSettings::NoError && success;

    if (!success) {
        const bool restored = restoreMenuEntries();
        if (!restored) {
            m_hideFromMenu = true;
        } else {
            error(ToolModel::ToolModel::tr("Menu setting failed"), ToolModel::tr("Could not update %1.").arg(directory.absolutePath()));
        }
        return false;
    }
    return true;
}

bool MenuVisibility::restoreMenuEntries()
{
    QSettings state(menuStateFilePath(), QSettings::IniFormat);
    state.beginGroup(QStringLiteral("Entries"));
    bool success = true;
    for (const QString &id : state.childGroups()) {
        state.beginGroup(id);
        const QString path = state.value(QStringLiteral("path")).toString();
        const bool originalExists = state.value(QStringLiteral("originalExists")).toBool();
        const QByteArray original = state.value(QStringLiteral("original")).toByteArray();
        const QByteArray hidden = state.value(QStringLiteral("hidden")).toByteArray();
        state.endGroup();

        QFile currentFile(path);
        QByteArray current;
        const bool currentExists = currentFile.open(QIODevice::ReadOnly);
        if (currentExists) {
            current = currentFile.readAll();
        }

        if (originalExists) {
            const QByteArray restored = !currentExists || current == hidden
                                            ? original
                                            : replaceVisibilityLines(QString::fromUtf8(current),
                                                                     visibilityLines(QString::fromUtf8(original)))
                                                  .toUtf8();
            success = writeFileAtomically(path, restored) && success;
        } else if (currentExists && current == hidden) {
            success = QFile::remove(path) && success;
        } else if (currentExists) {
            const QByteArray restored = replaceVisibilityLines(QString::fromUtf8(current),
                                                               visibilityLines(QString::fromUtf8(original)))
                                            .toUtf8();
            success = writeFileAtomically(path, restored) && success;
        }
    }
    state.endGroup();
    // Keep the state file, the only snapshot of the favorites, if they could not be restored.
    if (success && reconcileWhiskerMenuFavorites(state)) {
        state.clear();
        state.sync();
        success = state.status() == QSettings::NoError;
    } else {
        success = false;
    }
    if (!success) {
        error(ToolModel::ToolModel::tr("Menu setting failed"), ToolModel::tr("Could not update %1.").arg(menuStateFilePath()));
    }
    return success;
}

bool MenuVisibility::restoreLegacyMenuEntries()
{
    const QDir directory(QDir::homePath() + QString::fromLatin1(legacyUserApplicationsPath));
    bool success = true;
    for (const QString &fileName : std::as_const(m_menuFiles)) {
        // Legacy releases named overrides after the basename, not the desktop ID.
        const QString destination = directory.filePath(QFileInfo(fileName).fileName());
        if (!QFileInfo::exists(destination)) {
            continue;
        }
        QFile currentFile(destination);
        QFile systemFile(fileName);
        if (!currentFile.open(QIODevice::ReadOnly | QIODevice::Text)
            || !systemFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            success = false;
            continue;
        }
        const QByteArray current = currentFile.readAll();
        const QByteArray original = systemFile.readAll();
        if (currentFile.error() != QFileDevice::NoError || systemFile.error() != QFileDevice::NoError) {
            success = false;
            continue;
        }
        currentFile.close();

        // Legacy releases copied the system launcher and applied this exact
        // transformation, without recording ownership or the user's original.
        // Preserve anything else: it may contain user customizations, or have
        // been copied from an older version of the system launcher.
        if (current == legacyHiddenDesktopEntry(original)) {
            success = QFile::remove(destination) && success;
        }
    }
    if (!success) {
        error(ToolModel::ToolModel::tr("Menu setting failed"), ToolModel::tr("Could not update %1.").arg(directory.absolutePath()));
    }
    return success;
}

void MenuVisibility::snapshotWhiskerMenuFavorites(QSettings &state)
{
    if (!xfconfQueryAvailable()) {
        return;
    }
    const QList<int> instances = discoverWhiskerMenuInstances();
    if (instances.isEmpty()) {
        return;
    }
    QStringList instanceStrings;
    for (int instance : instances) {
        instanceStrings.append(QString::number(instance));
    }

    state.beginGroup(QStringLiteral("WhiskerMenu"));
    state.setValue(QStringLiteral("instances"), instanceStrings);
    for (int instance : instances) {
        state.beginGroup(QString::number(instance));
        state.setValue(QStringLiteral("favorites"),
                       readXfconfArray(instance, whiskerMenuFavoritesProperty).value_or(QStringList {}));
        state.endGroup();
    }
    state.endGroup();
}

bool MenuVisibility::reconcileWhiskerMenuFavorites(QSettings &state)
{
    state.beginGroup(QStringLiteral("WhiskerMenu"));
    const QStringList instanceStrings = state.value(QStringLiteral("instances")).toStringList();
    state.endGroup();
    if (instanceStrings.isEmpty()) {
        return true;
    }

    QSet<QString> ourDesktopIds;
    for (const QString &fileName : std::as_const(m_menuFiles)) {
        ourDesktopIds.insert(desktopId(fileName));
    }

    bool success = true;
    std::optional<QHash<int, QString>> pluginTypes;
    bool pluginTypesFetched = false;
    for (const QString &instanceString : instanceStrings) {
        state.beginGroup(QStringLiteral("WhiskerMenu"));
        state.beginGroup(instanceString);
        const QStringList beforeFavorites = state.value(QStringLiteral("favorites")).toStringList();
        state.endGroup();
        state.endGroup();

        // Nothing of ours to put back, so a query failure can't lose anything.
        if (std::ranges::none_of(beforeFavorites, [&](const QString &id) { return ourDesktopIds.contains(id); })) {
            continue;
        }
        // The snapshot outlives this process, so keep it until xfconf-query is back.
        if (!xfconfQueryAvailable()) {
            success = false;
            continue;
        }

        if (!pluginTypesFetched) {
            pluginTypes = panelPluginTypes();
            pluginTypesFetched = true;
            // An unreachable xfconfd can list as an empty channel, and a panel with no
            // plugins at all is no evidence that the Whisker Menu was removed.
            if (pluginTypes && pluginTypes->isEmpty()) {
                pluginTypes.reset();
            }
        }
        if (!pluginTypes) {
            // The query itself failed; keep the snapshot so a later restore can retry.
            success = false;
            continue;
        }
        const int instance = instanceString.toInt();
        // A removed plugin, or one replaced by another plugin type, has no favorites to restore.
        if (pluginTypes->value(instance) != QLatin1String("whiskermenu")) {
            continue;
        }

        success = reconcileOneWhiskerMenuArray(instance, whiskerMenuFavoritesProperty, beforeFavorites,
                                               ourDesktopIds)
                  && success;
    }
    return success;
}
