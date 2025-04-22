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
#include <QProcess>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QStorageInfo>

#include "about.h"
#include "flatbutton.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "version.h"

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
    icon_size = settings.value("icon_size", icon_size).toInt();
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
    const QDir userApplicationsDir(QDir::homePath() + "/.local/share/applications");
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
    const QString searchFolder {"/usr/share/applications"};
    for (auto it = categories.cbegin(); it != categories.cend(); ++it) {
        *(it.value()) = listDesktopFiles(it.key(), searchFolder);
    }
}

void MainWindow::filterLiveEnvironmentItems()
{
    QStorageInfo storageInfo(QDir::rootPath());
    const QString partitionType = storageInfo.fileSystemType();
    bool live = partitionType == "aufs" || partitionType == "overlay";

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

    for (auto it = desktopTerms.keyValueBegin(); it != desktopTerms.keyValueEnd(); ++it) {
        if (qgetenv("XDG_CURRENT_DESKTOP") != it->first) {
            termsToRemove << it->second;
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
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QString content = in.readAll();
            if (content.contains(searchString)) {
                matchingFiles << filePath;
            }
        }
    }
    return matchingFiles;
}

int MainWindow::calculateMaxElements(const QMultiMap<QString, QMultiMap<QString, QStringList>> &info_map)
{
    max_elements = 0;
    // Find maximum number of elements across all categories
    for (const auto &categoryMap : info_map) {
        max_elements = std::max(max_elements, static_cast<int>(categoryMap.size()));
    }

    // Calculate maximum button width based on first category
    const QString firstCategory = info_map.isEmpty() ? QString() : info_map.firstKey();
    int max_button_width = 20;
    for (const auto &fileInfo : info_map.value(firstCategory)) {
        const QString &name = fileInfo.at(Info::Name);
        max_button_width = qMax(name.size() * QApplication::font().pointSize() + icon_size, max_button_width);
    }

    // Calculate maximum columns that fit in window width
    return std::max(1, width() / max_button_width);
}

// Load info (name, comment, exec, iconName, category, terminal) to the info_map
void MainWindow::readInfo(const QMultiMap<QString, QStringList> &category_map)
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

    for (auto it = category_map.cbegin(); it != category_map.cend(); ++it) {
        const QString category = it.key();
        const QStringList &fileList = it.value();

        QMultiMap<QString, QStringList> categoryInfoMap;

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

            QString iconName = getValueFromText(text, iconKey);
            QString terminalSwitch = getValueFromText(text, terminalKey);

            QStringList infoList;
            infoList.reserve(6);
            infoList << name << comment << iconName << exec << category << terminalSwitch;

            categoryInfoMap.insert(fileName, infoList);
        }
        info_map.insert(category, categoryInfoMap);
    }
}

QString MainWindow::getTranslation(const QString &text, const QString &key, const QString &langRegion,
                                   const QString &lang)
{
    // First try to find translation for specific region (e.g., en_US)
    QRegularExpression re(QStringLiteral("^") + key + QStringLiteral("\\[") + langRegion + QStringLiteral("\\]=(.*)$"));
    re.setPatternOptions(QRegularExpression::MultilineOption);
    QRegularExpressionMatch match = re.match(text);
    if (match.hasMatch()) {
        QString translation = match.captured(1).trimmed();
        if (!translation.isEmpty()) {
            return translation;
        }
    }

    // Fall back to general language (e.g., en)
    re.setPattern(QStringLiteral("^") + key + QStringLiteral("\\[") + lang + QStringLiteral("\\]=(.*)$"));
    match = re.match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QString MainWindow::getValueFromText(const QString &text, const QString &key)
{
    QString pattern = QStringLiteral("^") + key + QStringLiteral("=(.*)$");
    QRegularExpression re(pattern);
    re.setPatternOptions(QRegularExpression::MultilineOption);
    return re.match(text).captured(1).trimmed();
}

// Read the info_map and add the buttons to the UI
void MainWindow::addButtons(const QMultiMap<QString, QMultiMap<QString, QStringList>> &info_map)
{
    clearGrid();

    const int max_columns = calculateMaxElements(info_map);
    int row = 0;

    // Add buttons for each category
    for (auto it = info_map.cbegin(); it != info_map.cend(); ++it) {
        const QString &category = it.key();
        const auto &categoryMap = it.value();

        if (categoryMap.isEmpty()) {
            continue;
        }

        if (row > 0) {
            addCategorySeparator(row, max_columns);
        }

        addCategoryHeader(category, row, max_columns);

        // Add buttons for this category
        int col = 0;
        for (const auto &fileInfo : categoryMap) {
            col_count = std::max(col_count, col + 1);
            auto *btn = createButton(fileInfo);
            ui->gridLayout_btn->addWidget(btn, row, col);

            // Move to the next row if more items than max columns
            if (++col >= max_columns) {
                col = 0;
                ++row;
            }
        }
    }
    ui->gridLayout_btn->setRowStretch(row, 1);
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

FlatButton *MainWindow::createButton(const QStringList &fileInfo)
{
    auto *btn = new FlatButton(fileInfo.at(Info::Name));
    btn->setToolTip(fileInfo.at(Info::Comment));
    btn->setAutoDefault(false);
    const QIcon icon = findIcon(fileInfo.at(Info::IconName));
    btn->setIcon(icon);
    btn->setIconSize({icon_size, icon_size});

    // Configure button command
    const QString &exec = fileInfo.at(Info::Exec);
    const bool runInTerminal = fileInfo.at(Info::Terminal) == "true";
    btn->setObjectName(runInTerminal ? "x-terminal-emulator -e " + exec : exec);
    connect(btn, &FlatButton::clicked, this, &MainWindow::btn_clicked);

    return btn;
}

QIcon MainWindow::findIcon(const QString &iconName)
{
    static QIcon defaultIcon;
    static bool defaultIconLoaded = false;

    if (iconName.isEmpty()) {
        if (!defaultIconLoaded) {
            defaultIcon = findIcon(QStringLiteral("utilities-terminal"));
            defaultIconLoaded = true;
        }
        return defaultIcon;
    }

    // Check if the icon name is an absolute path and exists
    if (QFileInfo(iconName).isAbsolute() && QFile::exists(iconName)) {
        return QIcon(iconName);
    }

    // Prepare regular expression to strip extension
    static const QRegularExpression re(R"(\.(png|svg|xpm)$)");
    QString nameNoExt = iconName;
    nameNoExt.remove(re);

    // Return the themed icon if available
    if (QIcon::hasThemeIcon(nameNoExt)) {
        return QIcon::fromTheme(nameNoExt);
    }

    // Define common search paths for icons
    QStringList searchPaths {QDir::homePath() + QStringLiteral("/.local/share/icons/"),
                             QStringLiteral("/usr/share/pixmaps/"),
                             QStringLiteral("/usr/local/share/icons/"),
                             QStringLiteral("/usr/share/icons/"),
                             QStringLiteral("/usr/share/icons/hicolor/scalable/apps/"),
                             QStringLiteral("/usr/share/icons/hicolor/48x48/apps/"),
                             QStringLiteral("/usr/share/icons/Adwaita/48x48/legacy/")};

    // Optimization: search first for the full iconName with the specified extension
    auto it = std::find_if(searchPaths.cbegin(), searchPaths.cend(),
                           [&](const QString &path) { return QFile::exists(path + iconName); });
    if (it != searchPaths.cend()) {
        return QIcon(*it + iconName);
    }

    // Search for the icon without extension in the specified paths
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

    // If the icon is "utilities-terminal" and not found, return the default icon if it's already loaded
    if (iconName == QStringLiteral("utilities-terminal")) {
        if (!defaultIconLoaded) {
            defaultIcon = QIcon();
            defaultIconLoaded = true;
        }
        return defaultIcon;
    }

    // If the icon is not "utilities-terminal", try to load the default icon
    return findIcon(QStringLiteral("utilities-terminal"));
}

void MainWindow::btn_clicked()
{
    hide();
    QStringList cmdList = QProcess::splitCommand(sender()->objectName());
    if (cmdList.isEmpty()) {
        qWarning() << "Empty command list";
        show();
        return;
    }

    QString command = cmdList.takeFirst();
    if (cmdList.last() == "&") {
        cmdList.removeLast();
        if (!QProcess::startDetached(command, cmdList)) {
            qWarning() << "Failed to start detached process:" << command << cmdList;
        }
    } else {
        int exitCode = QProcess::execute(command, cmdList);
        if (exitCode != 0) {
            qWarning() << "Process failed with exit code:" << exitCode << "Command:" << command << cmdList;
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
    const int newCount = width() / 200;
    if (newCount == col_count || (newCount > max_elements && col_count == max_elements)) {
        return;
    }
    // Finer check
    const int max = calculateMaxElements(info_map);
    if (col_count == max) {
        return;
    }
    col_count = max;

    if (ui->textSearch->text().isEmpty()) {
        addButtons(info_map);
    } else {
        textSearch_textChanged(ui->textSearch->text());
    }
}

// Hide icons in menu checkbox
void MainWindow::checkHide_clicked(bool checked)
{
    for (const QStringList &list : qAsConst(category_map)) {
        for (const QString &fileName : qAsConst(list)) {
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
    QFileInfo file(fileName);
    const QDir userApplicationsDir(QDir::homePath() + "/.local/share/applications");
    if (!userApplicationsDir.exists() && !QDir().mkpath(userApplicationsDir.absolutePath())) {
        qWarning() << "Failed to create directory:" << userApplicationsDir.absolutePath();
        return;
    }

    const QString fileNameLocal = userApplicationsDir.filePath(file.fileName());
    if (!hide) {
        QFile::remove(fileNameLocal);
    } else {
        QFile::copy(fileName, fileNameLocal);
        QProcess process;
        process.start("sed", {"-i", "-re", "/^(NoDisplay|Hidden)=/d", "-e", "/Exec/aNoDisplay=true", fileNameLocal});
        if (!process.waitForFinished()) {
            qWarning() << "Failed to modify file:" << fileNameLocal;
            return;
        }
        if (process.exitCode() != 0) {
            qWarning() << "sed command failed with exit code:" << process.exitCode();
            return;
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
        "/usr/share/doc/mx-tools/license.html", tr("%1 License").arg(windowTitle()));
    show();
}

void MainWindow::pushHelp_clicked()
{
    const QString manualPath = "/usr/bin/mx-manual";
    const QString fallbackPath = "/usr/local/share/doc/mxum.html#toc-Subsection-3.2";

    if (QFile::exists(manualPath)) {
        if (!QProcess::startDetached(manualPath, {})) {
            qWarning() << "Failed to start mx-manual";
            return;
        }
    } else { // for MX19?
        if (!QProcess::startDetached("xdg-open", {"file://" + fallbackPath})) {
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

    QMultiMap<QString, QMultiMap<QString, QStringList>> filteredMap;

    // Iterate over categories in info_map
    for (auto it = info_map.constBegin(); it != info_map.constEnd(); ++it) {
        const auto &category = it.key();
        const auto &fileInfo = it.value();
        QMultiMap<QString, QStringList> filteredCategoryMap;

        // Iterate over file names in the current category
        for (const auto &fileName : category_map.value(category)) {
            const auto &fileData = fileInfo.value(fileName);
            const auto &name = fileData.at(Info::Name);
            const auto &comment = fileData.at(Info::Comment);

            if (name.contains(searchTerm, Qt::CaseInsensitive) || comment.contains(searchTerm, Qt::CaseInsensitive)
                || category.contains(searchTerm, Qt::CaseInsensitive)) {
                filteredCategoryMap.insert(fileName, fileData);
            }
        }

        if (!filteredCategoryMap.isEmpty()) {
            filteredMap.insert(category, filteredCategoryMap);
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

            bool containsTerm
                = std::any_of(termsToRemove.cbegin(), termsToRemove.cend(), [&fileContent](const QString &term) {
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
    QLayoutItem *child;
    while ((child = ui->gridLayout_btn->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }
}
