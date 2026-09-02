/**********************************************************************
 * Copyright (C) 2014-2026 MX Authors
 *
 * This file is part of MX Tools and is licensed under GPL-3.0-or-later.
 **********************************************************************/
#include "toolmodel.h"

#include <algorithm>

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>

namespace
{
constexpr auto applicationsPath = "/usr/share/applications";
constexpr auto userApplicationsPath = "/.local/share/applications";
constexpr auto manualPath = "/usr/share/mx-docs/mxum_en.pdf";
constexpr auto licensePath = "/usr/share/doc/mx-tools/license.html";
constexpr auto changelogPath = "/usr/share/doc/mx-tools/changelog.gz";
constexpr auto menuStateFileName = "menu-visibility.ini";

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
    const QByteArray fileSystem = QStorageInfo(QStringLiteral("/")).fileSystemType();
    return fileSystem == "aufs" || fileSystem == "overlay";
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

QStringList listValue(const QString &text, const QString &key)
{
    const QString prefix = key + QLatin1Char('=');
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.startsWith(prefix, Qt::CaseInsensitive)) {
            QStringList result = line.mid(prefix.size()).split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (QString &item : result) {
                item = item.trimmed().toUpper();
            }
            return result;
        }
    }
    return {};
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
    static const bool available = !QStandardPaths::findExecutable(QStringLiteral("xfconf-query")).isEmpty();
    return available;
}

struct XfconfResult {
    bool success = false;
    int exitCode = -1;
    QString output;
};

XfconfResult runXfconfQuery(const QStringList &arguments)
{
    QProcess process;
    process.start(QStringLiteral("xfconf-query"), arguments);
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished();
        return {};
    }
    XfconfResult result;
    result.exitCode = process.exitCode();
    result.success = process.exitStatus() == QProcess::NormalExit;
    result.output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError());
    return result;
}

QList<int> discoverWhiskerMenuInstances()
{
    const XfconfResult listResult = runXfconfQuery({QStringLiteral("-c"), xfconfPanelChannel,
                                                     QStringLiteral("-p"), QStringLiteral("/plugins"),
                                                     QStringLiteral("-l")});
    if (!listResult.success || listResult.exitCode != 0) {
        return {};
    }
    static const QRegularExpression instancePattern(QStringLiteral(R"(^/plugins/plugin-(\d+)$)"));
    QList<int> instances;
    for (const QString &line : listResult.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch match = instancePattern.match(line.trimmed());
        if (!match.hasMatch()) {
            continue;
        }
        const int instance = match.captured(1).toInt();
        const XfconfResult typeResult = runXfconfQuery(
            {QStringLiteral("-c"), xfconfPanelChannel, QStringLiteral("-p"),
             QStringLiteral("/plugins/plugin-%1").arg(instance)});
        if (typeResult.success && typeResult.exitCode == 0
            && typeResult.output.trimmed() == QStringLiteral("whiskermenu")) {
            instances.append(instance);
        }
    }
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
        if (result.output.contains(QStringLiteral("does not exist"))) {
            return QStringList {};
        }
        return std::nullopt;
    }
    const QStringList lines = result.output.split(QLatin1Char('\n'));
    const auto headerIt = std::ranges::find_if(
        lines, [](const QString &line) { return line.startsWith(QStringLiteral("Value is an array")); });
    if (headerIt == lines.cend()) {
        return std::nullopt;
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

    runXfconfQuery({QStringLiteral("-c"), xfconfPanelChannel, QStringLiteral("-p"), propertyPath,
                    QStringLiteral("-r")});

    if (values.isEmpty()) {
        return true;
    }
    QStringList arguments {QStringLiteral("-c"), xfconfPanelChannel, QStringLiteral("-p"),
                           propertyPath, QStringLiteral("-n")};
    for (const QString &value : values) {
        arguments << QStringLiteral("-t") << QStringLiteral("string") << QStringLiteral("-s") << value;
    }
    const XfconfResult result = runXfconfQuery(arguments);
    return result.success && result.exitCode == 0;
}

void reconcileOneWhiskerMenuArray(int instance, const QString &property, const QStringList &before,
                                  const QSet<QString> &ourDesktopIds)
{
    const std::optional<QStringList> currentOpt = readXfconfArray(instance, property);
    if (!currentOpt) {
        return;
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
    if (changed) {
        writeXfconfArray(instance, property, current);
    }
}
}

ToolIconProvider::ToolIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

void ToolIconProvider::insert(const QString &key, const QIcon &icon)
{
    m_icons.insert(key, icon);
}

QPixmap ToolIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QSize target = requestedSize.isValid() ? requestedSize : QSize(48, 48);
    QPixmap pixmap = m_icons.value(id).pixmap(target);
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
    detectMenuVisibility();
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
    emit hideFromMenuChanged();

    QProcess panelCheck;
    panelCheck.start(QStringLiteral("pgrep"), {QStringLiteral("xfce4-panel")});
    panelCheck.waitForFinished();
    if (panelCheck.exitCode() == 0) {
        QProcess::startDetached(QStringLiteral("xfce4-panel"), {QStringLiteral("--restart")});
    }
}

void ToolModel::loadTools()
{
    m_categories = {tr("All tools")};
    int iconNumber = 0;
    for (const auto &[category, tokens] : categoryDefinitions) {
        const QString categoryName = translatedCategory(category);
        QStringList files = desktopFilesForCategory(tokens);
        m_menuFiles.append(files);
        QVector<ToolInfo> categoryTools;
        for (const QString &fileName : std::as_const(files)) {
            QFile file(fileName);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            const QString text = QString::fromUtf8(file.readAll());
            if (!visibleInCurrentEnvironment(text)) {
                continue;
            }
            if (!isLiveEnvironment()
                && (QFileInfo(fileName).fileName() == QLatin1String("mx-remastercc.desktop")
                    || QFileInfo(fileName).fileName() == QLatin1String("live-kernel-updater.desktop"))) {
                continue;
            }

            ToolInfo tool;
            tool.fileName = fileName;
            const QString englishName = value(text, QStringLiteral("Name"));
            tool.name = translatedValue(text, QStringLiteral("Name"));
            if (tool.name.isEmpty()) {
                tool.name = englishName;
            }
            tool.name.remove(QRegularExpression(QStringLiteral("^MX ")));
            const QString englishComment = value(text, QStringLiteral("Comment"));
            tool.comment = translatedValue(text, QStringLiteral("Comment"));
            if (tool.comment.isEmpty()) {
                tool.comment = englishComment;
            }
            const QString englishKeywords = value(text, QStringLiteral("Keywords"));
            tool.keywords = translatedValue(text, QStringLiteral("Keywords"));
            if (tool.keywords.isEmpty()) {
                tool.keywords = englishKeywords;
            }
            tool.exec = value(text, QStringLiteral("Exec"));
            tool.exec.remove(QRegularExpression(QStringLiteral(R"( %[a-zA-Z])")));
            tool.category = categoryName;
            tool.runInTerminal = value(text, QStringLiteral("Terminal")).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;

            // Always keep the English strings searchable alongside the
            // localized ones, so a tool can be found by its English name
            // even when a translation replaces it in the UI.
            tool.searchText = QStringList {tool.name, englishName, tool.comment, englishComment,
                                           tool.keywords, englishKeywords, tool.category, category}
                                  .join(QLatin1Char(' '));

            const QString iconName = value(text, QStringLiteral("Icon"));
            const QString iconKey = QString::number(iconNumber++);
            m_iconProvider->insert(iconKey, lookupIcon(iconName).value_or(fallbackIcon()));
            tool.iconSource = QStringLiteral("image://toolicons/") + iconKey;
            categoryTools.append(tool);
        }
        std::ranges::sort(categoryTools, {}, &ToolInfo::name);
        if (!categoryTools.isEmpty()) {
            m_categories.append(categoryName);
            m_allTools.append(categoryTools);
        }
    }
    m_menuFiles.removeDuplicates();
}

void ToolModel::refilter()
{
    beginResetModel();
    m_visibleRows.clear();
    const QString allTools = m_categories.value(0);
    for (int i = 0; i < m_allTools.size(); ++i) {
        const ToolInfo &tool = m_allTools.at(i);
        const bool categoryMatches = !m_search.trimmed().isEmpty() || m_selectedCategory.isEmpty()
                                     || m_selectedCategory == allTools
                                     || tool.category == m_selectedCategory;
        const bool textMatches = m_search.trimmed().isEmpty()
                                 || tool.searchText.contains(m_search, Qt::CaseInsensitive);
        if (categoryMatches && textMatches) {
            m_visibleRows.append(i);
        }
    }
    endResetModel();
}

QString ToolModel::value(const QString &text, const QString &key)
{
    const QRegularExpression expression(QStringLiteral("^") + QRegularExpression::escape(key)
                                        + QStringLiteral("=(.*)$"), QRegularExpression::MultilineOption);
    return expression.match(text).captured(1).trimmed();
}

QString ToolModel::translatedValue(const QString &text, const QString &key)
{
    const QLocale locale;
    const QStringList localeNames {locale.name(), locale.name().section(QLatin1Char('_'), 0, 0)};
    for (const QString &localeName : localeNames) {
        const QRegularExpression expression(QStringLiteral("^") + QRegularExpression::escape(key)
                                            + QStringLiteral("\\[") + QRegularExpression::escape(localeName)
                                            + QStringLiteral("\\]=(.*)$"), QRegularExpression::MultilineOption);
        const QString result = expression.match(text).captured(1).trimmed();
        if (!result.isEmpty()) {
            return result;
        }
    }
    return {};
}

QStringList ToolModel::desktopFilesForCategory(const QStringList &tokens)
{
    QStringList matchingFiles;
    QDirIterator iterator(QString::fromLatin1(applicationsPath), {QStringLiteral("*.desktop")}, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString categories = value(QString::fromUtf8(file.readAll()), QStringLiteral("Categories"));
        const QStringList entries = categories.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        if (std::ranges::any_of(tokens, [&entries](const QString &token) { return entries.contains(token); })) {
            matchingFiles.append(path);
        }
    }
    return matchingFiles;
}

bool ToolModel::visibleInCurrentEnvironment(const QString &text)
{
    const bool live = isLiveEnvironment();
    if ((live && text.contains(QStringLiteral("MX-OnlyInstalled"), Qt::CaseInsensitive))
        || (!live && text.contains(QStringLiteral("MX-OnlyLive"), Qt::CaseInsensitive))) {
        return false;
    }
    const QStringList desktops = currentDesktops();
    const QStringList onlyShowIn = listValue(text, QStringLiteral("OnlyShowIn"));
    if (!onlyShowIn.isEmpty()
        && std::ranges::none_of(onlyShowIn, [&desktops](const QString &desktop) { return desktops.contains(desktop); })) {
        return false;
    }
    const QStringList notShowIn = listValue(text, QStringLiteral("NotShowIn"));
    return std::ranges::none_of(notShowIn, [&desktops](const QString &desktop) { return desktops.contains(desktop); });
}

std::optional<QIcon> ToolModel::lookupIcon(const QString &iconName)
{
    if (QFileInfo(iconName).isAbsolute() && QFile::exists(iconName)) {
        return QIcon(iconName);
    }
    QString name = iconName;
    name.remove(QRegularExpression(QStringLiteral(R"(\.(png|svg|xpm)$)")));
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
            if (QFile::exists(candidate)) {
                return QIcon(candidate);
            }
        }
    }
    return std::nullopt;
}

QIcon ToolModel::fallbackIcon()
{
    static const QIcon icon = QIcon::fromTheme(QStringLiteral("applications-utilities"),
                                               QIcon(QStringLiteral(":/qt/qml/MxTools/icons/logo.svg")));
    return icon;
}

void ToolModel::launch(const QString &fileName)
{
    const auto iterator = std::ranges::find(m_allTools, fileName, &ToolInfo::fileName);
    if (iterator == m_allTools.cend()) {
        emit errorOccurred(tr("Unable to launch tool"), tr("The selected tool is no longer available."));
        return;
    }
    const qint64 runningProcessId = m_runningTools.value(fileName);
    if (runningProcessId > 0
        && QFileInfo::exists(QStringLiteral("/proc/%1").arg(runningProcessId))) {
        emit errorOccurred(tr("Tool already running"), tr("%1 is already running.").arg(iterator->name));
        return;
    }
    m_runningTools.remove(fileName);

    QString commandText = iterator->runInTerminal ? QStringLiteral("x-terminal-emulator -e ") + iterator->exec
                                                   : iterator->exec;
    QStringList arguments = QProcess::splitCommand(commandText);
    if (arguments.isEmpty()) {
        emit errorOccurred(tr("Unable to launch tool"), tr("The selected tool has no launch command."));
        return;
    }
    const QString program = arguments.takeFirst();
    if (!arguments.isEmpty() && arguments.constLast() == QLatin1String("&")) {
        arguments.removeLast();
    }
    qint64 processId = 0;
    if (!QProcess::startDetached(program, arguments, {}, &processId)) {
        emit errorOccurred(tr("Unable to launch tool"), tr("Could not start %1.").arg(iterator->name));
    } else if (processId > 0) {
        m_runningTools.insert(fileName, processId);
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
    QProcess process;
    process.start(QStringLiteral("zcat"), {QString::fromLatin1(changelogPath)}, QIODevice::ReadOnly);
    if (!process.waitForFinished() || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        emit errorOccurred(tr("Changelog unavailable"), tr("Could not read the application changelog."));
        return;
    }
    emit documentReady(tr("Changelog"), QString::fromUtf8(process.readAllStandardOutput()));
}

void ToolModel::detectMenuVisibility()
{
    QSettings state(menuStateFilePath(), QSettings::IniFormat);
    if (state.value(QStringLiteral("active"), false).toBool()) {
        m_hideFromMenu = true;
        return;
    }

    // Compatibility with overrides produced by releases that predate the
    // state file. Those releases used mx-user.desktop as their state probe.
    QFile file(QDir::homePath() + QString::fromLatin1(userApplicationsPath) + QStringLiteral("/mx-user.desktop"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_hideFromMenu = QString::fromUtf8(file.readAll()).contains(QStringLiteral("NoDisplay=true"));
        m_legacyMenuState = m_hideFromMenu;
    }
}

bool ToolModel::hideMenuEntries()
{
    const QDir directory(QDir::homePath() + QString::fromLatin1(userApplicationsPath));
    if (!QDir().mkpath(directory.absolutePath())) {
        emit errorOccurred(tr("Menu setting failed"), tr("Could not create %1.").arg(directory.absolutePath()));
        return false;
    }
    const QString statePath = menuStateFilePath();
    if (!QDir().mkpath(QFileInfo(statePath).absolutePath())) {
        emit errorOccurred(tr("Menu setting failed"), tr("Could not create %1.").arg(QFileInfo(statePath).absolutePath()));
        return false;
    }

    QSettings state(statePath, QSettings::IniFormat);
    state.clear();
    state.setValue(QStringLiteral("active"), true);
    snapshotWhiskerMenuFavorites(state);
    state.beginGroup(QStringLiteral("Entries"));
    bool success = true;
    for (const QString &fileName : std::as_const(m_menuFiles)) {
        const QString desktopId = QFileInfo(fileName).fileName();
        const QString destination = directory.filePath(desktopId);
        const bool originalExists = QFileInfo::exists(destination);
        QFile input(originalExists ? destination : fileName);
        if (!input.open(QIODevice::ReadOnly)) {
            success = false;
            break;
        }
        const QByteArray original = input.readAll();
        const QByteArray hidden = replaceVisibilityLines(QString::fromUtf8(original),
                                                         {QStringLiteral("NoDisplay=true")})
                                      .toUtf8();

        state.beginGroup(desktopId);
        state.setValue(QStringLiteral("path"), destination);
        state.setValue(QStringLiteral("originalExists"), originalExists);
        state.setValue(QStringLiteral("original"), original);
        state.setValue(QStringLiteral("hidden"), hidden);
        state.endGroup();
        state.sync();
        if (state.status() != QSettings::NoError || !writeFileAtomically(destination, hidden)) {
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
            emit hideFromMenuChanged();
        } else {
            emit errorOccurred(tr("Menu setting failed"), tr("Could not update %1.").arg(directory.absolutePath()));
        }
        return false;
    }
    return true;
}

bool ToolModel::restoreMenuEntries()
{
    QSettings state(menuStateFilePath(), QSettings::IniFormat);
    state.beginGroup(QStringLiteral("Entries"));
    bool success = true;
    for (const QString &desktopId : state.childGroups()) {
        state.beginGroup(desktopId);
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
    if (success) {
        reconcileWhiskerMenuFavorites(state);
        state.clear();
        state.sync();
        success = state.status() == QSettings::NoError;
    }
    if (!success) {
        emit errorOccurred(tr("Menu setting failed"), tr("Could not update %1.").arg(menuStateFilePath()));
    }
    return success;
}

bool ToolModel::restoreLegacyMenuEntries()
{
    const QDir directory(QDir::homePath() + QString::fromLatin1(userApplicationsPath));
    bool success = true;
    for (const QString &fileName : std::as_const(m_menuFiles)) {
        const QString destination = directory.filePath(QFileInfo(fileName).fileName());
        if (QFileInfo::exists(destination)) {
            success = QFile::remove(destination) && success;
        }
    }
    if (!success) {
        emit errorOccurred(tr("Menu setting failed"), tr("Could not update %1.").arg(directory.absolutePath()));
    }
    return success;
}

void ToolModel::snapshotWhiskerMenuFavorites(QSettings &state)
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

void ToolModel::reconcileWhiskerMenuFavorites(QSettings &state)
{
    if (!xfconfQueryAvailable()) {
        return;
    }
    state.beginGroup(QStringLiteral("WhiskerMenu"));
    const QStringList instanceStrings = state.value(QStringLiteral("instances")).toStringList();
    state.endGroup();
    if (instanceStrings.isEmpty()) {
        return;
    }

    QSet<QString> ourDesktopIds;
    for (const QString &fileName : std::as_const(m_menuFiles)) {
        ourDesktopIds.insert(QFileInfo(fileName).fileName());
    }

    for (const QString &instanceString : instanceStrings) {
        state.beginGroup(QStringLiteral("WhiskerMenu"));
        state.beginGroup(instanceString);
        const QStringList beforeFavorites = state.value(QStringLiteral("favorites")).toStringList();
        state.endGroup();
        state.endGroup();

        const int instance = instanceString.toInt();

        const XfconfResult typeCheck = runXfconfQuery(
            {QStringLiteral("-c"), xfconfPanelChannel, QStringLiteral("-p"),
             QStringLiteral("/plugins/plugin-%1").arg(instance)});
        if (!typeCheck.success || typeCheck.exitCode != 0
            || typeCheck.output.trimmed() != QStringLiteral("whiskermenu")) {
            continue;
        }

        reconcileOneWhiskerMenuArray(instance, whiskerMenuFavoritesProperty, beforeFavorites,
                                     ourDesktopIds);
    }
}
