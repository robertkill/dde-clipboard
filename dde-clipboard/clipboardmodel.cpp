/*
 * Copyright (C) 2018 ~ 2025 Deepin Technology Co., Ltd.
 *
 * Author:     fanpengcheng <fanpengcheng_cm@deepin.com>
 *
 * Maintainer: fanpengcheng <fanpengcheng_cm@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "clipboardmodel.h"

#include <QApplication>
#include <QPointer>
#include <QDebug>
#include <QDir>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>

ClipboardModel::ClipboardModel(ListView *list, QObject *parent) : QAbstractListModel(parent)
    , m_list(list)
    , m_loaderInter(new ClipboardLoader("com.deepin.dde.ClipboardLoader",
                                        "/com/deepin/dde/ClipboardLoader",
                                        QDBusConnection::sessionBus(), this))
{
    checkDbusConnect();
}

void ClipboardModel::clear()
{
    beginResetModel();
    m_data.clear();
    endResetModel();

    Q_EMIT dataChanged();
}

const QList<ItemData *> ClipboardModel::data()
{
    return m_data;
}

int ClipboardModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_data.size();
}

QVariant ClipboardModel::data(const QModelIndex &index, int role) const
{
    Q_UNUSED(role)
    if (index.isValid() && index.row() < m_data.size()) {
        QPointer<ItemData> dt = m_data.at(index.row());
        return QVariant::fromValue(dt);
    }

    return QVariant();
}

Qt::ItemFlags ClipboardModel::flags(const QModelIndex &index) const
{
    if (index.isValid()) {
        if (m_list != nullptr) m_list->openPersistentEditor(index);
        return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
    }
    return QAbstractListModel::flags(index);
}

void ClipboardModel::destroy(ItemData *data)
{
    int row = m_data.indexOf(data);
    if(row == -1) return;

    m_list->startAni(row);

    QTimer::singleShot(AnimationTime, this, [ = ] {
        if(m_data.indexOf(data) == -1) return;
        beginRemoveRows(QModelIndex(), row, row);
        auto item = m_data.takeAt(m_data.indexOf(data));
        endRemoveRows();

        item->deleteLater();

        beginResetModel();
        endResetModel();

        Q_EMIT dataChanged();
    });
}

void ClipboardModel::reborn(ItemData *data)
{
    int idx = m_data.indexOf(data);
    if (idx < 1) {
        Q_EMIT dataReborn();
        return;
    }

    QByteArray buf;
    QDataStream stream(&buf, QIODevice::WriteOnly);
    QByteArray iconBuf;
    stream << data->formatMap()
           << data->type()
           << data->urls()
           << data->imageData().isValid();
    if (data->imageData().isValid()) {
        stream << data->imageData();
    }
    stream  << data->dataEnabled()
            << data->text()
            << data->time()
            << iconBuf;

    m_loaderInter->dataReborned(buf);

    beginRemoveRows(QModelIndex(), idx, idx);
    m_data.removeOne(data);
    endRemoveRows();
    data->deleteLater();

    Q_EMIT dataReborn();
}

void ClipboardModel::checkDbusConnect()
{
    QTimer *timer = new QTimer(this);
    timer->setInterval(1000);

    connect(timer, &QTimer::timeout, this, [ = ] {
        if (m_loaderInter->isValid())
        {
            connect(m_loaderInter, &ClipboardLoader::dataComing, this, &ClipboardModel::dataComing);
            timer->stop();
        }
    });
    timer->start();
}

void ClipboardModel::dataComing(const QByteArray &buf)
{
    ItemData *item = new ItemData(buf);
    if (item->type() == Unknown) {
        item->deleteLater();
        return;
    }

    beginInsertRows(QModelIndex(), 0, 0);
    connect(item, &ItemData::destroy, this, &ClipboardModel::destroy);
    connect(item, &ItemData::reborn, this, &ClipboardModel::reborn);
    m_data.push_front(item);
    endInsertRows();

    Q_EMIT dataChanged();
}
