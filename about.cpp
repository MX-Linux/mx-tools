/**********************************************************************
 *
 **********************************************************************
 * Copyright (C) 2023-2024 MX Authors
 *
 * Authors: Adrian <adrian@mxlinux.org>
 *          MX Linux <http://mxlinux.org>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package. If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
#include "about.h"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextEdit>
#include <QVBoxLayout>

#include <unistd.h>

// Display doc as nomal user when run as root
void displayDoc(const QString &url, const QString &title)
{
    // Prefer mx-viewer otherwise use xdg-open (use runuser to run that as logname user)
    QString executablePath = QStandardPaths::findExecutable("mx-viewer");
    if (!executablePath.isEmpty()) {
        QProcess::startDetached("mx-viewer", {url, title});
    } else {
        if (getuid() != 0) {
            QProcess::startDetached("xdg-open", {url});
        } else {
            QProcess proc;
            proc.start("logname", {}, QIODevice::ReadOnly);
            proc.waitForFinished();
            QString user = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            QProcess::startDetached("runuser", {"-u", user, "--", "xdg-open", url});
        }
    }
}

void displayAboutMsgBox(const QString &title, const QString &message, const QString &licence_url,
                        const QString &license_title)
{
    const auto width = 600;
    const auto height = 500;
    QMessageBox msgBox(QMessageBox::NoIcon, title, message);
    auto *btnLicense = msgBox.addButton(QObject::tr("License"), QMessageBox::HelpRole);
    auto *btnChangelog = msgBox.addButton(QObject::tr("Changelog"), QMessageBox::HelpRole);
    auto *btnCancel = msgBox.addButton(QObject::tr("Cancel"), QMessageBox::NoRole);
    btnCancel->setIcon(QIcon::fromTheme("window-close"));

    msgBox.exec();

    if (msgBox.clickedButton() == btnLicense) {
        displayDoc(licence_url, license_title);
    } else if (msgBox.clickedButton() == btnChangelog) {
        QDialog changelog;
        changelog.setWindowTitle(QObject::tr("Changelog"));
        changelog.resize(width, height);

        auto *text = new QTextEdit(&changelog);
        text->setReadOnly(true);
        const QString changelogPath =
            QStringLiteral("/usr/share/doc/") + QFileInfo(QCoreApplication::applicationFilePath()).fileName()
            + QStringLiteral("/changelog.gz");
        QProcess proc;
        proc.start("zcat", {changelogPath}, QIODevice::ReadOnly);
        if (!proc.waitForFinished()) {
            text->setText(QObject::tr("Unable to load changelog from %1").arg(changelogPath));
        } else {
            text->setText(proc.readAllStandardOutput());
        }

        QPushButton btnClose(QObject::tr("&Close"), &changelog);
        btnClose.setIcon(QIcon::fromTheme("window-close"));
        QObject::connect(&btnClose, &QPushButton::clicked, &changelog, &QDialog::close);

        auto *layout = new QVBoxLayout(&changelog);
        layout->addWidget(text);
        layout->addWidget(&btnClose);
        changelog.setLayout(layout);
        changelog.exec();
    }
}
