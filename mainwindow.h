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
#pragma once

#include <QDialog>
#include <QCloseEvent>
#include <QMessageBox>
#include <QMap>
#include <QMultiMap>
#include <QResizeEvent>
#include <QSettings>
#include <QStringList>
#include <QVector>

#include "flatbutton.h"

namespace Ui
{
class MainWindow;
}

class MainWindow : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY(MainWindow)

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    static void pushHelp_clicked();
    void btn_clicked();
    void checkHide_clicked(bool checked);
    void pushAbout_clicked();
    void textSearch_textChanged(const QString &arg1);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct ToolInfo {
        QString fileName;
        QString name;
        QString comment;
        QString iconName;
        QString exec;
        QString category;
        bool runInTerminal = false;
    };

    using CategoryToolsMap = QMap<QString, QVector<ToolInfo>>;

    Ui::MainWindow *ui;
    CategoryToolsMap info_map;
    QMultiMap<QString, QStringList> category_map;
    QSettings settings;
    QStringList live_list;
    QStringList maintenance_list;
    QStringList setup_list;
    QStringList software_list;
    QStringList utilities_list;
    const QMap<QString, QStringList *> categories {{"MX-Live", &live_list},
                                                   {"MX-Maintenance", &maintenance_list},
                                                   {"MX-Setup", &setup_list},
                                                   {"MX-Software", &software_list},
                                                   {"MX-Utilities", &utilities_list}};
    int colCount = 0;
    int iconSize = 32;
    int maxElements = 0;
    int cachedMaxButtonWidth = 0;

    // Path constants
    static constexpr auto APPLICATIONS_PATH = "/usr/share/applications";
    static constexpr auto USER_APPLICATIONS_PATH = "/.local/share/applications";
    static constexpr auto HOME_SHARE_ICONS_PATH = "/.local/share/icons/";
    static constexpr auto PIXMAPS_PATH = "/usr/share/pixmaps/";
    static constexpr auto LOCAL_SHARE_ICONS_PATH = "/usr/local/share/icons/";
    static constexpr auto SHARE_ICONS_PATH = "/usr/share/icons/";
    static constexpr auto HICOLOR_SCALABLE_PATH = "/usr/share/icons/hicolor/scalable/apps/";
    static constexpr auto HICOLOR_48_PATH = "/usr/share/icons/hicolor/48x48/apps/";
    static constexpr auto ADWAITA_PATH = "/usr/share/icons/Adwaita/48x48/legacy/";
    static constexpr auto MX_TOOLS_PATH = "/usr/bin/mx-tools";
    static constexpr auto MX_MANUAL_PATH = "/usr/bin/mx-manual";
    static constexpr auto FALLBACK_DOC_PATH = "/usr/local/share/doc/mxum.html#toc-Subsection-3.2";
    static constexpr auto LICENSE_PATH = "/usr/share/doc/mx-tools/license.html";
    static constexpr auto DEFAULT_ICON_NAME = "utilities-terminal";

    [[nodiscard]] FlatButton *createButton(const ToolInfo &toolInfo);
    [[nodiscard]] QString getTranslation(const QString &text, const QString &key, const QString &langRegion,
                                         const QString &lang);
    [[nodiscard]] QString getValueFromText(const QString &text, const QString &key);
    [[nodiscard]] QIcon findIcon(const QString &iconName);
    [[nodiscard]] QStringList listDesktopFiles(const QString &searchString, const QString &location);
    [[nodiscard]] int calculateMaxElements(const CategoryToolsMap &infoMap);
    static void fixExecItem(QString *item);
    static void hideShowIcon(const QString &fileName, bool hide);
    static void removeEnvExclusive(QStringList *list, const QStringList &termsToRemove);
    void addButtons(const CategoryToolsMap &infoMap);
    void addCategoryHeader(const QString &category, int &row, int max_columns);
    void addCategorySeparator(int &row, int max_columns);
    void checkHideToolsInMenu();
    void clearGrid();
    void filterDesktopEnvironmentItems();
    void filterLiveEnvironmentItems();
    void initializeCategoryLists();
    void populateCategoryMap();
    void readInfo(const QMultiMap<QString, QStringList> &category_map);
    void restoreWindowGeometry();
    void setConnections();
};
