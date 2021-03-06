/*
 * Copyright (C) 2019 ~ 2019 Deepin Technology Co., Ltd.
 *
 * Author:     dengkeyun <dengkeyun@uniontech.com>
 *
 * Maintainer: dengkeyun <dengkeyun@uniontech.com>
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

#include "defenderinterface.h"

#include <QDebug>
#include <QThread>
#include <QDateTime>
#include <QDBusPendingCall>
#include <QApplication>

#define DBUS_SERVICE_NAME "com.deepin.defender.daemonservice"
#define DBUS_SERVICE_PATH "/com/deepin/defender/daemonservice"
#define DBUS_INTERFACE_NAME "com.deepin.defender.daemonservice"

DefenderInterface::DefenderInterface(QObject *parent)
    : QObject(parent)
    , m_started(false)
{

}

void DefenderInterface::start()
{
    if (m_started)
        return;

    m_started = true;

    qInfo() << "create dbus interface:" << DBUS_SERVICE_NAME;
    interface.reset(new QDBusInterface(DBUS_SERVICE_NAME,
                         DBUS_SERVICE_PATH,
                         DBUS_INTERFACE_NAME,
                         QDBusConnection::sessionBus()));

    qInfo() << "create dbus interface done";

    QDBusConnection::sessionBus().connect(
        DBUS_SERVICE_NAME,
        DBUS_SERVICE_PATH,
        DBUS_INTERFACE_NAME,
        "ScanningUsbPathsChanged",
        this,
        SLOT(scanningUsbPathsChanged(QStringList)));

    qInfo() << "start get usb scanning path";
    QStringList list = interface->property("ScanningUsbPaths").toStringList();
    foreach (const QString &p, list)
        scanningPaths << QUrl::fromLocalFile(p);

    qInfo() << "get usb scanning path done:" << scanningPaths;
}

void DefenderInterface::scanningUsbPathsChanged(QStringList list)
{
    qInfo() << "reveive signal: scanningUsbPathsChanged, " << list;
    scanningPaths.clear();
    foreach (const QString &p, list)
        scanningPaths << QUrl::fromLocalFile(p);
}

/*
 * ???????????????????????????url??????????????????url?????????????????????????????????????????????
 */
QList<QUrl> DefenderInterface::getScanningPaths(const QUrl &url)
{
    QList<QUrl> list;
    foreach (const QUrl &p, scanningPaths) {
        if (url.isParentOf(p) || url == p)
            list << p;
    }
    return list;
}


/*
 * ????????????url???url?????????????????????false???????????????
 */
bool DefenderInterface::stopScanning(const QUrl &url)
{
    QList<QUrl> urls;
    urls << url;
    return stopScanning(urls);
}

#define MAX_DBUS_TIMEOUT 1000 // ??????????????????????????????????????????, ????????????????????????
bool DefenderInterface::stopScanning(const QList<QUrl> &urls)
{
    qInfo() << "stopScanning:" << urls;
    qInfo() << "current scanning:" << scanningPaths;

    // ??????DBus??????
    start();

    QList<QUrl> paths;
    foreach(const QUrl &url, urls)
        paths << getScanningPaths(url);

    if (paths.empty())
        return true;

    foreach (const QUrl &path, paths) {
        qInfo() << "send RequestStopUsbScannig:" << path;
        interface->asyncCall("RequestStopUsbScannig", path.toLocalFile());
    }

    // ?????????????????????????????????????????????
    QTime t;
    t.start();
    while (t.elapsed() < MAX_DBUS_TIMEOUT) {
        qApp->processEvents();
        if (!isScanning(urls))
            return true;
        QThread::msleep(10);
    }
    return false;
}

/*
 * ???????????????????????????????????????url???url????????????
 */
bool DefenderInterface::isScanning(const QUrl &url)
{
    // ??????DBus??????
    start();

    QList<QUrl> paths = getScanningPaths(url);
    return !paths.empty();
}

bool DefenderInterface::isScanning(const QList<QUrl> &urls)
{
    foreach(const QUrl &url, urls) {
        if (isScanning(url))
            return true;
    }
    return false;
}
