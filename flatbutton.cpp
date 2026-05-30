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

#include "flatbutton.h"

namespace
{
// Underline-on-hover handled declaratively via the :hover pseudo-state so it needs no
// enter/leave event overrides. Qt Style Sheet rules are concatenated with whitespace.
const QString buttonStyle = QStringLiteral("QPushButton { text-align: left; text-decoration: none; } "
                                           "QPushButton:hover { text-decoration: underline; }");
} // namespace

FlatButton::FlatButton(QWidget *parent)
    : QPushButton(parent)
{
    setFlat(true);
    setStyleSheet(buttonStyle);
}

FlatButton::FlatButton(const QString &name, QWidget *parent)
    : QPushButton(name, parent)
{
    setFlat(true);
    setStyleSheet(buttonStyle);
}

void FlatButton::setIconSize(QSize size)
{
    QPushButton::setIconSize(size);
}
