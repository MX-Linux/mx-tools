/**********************************************************************
 * Copyright (C) 2014 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of MX Tools.
 *
 * MX Tools is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MX Tools is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MX Tools.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFontMetrics>
#include <QFileInfo>
#include <QHash>
#include <QLatin1String>
#include <QProcess>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QSpacerItem>
#include <QStorageInfo>
#include <QTextStream>

#include "about.h"
#include "flatbutton.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"

#ifndef VERSION
    #define VERSION "?.?.?.?"
#endif

namespace
{
QString rootFileSystemType()
{
    return QStorageInfo(QStringLiteral("/")).fileSystemType();
}

bool isLiveEnvironment()
{
    const QString partitionType = rootFileSystemType();
    return partitionType == QLatin1String("aufs") || partitionType == QLatin1String("overlay");
}

QStringList currentDesktopNames()
{
    QStringList desktops = QString::fromUtf8(qgetenv("XDG_CURRENT_DESKTOP")).split(QLatin1Char(':'),
                                                                                    Qt::SkipEmptyParts);
    for (QString &desktop : desktops) {
        desktop = desktop.trimmed().toUpper();
    }
    return desktops;
}

QStringList desktopEntryListValue(const QString &fileContent, const QString &key)
{
    const QString valuePattern = key + QLatin1Char('=');
    const QStringList lines = fileContent.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!line.startsWith(valuePattern, Qt::CaseInsensitive)) {
            continue;
        }

        QStringList values = line.mid(valuePattern.size()).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (QString &value : values) {
            value = value.trimmed().toUpper();
        }
        return values;
    }

    return {};
}

bool isVisibleOnCurrentDesktop(const QString &fileContent, const QStringList &desktops)
{
    const QStringList onlyShowIn = desktopEntryListValue(fileContent, QStringLiteral("OnlyShowIn"));
    if (!onlyShowIn.isEmpty()) {
        const bool matchesAllowedDesktop = std::ranges::any_of(onlyShowIn, [&desktops](const QString &desktop) {
            return desktops.contains(desktop);
        });
        if (!matchesAllowedDesktop) {
            return false;
        }
    }

    const QStringList notShowIn = desktopEntryListValue(fileContent, QStringLiteral("NotShowIn"));
    const bool matchesBlockedDesktop = std::ranges::any_of(notShowIn, [&desktops](const QString &desktop) {
        return desktops.contains(desktop);
    });
    return !matchesBlockedDesktop;
}

bool isVisibleInCurrentEnvironment(const QString &fileContent, bool live)
{
    if (live) {
        return !fileContent.contains(QStringLiteral("MX-OnlyInstalled"), Qt::CaseInsensitive);
    }
    return !fileContent.contains(QStringLiteral("MX-OnlyLive"), Qt::CaseInsensitive);
}
}

MainWindow::MainWindow(QWidget *parent)
    : QDialog(parent),
      ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setConnections();
    setWindowFlags(Qt::Window); // Enables the close, min, and max buttons
    checkHideToolsInMenu();
    initializeCategoryLists();
    populateCategoryMap(&menu_category_map);
    filterLiveEnvironmentItems();
    filterDesktopEnvironmentItems();
    populateCategoryMap(&category_map);
    readInfo(category_map);
    iconSize = settings.value("icon_size", iconSize).toInt();
    updateLayoutMetrics();
    addButtons(info_map);
    ui->textSearch->setFocus();
    restoreWindowGeometry();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setConnections()
{
    connect(ui->checkHide, &QCheckBox::clicked, this, &MainWindow::checkHide_clicked);
    connect(ui->pushAbout, &QPushButton::clicked, this, &MainWindow::pushAbout_clicked);
    connect(ui->pushCancel, &QPushButton::clicked, this, &MainWindow::close);
    connect(ui->pushHelp, &QPushButton::clicked, this, &MainWindow::pushHelp_clicked);
    connect(ui->textSearch, &QLineEdit::textChanged, this, &MainWindow::textSearch_textChanged);
}

void MainWindow::checkHideToolsInMenu()
{
    const QDir userApplicationsDir(QDir::homePath() + USER_APPLICATIONS_PATH);
    const QString userDesktopFilePath = userApplicationsDir.filePath("mx-user.desktop");
    QFile userDesktopFile(userDesktopFilePath);
    if (!userDesktopFile.exists()) {
        ui->checkHide->setChecked(false);
        return;
    }

    if (userDesktopFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui->checkHide->setChecked(userDesktopFile.readAll().contains("NoDisplay=true"));
        userDesktopFile.close();
    } else {
        ui->checkHide->setChecked(false);
    }
}

void MainWindow::initializeCategoryLists()
{
    for (auto it = categories.cbegin(); it != categories.cend(); ++it) {
        *(it.value()) = listDesktopFiles(it.key(), APPLICATIONS_PATH);
    }
}

void MainWindow::filterLiveEnvironmentItems()
{
    if (!isLiveEnvironment()) {
        QStringList itemsToRemove {"mx-remastercc.desktop", "live-kernel-updater.desktop"};
        live_list.erase(std::remove_if(live_list.begin(), live_list.end(),
                                       [&itemsToRemove](const QString &item) {
                                           return item.contains(itemsToRemove.at(0))
                                                  || item.contains(itemsToRemove.at(1));
                                       }),
                        live_list.end());
    }
}

void MainWindow::filterDesktopEnvironmentItems()
{
    const bool live = isLiveEnvironment();
    const QStringList desktops = currentDesktopNames();
    for (auto it = categories.begin(); it != categories.end(); ++it) {
        removeEnvExclusive(it.value(), live, desktops);
    }
}

void MainWindow::populateCategoryMap(CategoryFileMap *categoryMap)
{
    categoryMap->clear();
    for (auto it = categories.cbegin(); it != categories.cend(); ++it) {
        categoryMap->insert(it.key(), *(it.value()));
    }
}

void MainWindow::restoreWindowGeometry()
{
    auto size = this->size();
    restoreGeometry(settings.value("geometry").toByteArray());
    if (isMaximized()) { // if started maximized give option to resize to normal window size
        resize(size);
        auto screenGeometry = QApplication::primaryScreen()->geometry();
        move((screenGeometry.width() - width()) / 2, (screenGeometry.height() - height()) / 2);
    }
}

// List .desktop files whose Categories= line contains the given category token.
QStringList MainWindow::listDesktopFiles(const QString &category, const QString &location)
{
    QDirIterator it(location, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
    QStringList matchingFiles;

    while (it.hasNext()) {
        const QString filePath = it.next();
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream stream(&file);
        while (!stream.atEnd()) {
            const QString line = stream.readLine();
            if (!line.startsWith(QLatin1String("Categories="))) {
                continue;
            }
            // Match the category as an exact ;-separated token, not a loose substring,
            // so it can only come from Categories= and won't partial-match other tokens.
            const QStringList categories =
                line.mid(line.indexOf(QLatin1Char('=')) + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
            if (categories.contains(category)) {
                matchingFiles << filePath;
            }
            break; // only one Categories line per desktop file
        }
    }
    return matchingFiles;
}

// Recompute layout metrics derived from the full info_map: the largest category size
// (maxElements) and the widest button (cachedMaxButtonWidth). These only change when
// info_map or the application font changes, so this is called at init and on font change
// rather than on every relayout.
void MainWindow::updateLayoutMetrics()
{
    constexpr int buttonPadding = 16;   // left/right padding inside button
    constexpr int iconTextSpacing = 8;  // gap between icon and text

    maxElements = 0;
    int widestText = 0;
    const QFontMetrics fm(QApplication::font());
    for (const auto &categoryTools : std::as_const(info_map)) {
        maxElements = std::max(maxElements, static_cast<int>(categoryTools.size()));
        for (const auto &toolInfo : categoryTools) {
            widestText = std::max(widestText, fm.horizontalAdvance(toolInfo.name));
        }
    }
    cachedMaxButtonWidth = widestText + iconSize + iconTextSpacing + buttonPadding;
}

// Number of button columns that fit in the current window width.
int MainWindow::columnsForWidth() const
{
    const int buttonWidth = cachedMaxButtonWidth > 0 ? cachedMaxButtonWidth : 200;
    return std::max(1, width() / buttonWidth);
}

// Load info (name, comment, exec, iconName, category, terminal) to the info_map
void MainWindow::readInfo(const CategoryFileMap &categoryMap)
{
    const QString lang = QLocale().name().split(QStringLiteral("_")).first();
    const QString langRegion = QLocale().name();

    // Pre-allocate strings for common keys
    const QString nameKey = QStringLiteral("Name");
    const QString commentKey = QStringLiteral("Comment");
    const QString execKey = QStringLiteral("Exec");
    const QString iconKey = QStringLiteral("Icon");
    const QString terminalKey = QStringLiteral("Terminal");
    const QString enLang = QStringLiteral("en");

    for (auto it = categoryMap.cbegin(); it != categoryMap.cend(); ++it) {
        const QString category = it.key();
        const QStringList &fileList = it.value();

        QVector<ToolInfo> categoryTools;
        categoryTools.reserve(fileList.size());

        for (const QString &fileName : fileList) {
            QFile file(fileName);
            if (!file.open(QFile::Text | QFile::ReadOnly)) {
                continue;
            }
            QTextStream stream(&file);
            QString text = stream.readAll();
            file.close();

            QString name = lang != enLang ? getTranslation(text, nameKey, langRegion, lang) : QString();
            QString comment = lang != enLang ? getTranslation(text, commentKey, langRegion, lang) : QString();

            if (name.isEmpty()) {
                name = getValueFromText(text, nameKey);
            }
            // Apply display fixups to both translated and English names: drop the
            // redundant "MX " prefix and escape "&" so it isn't treated as a button mnemonic.
            static const QRegularExpression mxPrefixRegex(QStringLiteral("^MX "));
            name = name.remove(mxPrefixRegex).replace(QStringLiteral("&"), QStringLiteral("&&"));

            comment = comment.isEmpty() ? getValueFromText(text, commentKey) : comment;

            QString exec = getValueFromText(text, execKey);
            fixExecItem(&exec);

            ToolInfo toolInfo;
            toolInfo.fileName = fileName;
            toolInfo.name = name;
            toolInfo.comment = comment;
            toolInfo.iconName = getValueFromText(text, iconKey);
            toolInfo.exec = exec;
            toolInfo.category = category;
            toolInfo.runInTerminal = getValueFromText(text, terminalKey).compare("true", Qt::CaseInsensitive) == 0;
            categoryTools.append(toolInfo);
        }
        info_map.insert(category, categoryTools);
    }
}

QString MainWindow::getTranslation(const QString &text, const QString &key, const QString &langRegion,
                                   const QString &lang)
{
    static QHash<QString, QRegularExpression> regexCache;

    // First try to find translation for specific region (e.g., en_US)
    const QString regionPattern = key + QStringLiteral("[") + langRegion + QStringLiteral("]");
    auto it = regexCache.find(regionPattern);
    if (it == regexCache.end()) {
        QString pattern = QStringLiteral("^") + key + QStringLiteral("\\[") + langRegion + QStringLiteral("\\]=(.*)$");
        QRegularExpression re(pattern);
        re.setPatternOptions(QRegularExpression::MultilineOption);
        it = regexCache.insert(regionPattern, re);
    }

    QRegularExpressionMatch match = it->match(text);
    if (match.hasMatch()) {
        QString translation = match.captured(1).trimmed();
        if (!translation.isEmpty()) {
            return translation;
        }
    }

    // Fall back to general language (e.g., en)
    const QString langPattern = key + QStringLiteral("[") + lang + QStringLiteral("]");
    it = regexCache.find(langPattern);
    if (it == regexCache.end()) {
        QString pattern = QStringLiteral("^") + key + QStringLiteral("\\[") + lang + QStringLiteral("\\]=(.*)$");
        QRegularExpression re(pattern);
        re.setPatternOptions(QRegularExpression::MultilineOption);
        it = regexCache.insert(langPattern, re);
    }

    match = it->match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QString MainWindow::getValueFromText(const QString &text, const QString &key)
{
    static QHash<QString, QRegularExpression> regexCache;

    auto it = regexCache.find(key);
    if (it == regexCache.end()) {
        QString pattern = QStringLiteral("^") + key + QStringLiteral("=(.*)$");
        QRegularExpression re(pattern);
        re.setPatternOptions(QRegularExpression::MultilineOption);
        it = regexCache.insert(key, re);
    }

    return it->match(text).captured(1).trimmed();
}

// Read the info_map and add the buttons to the UI
void MainWindow::addButtons(const CategoryToolsMap &infoMap)
{
    clearGrid();

    const int max_columns = columnsForWidth();
    int row = 0;
    int actualMaxCol = 0;
    bool addedWidgets = false;

    // Add buttons for each category
    for (auto it = infoMap.cbegin(); it != infoMap.cend(); ++it) {
        const QString &category = it.key();
        const auto &categoryTools = it.value();

        if (categoryTools.isEmpty()) {
            continue;
        }

        if (row > 0) {
            addCategorySeparator(row, max_columns);
        }

        addCategoryHeader(category, row, max_columns);
        addedWidgets = true;

        // Add buttons for this category
        int col = 0;
        for (const auto &toolInfo : categoryTools) {
            auto *btn = createButton(toolInfo);
            ui->gridLayout_btn->addWidget(btn, row, col);
            actualMaxCol = std::max(actualMaxCol, col + 1);
            addedWidgets = true;

            // Move to the next row if more items than max columns
            if (++col >= max_columns) {
                col = 0;
                ++row;
            }
        }
    }

    colCount = actualMaxCol;
    if (addedWidgets) {
        auto *spacer = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        ui->gridLayout_btn->addItem(spacer, row + 1, 0, 1, std::max(1, max_columns));
    }
}

void MainWindow::addCategorySeparator(int &row, int max_columns)
{
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    ui->gridLayout_btn->addWidget(line, ++row, 0, 1, max_columns);
}

void MainWindow::addCategoryHeader(const QString &category, int &row, int max_columns)
{
    auto *label = new QLabel(category, this);
    static const QRegularExpression mxPrefixRegex(QStringLiteral("^MX-"));
    label->setText(label->text().remove(mxPrefixRegex));
    QFont font;
    font.setBold(true);
    font.setUnderline(true);
    label->setFont(font);
    ui->gridLayout_btn->addWidget(label, ++row, 0, 1, max_columns);
    ++row;
}

FlatButton *MainWindow::createButton(const ToolInfo &toolInfo)
{
    auto *btn = new FlatButton(toolInfo.name);
    btn->setToolTip(toolInfo.comment);
    btn->setAutoDefault(false);
    const QIcon icon = findIcon(toolInfo.iconName);
    btn->setIcon(icon);
    btn->setIconSize({iconSize, iconSize});

    // Configure button command
    const QString &exec = toolInfo.exec;
    const bool runInTerminal = toolInfo.runInTerminal;
    btn->setProperty("command", runInTerminal ? "x-terminal-emulator -e " + exec : exec);
    connect(btn, &FlatButton::clicked, this, &MainWindow::btn_clicked);

    return btn;
}

// Locate an icon by name without any default fallback. Returns nullopt if nothing matched.
std::optional<QIcon> MainWindow::lookupIcon(const QString &iconName)
{
    // Absolute path to an existing file.
    if (QFileInfo(iconName).isAbsolute() && QFile::exists(iconName)) {
        return QIcon(iconName);
    }

    static const QRegularExpression extRe(R"(\.(png|svg|xpm)$)");
    QString nameNoExt = iconName;
    nameNoExt.remove(extRe);

    // Themed icon.
    if (QIcon::hasThemeIcon(nameNoExt)) {
        return QIcon::fromTheme(nameNoExt);
    }

    const QStringList searchPaths {QDir::homePath() + HOME_SHARE_ICONS_PATH,
                                   PIXMAPS_PATH,
                                   LOCAL_SHARE_ICONS_PATH,
                                   SHARE_ICONS_PATH,
                                   HICOLOR_SCALABLE_PATH,
                                   HICOLOR_48_PATH,
                                   ADWAITA_PATH};

    // First try the full iconName (with its extension, if any) under each path.
    auto it = std::ranges::find_if(searchPaths, [&](const QString &path) { return QFile::exists(path + iconName); });
    if (it != searchPaths.cend()) {
        return QIcon(*it + iconName);
    }

    // Then try the extensionless name with each known extension.
    for (const QString &path : searchPaths) {
        if (!QFile::exists(path)) {
            continue;
        }
        for (const auto &ext : {QStringLiteral(".png"), QStringLiteral(".svg"), QStringLiteral(".xpm")}) {
            const QString file = path + nameNoExt + ext;
            if (QFile::exists(file)) {
                return QIcon(file);
            }
        }
    }

    return std::nullopt;
}

// The shared fallback icon, resolved once. Empty QIcon if even the default cannot be found.
QIcon MainWindow::defaultIcon()
{
    static const QIcon icon = lookupIcon(DEFAULT_ICON_NAME).value_or(QIcon());
    return icon;
}

QIcon MainWindow::findIcon(const QString &iconName)
{
    static QHash<QString, QIcon> iconCache;

    if (iconName.isEmpty()) {
        return defaultIcon();
    }

    auto cacheIt = iconCache.find(iconName);
    if (cacheIt != iconCache.end()) {
        return *cacheIt;
    }

    const QIcon icon = lookupIcon(iconName).value_or(defaultIcon());
    iconCache.insert(iconName, icon);
    return icon;
}

void MainWindow::btn_clicked()
{
    hide();
    
    // Get sender and check for null pointer
    auto *senderObj = sender();
    if (!senderObj) {
        qWarning() << "btn_clicked called with null sender";
        show();
        return;
    }
    
    const QString commandString = senderObj->property("command").toString();
    QStringList cmdList = QProcess::splitCommand(commandString);
    if (cmdList.isEmpty()) {
        qWarning() << "Empty command list";
        show();
        return;
    }

    QString command = cmdList.takeFirst();
    
    // Check if list has elements before accessing last()
    if (!cmdList.isEmpty() && cmdList.last() == "&") {
        cmdList.removeLast();
        if (!QProcess::startDetached(command, cmdList)) {
            qWarning() << "Failed to start detached process:" << command << cmdList;
        }
    } else {
        int exitCode = QProcess::execute(command, cmdList);
        if (exitCode != 0) {
            qWarning() << "Process exited with code:" << exitCode << "Command:" << command << cmdList;
        }
        // Only -2 from QProcess::execute means the process could not be started.
        // -1 (crash) and any positive exit code are the tool's own outcome and
        // should not be reported as a launch failure.
        if (exitCode == -2) {
            QMessageBox::critical(this, tr("Error"),
                tr("Failed to launch command") + "\n" +
                tr("Command: %1").arg(command + " " + cmdList.join(" ")));
        }
    }
    show();
}

void MainWindow::closeEvent(QCloseEvent * /*unused*/)
{
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    if (event->oldSize().width() == event->size().width()) {
        return;
    }

    const int newColCount = columnsForWidth();

    // Early exit: column count unchanged, or already wide enough to fit the largest
    // category so extra width cannot add columns. maxElements always reflects the full
    // info_map (set in updateLayoutMetrics), so this stays correct during an active search.
    if (newColCount == colCount || (newColCount >= maxElements && colCount >= maxElements)) {
        return;
    }
    colCount = newColCount;

    if (ui->textSearch->text().isEmpty()) {
        addButtons(info_map);
    } else {
        textSearch_textChanged(ui->textSearch->text());
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    // Button widths depend on the application font, so recompute metrics and relayout.
    if (event->type() == QEvent::FontChange) {
        updateLayoutMetrics();
        colCount = 0; // force addButtons to relayout
        const QString search = ui->textSearch->text();
        if (search.isEmpty()) {
            addButtons(info_map);
        } else {
            textSearch_textChanged(search);
        }
    }
    QDialog::changeEvent(event);
}

// Hide icons in menu checkbox
void MainWindow::checkHide_clicked(bool checked)
{
    QStringList menuFiles;
    for (const QStringList &list : std::as_const(menu_category_map)) {
        menuFiles.append(list);
    }
    menuFiles.removeDuplicates();

    for (const QString &fileName : std::as_const(menuFiles)) {
        hideShowIcon(fileName, checked);
    }

    QProcess process;
    process.start("pgrep", {"xfce4-panel"});
    process.waitForFinished();
    if (process.exitCode() == 0) {
        QProcess::execute("xfce4-panel", {"--restart"});
    }
}

// Hide or show icon for .desktop file
void MainWindow::hideShowIcon(const QString &fileName, bool hide)
{
    QFileInfo fileInfo(fileName);
    const QDir userApplicationsDir(QDir::homePath() + USER_APPLICATIONS_PATH);
    if (!userApplicationsDir.exists() && !QDir().mkpath(userApplicationsDir.absolutePath())) {
        qWarning() << "Failed to create directory:" << userApplicationsDir.absolutePath();
        return;
    }

    const QString fileNameLocal = userApplicationsDir.filePath(fileInfo.fileName());
    if (!hide) {
        QFile::remove(fileNameLocal);
    } else {
        // QFile::copy won't overwrite an existing destination, so clear it first.
        QFile::remove(fileNameLocal);
        if (!QFile::copy(fileName, fileNameLocal)) {
            qWarning() << "Failed to copy file:" << fileName << "to" << fileNameLocal;
            return;
        }

        // Read and modify the file content using Qt APIs
        QFile localFile(fileNameLocal);
        if (!localFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Failed to open file for reading:" << fileNameLocal;
            return;
        }

        QTextStream in(&localFile);
        QStringList lines;
        bool noDisplayAdded = false;

        // Process each line
        while (!in.atEnd()) {
            QString line = in.readLine();

            // Skip NoDisplay and Hidden lines
            if (line.startsWith(QLatin1String("NoDisplay=")) || line.startsWith(QLatin1String("Hidden="))) {
                continue;
            }

            lines << line;

            // Add NoDisplay=true inside the main group, right after its header. This is more
            // robust than keying off Exec= (which may be absent or appear in action groups).
            if (!noDisplayAdded && line.startsWith(QLatin1String("[Desktop Entry]"))) {
                lines << QLatin1String("NoDisplay=true");
                noDisplayAdded = true;
            }
        }
        localFile.close();

        // Fallback for files without a recognizable [Desktop Entry] header.
        if (!noDisplayAdded) {
            lines << QLatin1String("NoDisplay=true");
        }

        // Write the modified content back
        if (!localFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            qWarning() << "Failed to open file for writing:" << fileNameLocal;
            return;
        }

        QTextStream out(&localFile);
        for (const QString &line : lines) {
            out << line << Qt::endl;
        }
    }
}

void MainWindow::pushAbout_clicked()
{
    displayAboutMsgBox(
        tr("About MX Tools"),
        "<p align=\"center\"><b><h2>" + tr("MX Tools") + "</h2></b></p><p align=\"center\">" + tr("Version: ") + VERSION
            + "</p><p align=\"center\"><h3>" + tr("Configuration Tools for MX Linux")
            + "</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p>"
              "<p align=\"center\">"
            + tr("Copyright (c) MX Linux") + "<br /><br /></p>",
        LICENSE_PATH, tr("%1 License").arg(windowTitle()));
}

void MainWindow::pushHelp_clicked()
{
    if (!QFileInfo::exists(HELP_DOC_PATH)) {
        qWarning() << "Help document not found:" << HELP_DOC_PATH;
        return;
    }

    if (!QProcess::startDetached("xdg-open", {HELP_DOC_PATH})) {
        qWarning() << "Failed to open help PDF:" << HELP_DOC_PATH;
    }
}

void MainWindow::textSearch_textChanged(const QString &searchTerm)
{
    if (searchTerm.isEmpty()) {
        addButtons(info_map);
        return;
    }

    CategoryToolsMap filteredMap;

    // Iterate over categories in info_map
    for (auto it = info_map.constBegin(); it != info_map.constEnd(); ++it) {
        const auto &category = it.key();
        const auto &categoryTools = it.value();
        QVector<ToolInfo> filteredCategoryTools;

        for (const auto &toolInfo : categoryTools) {
            if (toolInfo.name.contains(searchTerm, Qt::CaseInsensitive)
                || toolInfo.comment.contains(searchTerm, Qt::CaseInsensitive)
                || category.contains(searchTerm, Qt::CaseInsensitive)) {
                filteredCategoryTools.append(toolInfo);
            }
        }

        if (!filteredCategoryTools.isEmpty()) {
            filteredMap.insert(category, filteredCategoryTools);
        }
    }

    addButtons(filteredMap);
}

// Strip %f, %F, %U, etc. if exec expects a file name since it's called without an argument from this launcher.
void MainWindow::fixExecItem(QString *item)
{
    item->remove(QRegularExpression(R"( %[a-zA-Z])"));
}

// Remove programs hidden for the current live/install state or desktop environment.
void MainWindow::removeEnvExclusive(QStringList *list, bool live, const QStringList &desktops)
{
    for (auto it = list->begin(); it != list->end();) {
        const QString &filePath = *it;
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString fileContent = QString::fromUtf8(file.readAll());
            file.close();

            const bool keepItem = isVisibleInCurrentEnvironment(fileContent, live)
                                  && isVisibleOnCurrentDesktop(fileContent, desktops);
            keepItem ? ++it : it = list->erase(it);
        } else {
            qWarning() << "Could not open file:" << filePath;
            ++it;
        }
    }
}

// Remove all items from the layout
void MainWindow::clearGrid()
{
    for (int row = 0; row < ui->gridLayout_btn->rowCount(); ++row) {
        ui->gridLayout_btn->setRowStretch(row, 0);
    }
    for (int col = 0; col < ui->gridLayout_btn->columnCount(); ++col) {
        ui->gridLayout_btn->setColumnStretch(col, 0);
    }

    QLayoutItem *child;
    while ((child = ui->gridLayout_btn->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
}
