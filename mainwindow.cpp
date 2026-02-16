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
#include <QHash>
#include <QLatin1String>
#include <QProcess>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QSpacerItem>
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
    QFile mountsFile(QStringLiteral("/proc/mounts"));
    if (!mountsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QTextStream stream(&mountsFile);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 3) {
            continue;
        }
        if (fields.at(1) == QLatin1String("/")) {
            return fields.at(2);
        }
    }

    return {};
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
    filterLiveEnvironmentItems();
    filterDesktopEnvironmentItems();
    populateCategoryMap();
    readInfo(category_map);
    addButtons(info_map);
    ui->textSearch->setFocus();
    restoreWindowGeometry();
    iconSize = settings.value("icon_size", iconSize).toInt();
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
    const QString partitionType = rootFileSystemType();
    const bool live = partitionType == QLatin1String("aufs") || partitionType == QLatin1String("overlay");

    if (!live) {
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
    QStringList termsToRemove {qgetenv("XDG_CURRENT_DESKTOP") == "live" ? "MX-OnlyInstalled" : "MX-OnlyLive"};
    const QMap<QString, QString> desktopTerms {
        {"XFCE", "OnlyShowIn=XFCE"}, {"Fluxbox", "OnlyShowIn=FLUXBOX"}, {"KDE", "OnlyShowIn=KDE"}};

    for (const auto &[desktop, term] : desktopTerms.asKeyValueRange()) {
        if (qgetenv("XDG_CURRENT_DESKTOP") != desktop) {
            termsToRemove << term;
        }
    }
    for (auto it = categories.begin(); it != categories.end(); ++it) {
        removeEnvExclusive(it.value(), termsToRemove);
    }
}

void MainWindow::populateCategoryMap()
{
    for (auto it = categories.cbegin(); it != categories.cend(); ++it) {
        category_map.insert(it.key(), *(it.value()));
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

// List .desktop files that contain a specific string
QStringList MainWindow::listDesktopFiles(const QString &searchString, const QString &location)
{
    QDirIterator it(location, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
    QStringList matchingFiles;
    matchingFiles.reserve(200);

    while (it.hasNext()) {
        const QString filePath = it.next();
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream stream(&file);
        while (!stream.atEnd()) {
            if (stream.readLine().contains(searchString)) {
                matchingFiles << filePath;
                break;
            }
        }
    }
    return matchingFiles;
}

int MainWindow::calculateMaxElements(const CategoryToolsMap &infoMap)
{
    maxElements = 0;
    // Find maximum number of elements across all categories
    for (const auto &categoryTools : infoMap) {
        maxElements = std::max(maxElements, static_cast<int>(categoryTools.size()));
    }

    // Only recalculate button width if not cached (expensive operation)
    if (cachedMaxButtonWidth == 0) {
        const QFontMetrics fm(QApplication::font());
        constexpr int buttonPadding = 16; // Left/right padding inside button

        // Check ALL categories for the widest button text
        for (const auto &categoryTools : infoMap) {
            for (const auto &toolInfo : categoryTools) {
                const int textWidth = fm.horizontalAdvance(toolInfo.name);
                cachedMaxButtonWidth = std::max(cachedMaxButtonWidth, textWidth);
            }
        }
        // Add icon size, spacing between icon and text, and button padding
        cachedMaxButtonWidth += iconSize + 8 + buttonPadding;
    }

    // Calculate maximum columns that fit in window width
    return std::max(1, width() / cachedMaxButtonWidth);
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

            name = name.isEmpty() ? getValueFromText(text, nameKey)
                                        .remove(QRegularExpression(QStringLiteral("^MX ")))
                                        .replace(QStringLiteral("&"), QStringLiteral("&&"))
                                  : name;
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

    const int max_columns = calculateMaxElements(infoMap);
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
    label->setText(label->text().remove(QRegularExpression("^MX-")));
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

QIcon MainWindow::findIcon(const QString &iconName)
{
    static QHash<QString, QIcon> iconCache;
    static QIcon defaultIcon;
    static bool defaultIconLoaded = false;
    static thread_local bool resolvingDefault = false;

    if (iconName.isEmpty()) {
        if (!defaultIconLoaded) {
            resolvingDefault = true;
            defaultIcon = findIcon(DEFAULT_ICON_NAME);
            defaultIconLoaded = true;
            resolvingDefault = false;
        }
        return defaultIcon;
    }

    // Check cache first
    auto cacheIt = iconCache.find(iconName);
    if (cacheIt != iconCache.end()) {
        return *cacheIt;
    }

    // Check if the icon name is an absolute path and exists
    if (QFileInfo(iconName).isAbsolute() && QFile::exists(iconName)) {
        QIcon icon(iconName);
        iconCache.insert(iconName, icon);
        return icon;
    }

    // Prepare regular expression to strip extension
    static const QRegularExpression re(R"(\.(png|svg|xpm)$)");
    QString nameNoExt = iconName;
    nameNoExt.remove(re);

    // Return the themed icon if available
    if (QIcon::hasThemeIcon(nameNoExt)) {
        QIcon icon = QIcon::fromTheme(nameNoExt);
        iconCache.insert(iconName, icon);
        return icon;
    }

    // Define common search paths for icons
    QStringList searchPaths {QDir::homePath() + HOME_SHARE_ICONS_PATH,
                             PIXMAPS_PATH,
                             LOCAL_SHARE_ICONS_PATH,
                             SHARE_ICONS_PATH,
                             HICOLOR_SCALABLE_PATH,
                             HICOLOR_48_PATH,
                             ADWAITA_PATH};

    // Optimization: search first for the full iconName with the specified extension
    auto it = std::ranges::find_if(searchPaths, [&](const QString &path) {
        return QFile::exists(path + iconName);
    });
    if (it != searchPaths.cend()) {
        QIcon icon(*it + iconName);
        iconCache.insert(iconName, icon);
        return icon;
    }

    // Search for the icon without extension in the specified paths
    for (const QString &path : searchPaths) {
        if (!QFile::exists(path)) {
            continue;
        }
        for (const auto &ext : {QStringLiteral(".png"), QStringLiteral(".svg"), QStringLiteral(".xpm")}) {
            const QString file = path + nameNoExt + ext;
            if (QFile::exists(file)) {
                QIcon icon(file);
                iconCache.insert(iconName, icon);
                return icon;
            }
        }
    }

    // If the icon is "utilities-terminal" and not found, return the default icon if it's already loaded
    if (iconName == DEFAULT_ICON_NAME) {
        if (!defaultIconLoaded) {
            defaultIcon = QIcon();
            defaultIconLoaded = true;
        }
        iconCache.insert(iconName, defaultIcon);
        return defaultIcon;
    }

    // If the icon is not "utilities-terminal", try to load the default icon
    if (!defaultIconLoaded) {
        if (!resolvingDefault) {
            resolvingDefault = true;
            defaultIcon = findIcon(DEFAULT_ICON_NAME);
            defaultIconLoaded = true;
            resolvingDefault = false;
        } else {
            defaultIcon = QIcon();
            defaultIconLoaded = true;
        }
    }

    iconCache.insert(iconName, defaultIcon);
    return defaultIcon;
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
            qWarning() << "Process failed with exit code:" << exitCode << "Command:" << command << cmdList;
            // Show error message to user and ensure window is shown again
            QMessageBox::critical(this, tr("Error"), 
                tr("Failed to execute command") + "\n" + 
                tr("Command: %1").arg(command + " " + cmdList.join(" ")) + "\n" +
                tr("Exit code: %1").arg(exitCode));
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

    // Fast column calculation using cached button width (avoids full recalculation)
    const int effectiveWidth = cachedMaxButtonWidth > 0 ? cachedMaxButtonWidth : 200;
    const int newColCount = std::max(1, width() / effectiveWidth);

    // Early exit: column count unchanged or already at max elements
    if (newColCount == colCount || (newColCount >= maxElements && colCount == maxElements)) {
        return;
    }

    // Full recalculation to get exact column count (updates maxElements too)
    const int exactColCount = calculateMaxElements(info_map);
    if (colCount == exactColCount) {
        return;
    }
    colCount = exactColCount;

    if (ui->textSearch->text().isEmpty()) {
        addButtons(info_map);
    } else {
        textSearch_textChanged(ui->textSearch->text());
    }
}

// Hide icons in menu checkbox
void MainWindow::checkHide_clicked(bool checked)
{
    for (const QStringList &list : std::as_const(category_map)) {
        for (const QString &fileName : std::as_const(list)) {
            hideShowIcon(fileName, checked);
        }
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
        QFile::copy(fileName, fileNameLocal);
        
        // Read and modify the file content using Qt APIs
        QFile localFile(fileNameLocal);
        if (!localFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Failed to open file for reading:" << fileNameLocal;
            return;
        }

        QTextStream in(&localFile);
        QStringList lines;
        
        // Process each line
        while (!in.atEnd()) {
            QString line = in.readLine();
            
            // Skip NoDisplay and Hidden lines
            if (line.startsWith(QLatin1String("NoDisplay=")) || line.startsWith(QLatin1String("Hidden="))) {
                continue;
            }
            
            lines << line;

            // Add NoDisplay=true immediately after Exec line
            if (line.startsWith(QLatin1String("Exec="))) {
                lines << QLatin1String("NoDisplay=true");
            }
        }
        localFile.close();

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
    hide();
    displayAboutMsgBox(
        tr("About MX Tools"),
        "<p align=\"center\"><b><h2>" + tr("MX Tools") + "</h2></b></p><p align=\"center\">" + tr("Version: ") + VERSION
            + "</p><p align=\"center\"><h3>" + tr("Configuration Tools for MX Linux")
            + "</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p>"
              "<p align=\"center\">"
            + tr("Copyright (c) MX Linux") + "<br /><br /></p>",
        LICENSE_PATH, tr("%1 License").arg(windowTitle()));
    show();
}

void MainWindow::pushHelp_clicked()
{
    if (QFile::exists(MX_MANUAL_PATH)) {
        if (!QProcess::startDetached(MX_MANUAL_PATH, {})) {
            qWarning() << "Failed to start mx-manual";
            return;
        }
    } else { // for MX19?
        if (!QProcess::startDetached("xdg-open", {QStringLiteral("file://") + FALLBACK_DOC_PATH})) {
            qWarning() << "Failed to open fallback documentation";
            return;
        }
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
        filteredCategoryTools.reserve(categoryTools.size());

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

// When running live remove programs meant only for installed environments and the other way round
// Remove XfceOnly and FluxboxOnly when not running in that environment
void MainWindow::removeEnvExclusive(QStringList *list, const QStringList &termsToRemove)
{
    for (auto it = list->begin(); it != list->end();) {
        const QString &filePath = *it;
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString fileContent = QString::fromUtf8(file.readAll());
            file.close();

            bool containsTerm = std::ranges::any_of(termsToRemove, [&fileContent](const QString &term) {
                return fileContent.contains(term, Qt::CaseInsensitive);
            });

            containsTerm ? it = list->erase(it) : ++it;
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
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }
}
