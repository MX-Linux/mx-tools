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
#include "mainwindow.h"
#include "ui_mainwindow.h"

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

    while (it.hasNext()) {
        QFile file(it.next());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QString content = in.readAll();
            if (content.contains(searchString)) {
                matchingFiles << file.fileName();
            }
        }
    }
    return matchingFiles;
}

// Load info (name, comment, exec, iconName, category, terminal) to the info_map
void MainWindow::readInfo(const QMultiMap<QString, QStringList> &category_map)
{
    const QString lang = QLocale().name().split('_').first();
    const QString langRegion = QLocale().name();

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

            QString name = lang != "en" ? getTranslation(text, "Name", langRegion, lang) : QString();
            QString comment = lang != "en" ? getTranslation(text, "Comment", langRegion, lang) : QString();

            name = name.isEmpty() ? getValueFromText(text, "Name").remove(QRegularExpression("^MX ")).replace('&', "&&")
                                  : name;
            comment = comment.isEmpty() ? getValueFromText(text, "Comment") : comment;

            QString exec = getValueFromText(text, "Exec");
            fixExecItem(&exec);

            QString iconName = getValueFromText(text, "Icon");
            QString terminalSwitch = getValueFromText(text, "Terminal");

            categoryInfoMap.insert(fileName, {name, comment, iconName, exec, category, terminalSwitch});
        }
        info_map.insert(category, categoryInfoMap);
    }
}

QString MainWindow::getTranslation(const QString &text, const QString &key, const QString &langRegion,
                                   const QString &lang)
{
    QRegularExpression re('^' + key + "\\[" + langRegion + "\\]=(.*)$");
    re.setPatternOptions(QRegularExpression::MultilineOption);
    QString translation = re.match(text).captured(1).trimmed();
    if (!translation.isEmpty()) {
        return translation;
    }
    re.setPattern('^' + key + "\\[" + lang + "\\]=(.*)$");
    return re.match(text).captured(1).trimmed();
}

QString MainWindow::getValueFromText(const QString &text, const QString &key)
{
    QRegularExpression re('^' + key + "=(.*)$");
    re.setPatternOptions(QRegularExpression::MultilineOption);
    return re.match(text).captured(1).trimmed();
}

// Read the info_map and add the buttons to the UI
void MainWindow::addButtons(const QMultiMap<QString, QMultiMap<QString, QStringList>> &info_map)
{
    clearGrid();

    int col = 0;
    int row = 0;
    int max = 200;

    max_elements = 0;
    QMapIterator<QString, QMultiMap<QString, QStringList>> it(info_map);
    QString category;
    while (it.hasNext()) {
        category = it.next().key();
        if (info_map.value(category).keys().count() > max_elements) {
            max_elements = info_map.value(category).keys().count();
        }
    }

    QString name;
    QString comment;
    QString exec;
    QString iconName;
    QString terminal_switch;

    // Get max button size
    QMapIterator<QString, QStringList> itsize(info_map.value(category));
    int max_button_width = 20; // set a min != 0 to avoid div/0 in case of error
    while (itsize.hasNext()) {
        QString fileName = itsize.next().key();
        QStringList fileInfo = info_map.value(category).value(fileName);
        name = fileInfo.at(Info::Name);
        max_button_width = qMax(name.size() * QApplication::font().pointSize() + icon_size, max_button_width);
    }
    max = width() / max_button_width;
    it.toFront();
    while (it.hasNext()) {
        category = it.next().key();
        if (!info_map.value(category).isEmpty()) {
            // Add empty row and delimiter except for the first row
            if (row != 0) {
                ++row;
                auto *line = new QFrame();
                line->setFrameShape(QFrame::HLine);
                line->setFrameShadow(QFrame::Sunken);
                ui->gridLayout_btn->addWidget(line, row, 0, 1, -1);
            }
            auto *label = new QLabel();
            QFont font;
            font.setBold(true);
            font.setUnderline(true);
            label->setFont(font);
            QString label_txt = category;
            label_txt.remove(QRegularExpression("^MX-"));
            label->setText(label_txt);
            ++row;
            ui->gridLayout_btn->addWidget(label, row, 0);
            ++row;
            col = 0;
            QMapIterator<QString, QStringList> it(info_map.value(category));
            while (it.hasNext()) {
                QString fileName = it.next().key();
                if (col >= col_count) {
                    col_count = col + 1;
                }
                QStringList fileInfo = info_map.value(category).value(fileName);
                name = fileInfo.at(Info::Name);
                comment = fileInfo.at(Info::Comment);
                iconName = fileInfo.at(Info::IconName);
                exec = fileInfo.at(Info::Exec);
                terminal_switch = fileInfo.at(Info::Terminal);
                btn = new FlatButton(name);
                btn->setToolTip(comment);
                btn->setAutoDefault(false);
                btn->setIcon(findIcon(iconName));
                btn->setIconSize(icon_size, icon_size);
                ui->gridLayout_btn->addWidget(btn, row, col);
                // ui->gridLayout_btn->setRowStretch(row, 0);
                ++col;

                if (col >= max) {
                    col = 0;
                    ++row;
                }
                btn->setObjectName(terminal_switch == "true" ? "x-terminal-emulator -e " + exec : exec);
                QObject::connect(btn, &FlatButton::clicked, this, &MainWindow::btn_clicked);
            }
        }
    }
    ui->gridLayout_btn->setRowStretch(row + 2, 1);
}

QIcon MainWindow::findIcon(const QString &iconName)
{
    static QIcon defaultIcon;
    static bool defaultIconLoaded = false;

    if (iconName.isEmpty()) {
        if (!defaultIconLoaded) {
            defaultIcon = findIcon("utilities-terminal");
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
    QStringList searchPaths {QDir::homePath() + "/.local/share/icons/",
                             "/usr/share/pixmaps/",
                             "/usr/local/share/icons/",
                             "/usr/share/icons/",
                             "/usr/share/icons/hicolor/scalable/apps/",
                             "/usr/share/icons/hicolor/48x48/apps/",
                             "/usr/share/icons/Adwaita/48x48/legacy/"};

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
        for (const char *ext : {".png", ".svg", ".xpm"}) {
            QString file = path + nameNoExt + ext;
            if (QFile::exists(file)) {
                return QIcon(file);
            }
        }
    }

    // If the icon is "utilities-terminal" and not found, return the default icon if it's already loaded
    if (iconName == "utilities-terminal") {
        if (!defaultIconLoaded) {
            defaultIcon = QIcon();
            defaultIconLoaded = true;
        }
        return defaultIcon;
    }

    // If the icon is not "utilities-terminal", try to load the default icon
    return findIcon("utilities-terminal");
}

void MainWindow::btn_clicked()
{
    hide();
    QStringList cmdList = QProcess::splitCommand(sender()->objectName());
    if (cmdList.last() == "&") {
        cmdList.removeLast();
        QProcess::startDetached(cmdList.first(), cmdList);
    } else {
        QProcess::execute(cmdList.first(), cmdList);
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
    int newCount = width() / 200;
    if (newCount == col_count || (newCount > max_elements && col_count == max_elements)) {
        return;
    }
    col_count = newCount;

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
        process.start("sed", {"-ire", "/^(NoDisplay|Hidden)=/d", "-e", "/Exec/aNoDisplay=true", fileNameLocal});
        process.waitForFinished();
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
    if (QFile::exists("/usr/bin/mx-manual")) {
        QProcess::startDetached("mx-manual", {});
    } else { // for MX19?
        QProcess::startDetached("xdg-open", {"file:///usr/local/share/doc/mxum.html#toc-Subsection-3.2"});
    }
}

void MainWindow::textSearch_textChanged(const QString &searchTerm)
{
    // Check if the search term is empty and display all buttons if it is
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

            // Check if any part of the file matches the search term
            if (name.contains(searchTerm, Qt::CaseInsensitive) || comment.contains(searchTerm, Qt::CaseInsensitive)
                || category.contains(searchTerm, Qt::CaseInsensitive)) {
                filteredCategoryMap.insert(fileName, fileData);
            }
        }

        // If the filtered category map is not empty, add it to the filtered map
        if (!filteredCategoryMap.isEmpty()) {
            filteredMap.insert(category, filteredCategoryMap);
        }
    }

    // Display buttons for the filtered map
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
    while (auto *child = ui->gridLayout_btn->takeAt(0)) {
        delete child->widget();
        delete child;
    }
}
