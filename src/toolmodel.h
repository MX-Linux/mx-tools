/**********************************************************************
 * Copyright (C) 2014-2026 MX Authors
 *
 * This file is part of MX Tools and is licensed under GPL-3.0-or-later.
 **********************************************************************/
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QIcon>
#include <QQuickImageProvider>
#include <QStringList>

#include <optional>

class QSettings;

class ToolIconProvider final : public QQuickImageProvider
{
public:
    ToolIconProvider();
    void insert(const QString &key, const QIcon &icon);
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QHash<QString, QIcon> m_icons;
};

class ToolModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString selectedCategory READ selectedCategory WRITE setSelectedCategory NOTIFY selectedCategoryChanged)
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
    Q_PROPERTY(int totalCount READ totalCount CONSTANT)
    Q_PROPERTY(bool hideFromMenu READ hideFromMenu WRITE setHideFromMenu NOTIFY hideFromMenuChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        CommentRole,
        CategoryRole,
        IconSourceRole,
        FileNameRole
    };
    Q_ENUM(Role)

    explicit ToolModel(ToolIconProvider *iconProvider, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString search() const;
    void setSearch(const QString &search);
    [[nodiscard]] QString selectedCategory() const;
    void setSelectedCategory(const QString &category);
    [[nodiscard]] QStringList categories() const;
    [[nodiscard]] int totalCount() const;
    [[nodiscard]] bool hideFromMenu() const;
    void setHideFromMenu(bool hide);

    Q_INVOKABLE void launch(const QString &fileName);
    Q_INVOKABLE void openManual();
    Q_INVOKABLE void openLicense();
    Q_INVOKABLE void openWebsite();
    Q_INVOKABLE void openChangelog();

signals:
    void searchChanged();
    void selectedCategoryChanged();
    void hideFromMenuChanged();
    void errorOccurred(const QString &title, const QString &message);
    void documentReady(const QString &title, const QString &content);

private:
    struct ToolInfo {
        QString fileName;
        QString name;
        QString comment;
        QString keywords;
        QString iconSource;
        QString exec;
        QString category;
        QString searchText;
        bool runInTerminal = false;
    };

    QVector<ToolInfo> m_allTools;
    QVector<int> m_visibleRows;
    QStringList m_categories;
    QStringList m_menuFiles;
    QString m_search;
    QString m_selectedCategory;
    QHash<QString, qint64> m_runningTools;
    ToolIconProvider *m_iconProvider;
    bool m_hideFromMenu = false;
    bool m_legacyMenuState = false;

    void loadTools();
    void refilter();
    void detectMenuVisibility();
    [[nodiscard]] bool hideMenuEntries();
    [[nodiscard]] bool restoreMenuEntries();
    [[nodiscard]] bool restoreLegacyMenuEntries();
    void snapshotWhiskerMenuFavorites(QSettings &state);
    void reconcileWhiskerMenuFavorites(QSettings &state);
    [[nodiscard]] static QString value(const QString &text, const QString &key);
    [[nodiscard]] static QString translatedValue(const QString &text, const QString &key);
    [[nodiscard]] static QStringList desktopFilesForCategory(const QStringList &tokens);
    [[nodiscard]] static bool visibleInCurrentEnvironment(const QString &text);
    [[nodiscard]] static std::optional<QIcon> lookupIcon(const QString &iconName);
    [[nodiscard]] static QIcon fallbackIcon();
    static void openLocalOrReport(const QString &path, ToolModel *model, const QString &title);
};
