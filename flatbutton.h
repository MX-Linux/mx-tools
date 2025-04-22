/**********************************************************************
 * Copyright (C) 2014-2024 MX Authors
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

#include <QEnterEvent>
#include <QEvent>
#include <QPushButton>

class QEvent;

class FlatButton : public QPushButton
{
    Q_OBJECT
public:
    explicit FlatButton(QWidget *parent = nullptr);
    explicit FlatButton(const QString &name, QWidget *parent = nullptr);
    void setIconSize(int, int);
    void setIconSize(QSize);

protected:
    void enterEvent(QEnterEvent *e);
    void leaveEvent(QEvent *e);
};
