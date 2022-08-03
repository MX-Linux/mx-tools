#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include "about.h"
#include <unistd.h>

// display doc as nomal user when run as root
void displayDoc(const QString &url, const QString &title)
{
    bool started_as_root = false;
    // prefer mx-viewer otherwise use xdg-open (use runuser to run that as logname user)
    if (QFile::exists(QStringLiteral("/usr/bin/mx-viewer"))) {
        QProcess::execute(QStringLiteral("mx-viewer"), {url, title});
    } else {
        if (getuid() != 0) {
            QProcess::execute(QStringLiteral("xdg-open"), {url});
        } else {
            QProcess proc;
            proc.start(QStringLiteral("logname"), {}, QIODevice::ReadOnly);
            proc.waitForFinished();
            QString user = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            QProcess::startDetached(QStringLiteral("runuser"),
                                    {QStringLiteral("-u"), user, QStringLiteral("--"), QStringLiteral("xdg-open"), url});
        }
    }
    if (started_as_root)
        qputenv("HOME", "/root");
}

void displayAboutMsgBox(const QString &title, const QString &message, const QString &licence_url, const QString &license_title)
{
    const auto width  = 600;
    const auto height = 500;
    QMessageBox msgBox(QMessageBox::NoIcon, title, message);
    auto *btnLicense = msgBox.addButton(QObject::tr("License"), QMessageBox::HelpRole);
    auto *btnChangelog = msgBox.addButton(QObject::tr("Changelog"), QMessageBox::HelpRole);
    auto *btnCancel = msgBox.addButton(QObject::tr("Cancel"), QMessageBox::NoRole);
    btnCancel->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));

    msgBox.exec();

    if (msgBox.clickedButton() == btnLicense) {
        displayDoc(licence_url, license_title);
    } else if (msgBox.clickedButton() == btnChangelog) {
        auto *changelog = new QDialog;
        changelog->setWindowTitle(QObject::tr("Changelog"));
        changelog->resize(width, height);

        auto *text = new QTextEdit(changelog);
        text->setReadOnly(true);
        QProcess proc;
        proc.start(QStringLiteral("zless"), {QStringLiteral("/usr/share/doc/") +
                                             QFileInfo(QCoreApplication::applicationFilePath()).fileName() +
                                             QStringLiteral("/changelog.gz")}, QIODevice::ReadOnly);
        proc.waitForFinished();
        text->setText(proc.readAllStandardOutput());

        auto *btnClose = new QPushButton(QObject::tr("&Close"), changelog);
        btnClose->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
        QObject::connect(btnClose, &QPushButton::clicked, changelog, &QDialog::close);

        auto *layout = new QVBoxLayout(changelog);
        layout->addWidget(text);
        layout->addWidget(btnClose);
        changelog->setLayout(layout);
        changelog->exec();
        delete changelog;
    }
}
