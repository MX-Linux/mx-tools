#pragma once

class QString;

void displayDoc(const QString &url, const QString &title, bool largeWindow = false);
void displayHelpDoc(const QString &path, const QString &title);
void displayAboutMsgBox(const QString &title, const QString &message, const QString &licence_url,
                        const QString &license_title);
