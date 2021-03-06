/*
 * Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
 *
 * Author:     zhangyu<zhangyub@uniontech.com>
 *
 * Maintainer: zhangyu<zhangyub@uniontech.com>
 *             wangchunlin<wangchunlin@uniontech.com>
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

#include "screenmanagerwayland.h"
#include "screenobjectwayland.h"
#include "abstractscreenmanager_p.h"
#include "dbus/dbusdisplay.h"
#include "dbus/dbusdock.h"
#include "dbus/dbusmonitor.h"

#include <QGuiApplication>
#include <QScreen>

#define SCREENOBJECT(screen) dynamic_cast<ScreenObjectWayland*>(screen)
ScreenManagerWayland::ScreenManagerWayland(QObject *parent)
    : AbstractScreenManager(parent)
{
    m_display = new DBusDisplay(this);
    init();
}

ScreenManagerWayland::~ScreenManagerWayland()
{
    if (m_display) {
        m_display->deleteLater();
        m_display = nullptr;
    }
}

ScreenPointer ScreenManagerWayland::primaryScreen()
{
    QString primaryName = m_display->primary();
    if (primaryName.isEmpty())
        qCritical() << "get primary name failed";

    ScreenPointer ret;
    for (ScreenPointer sp : m_screens.values()) {
        if (sp->name() == primaryName) {
            ret = sp;
            break;
        }
    }
    if (ret.isNull())
            qWarning() << "get primary from dbus:" <<primaryName << ".save monitors:" << m_screens.keys();
    return ret;
}

QVector<ScreenPointer> ScreenManagerWayland::screens() const
{
    QVector<ScreenPointer> order;
    for (const QDBusObjectPath &path : m_display->monitors()) {
        if (m_screens.contains(path.path())) {
            ScreenPointer sp = m_screens.value(path.path());
            ScreenObjectWayland *screen = SCREENOBJECT(sp.data());
            if (screen) {
                if (screen->enabled())
                    order.append(sp);
            } else
                order.append(sp);
        } else {
            qWarning() << "unknow monitor:" << path.path() << ".save monitors:" << m_screens.keys();
        }
    }
    return order;
}

QVector<ScreenPointer> ScreenManagerWayland::logicScreens() const
{
    QVector<ScreenPointer> order;
    QString primaryName = m_display->primary();
    if (primaryName.isEmpty())
        qCritical() << "get primary name failed";

    //????????????????????????
    for (const QDBusObjectPath &path : m_display->monitors()) {
        if (path.path().isEmpty()) {
            qWarning() << "monitor: QDBusObjectPath is empty";
            continue;
        }

        if (m_screens.contains(path.path())) {
            ScreenPointer sp = m_screens.value(path.path());
            if (sp == nullptr) {
                qCritical() << "get scrreen failed path" << path.path();
                continue;
            }
            if (sp->name() == primaryName) {
                order.push_front(sp);
            } else {
                ScreenObjectWayland *screen = SCREENOBJECT(sp.data());
                if (screen) {
                    if (screen->enabled())
                        order.push_back(sp);
                } else
                    order.push_back(sp);
            }
        }
    }
    return order;
}

ScreenPointer ScreenManagerWayland::screen(const QString &name) const
{
    ScreenPointer ret;
    auto screens = m_screens.values();
    auto iter = std::find_if(screens.begin(), screens.end(), [name](const ScreenPointer & sp) {
        return sp->name() == name;
    });

    if (iter != screens.end()) {
        ret = *iter;
    }

    return ret;
}

qreal ScreenManagerWayland::devicePixelRatio() const
{
    //dbus??????????????????????????????????????????????????????????????????QT?????????
    return qApp->primaryScreen()->devicePixelRatio();
}

AbstractScreenManager::DisplayMode ScreenManagerWayland::displayMode() const
{
    auto pending = m_display->GetRealDisplayMode();
    pending.waitForFinished();
    if (pending.isError()) {
        qWarning() << "Display GetRealDisplayMode Error:" << pending.error().name() << pending.error().message();
        AbstractScreenManager::DisplayMode ret = AbstractScreenManager::DisplayMode(m_display->displayMode());
        return ret;
    } else {
        /*
        DisplayModeMirror: 1
        DisplayModeExtend: 2
        DisplayModeOnlyOne: 3
        DisplayModeUnknow: 4
        */
        int mode = pending.argumentAt(0).toInt();
        qDebug() << "GetRealDisplayMode resulet" << mode;
        if (mode > 0 && mode < 4)
            return static_cast<AbstractScreenManager::DisplayMode>(mode);
        else
            return AbstractScreenManager::Custom;
    }
}

AbstractScreenManager::DisplayMode ScreenManagerWayland::lastChangedMode() const
{
    return static_cast<AbstractScreenManager::DisplayMode>(m_lastMode);
}

void ScreenManagerWayland::reset()
{
    if (m_display) {
        delete m_display;
        m_display = nullptr;
    }

    m_display = new DBusDisplay(this);
    init();
}

void ScreenManagerWayland::processEvent()
{
    //????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
    if (d->m_events.contains(AbstractScreenManager::Mode)) {
        emit sigDisplayModeChanged();
    }
    else if (d->m_events.contains(AbstractScreenManager::Screen)) {
        emit sigScreenChanged();
    }
    else if (d->m_events.contains(AbstractScreenManager::Geometry)) {
        emit sigScreenGeometryChanged();
    }
    else if (d->m_events.contains(AbstractScreenManager::AvailableGeometry)) {
        emit sigScreenAvailableGeometryChanged();
    }
}

void ScreenManagerWayland::onMonitorChanged()
{
    QStringList monitors;
    //?????????????????????
    for (auto objectPath : m_display->monitors()) {
        QString path = objectPath.path();
        if (path.isEmpty()) {
            qWarning() << "get monitor path is empty from display";
            continue;
        }

        //?????????
        if (!m_screens.contains(path)) {
            ScreenPointer sp(new ScreenObjectWayland(new DBusMonitor(path)));
            m_screens.insert(path, sp);
            connectScreen(sp);
            qInfo() << "add monitor:" << path;
        }
        monitors << path;
    }
    qDebug() << "get monitors:" << monitors;

    //?????????????????????
    for (const QString &path : m_screens.keys()) {
        if (!monitors.contains(path)) {
            ScreenPointer sp = m_screens.take(path);
            disconnectScreen(sp);
            qInfo() << "del monitor:" << path;
        }
    }
    qDebug() << "save monitors:" << m_screens.keys();
    appendEvent(Screen);
}

void ScreenManagerWayland::onDockChanged()
{
#ifdef UNUSED_SMARTDOCK
    auto screen = primaryScreen();
    if (screen == nullptr) {
        qCritical() << "primaryScreen() return nullptr!!!";
        return;
    }
    emit sigScreenAvailableGeometryChanged(screen, screen->availableGeometry());
#else
    //????????????dock????????????dock???????????????????????????,???????????????
    //emit sigScreenAvailableGeometryChanged(nullptr, QRect());
    appendEvent(AvailableGeometry);
#endif
}

void ScreenManagerWayland::onScreenGeometryChanged(const QRect &rect)
{
    Q_UNUSED(rect)
    appendEvent(Geometry);

    //fix wayland???????????????/?????????????????????geometry???????????????????????????????????????????????????PrimaryRectChanged?????????????????????????????????
    //???????????????????????????????????????????????????????????????
    emit m_display->PrimaryRectChanged();
}

void ScreenManagerWayland::init()
{
    m_screens.clear();

    //???????????????Qt??????????????????????????????DBUS?????????
    connect(qApp, &QGuiApplication::screenAdded, this, &ScreenManagerWayland::onMonitorChanged);
    connect(m_display, &DBusDisplay::MonitorsChanged, this, &ScreenManagerWayland::onMonitorChanged);
    connect(m_display, &DBusDisplay::PrimaryChanged, this, [this]() {
        this->appendEvent(Screen);
    });
#ifdef UNUSE_TEMP
    connect(m_display, &DBusDisplay::DisplayModeChanged, this, &AbstractScreenManager::sigDisplayModeChanged);
#else
    //???????????????
    connect(m_display, &DBusDisplay::DisplayModeChanged, this, [this]() {
        //emit sigDisplayModeChanged();
        int mode = m_display->GetRealDisplayMode();
        qInfo() << "deal display mode changed " << mode;
        if (m_lastMode == mode)
            return;
        m_lastMode = mode;
        this->appendEvent(Mode);
    });

    //?????????????????????PrimaryRectChanged??????????????????/????????????
    connect(m_display, &DBusDisplay::PrimaryRectChanged, this, [this]() {
        int mode = m_display->GetRealDisplayMode();
        qInfo() << "deal merge and split" << mode << m_lastMode;
        if (m_lastMode == mode)
            return;
        m_lastMode = mode;
        //emit sigDisplayModeChanged();
        this->appendEvent(Mode);
    });

    m_lastMode = m_display->GetRealDisplayMode();
#endif

    //dock?????????
    connect(DockInfoIns, &DBusDock::FrontendWindowRectChanged, this, &ScreenManagerWayland::onDockChanged);
    connect(DockInfoIns, &DBusDock::HideModeChanged, this, &ScreenManagerWayland::onDockChanged);
    //connect(DockInfoIns,&DBusDock::PositionChanged,this, &ScreenManagerWayland::onDockChanged);???????????????????????????bug#25148??????????????????????????????

    //???????????????
    for (auto objectPath : m_display->monitors()) {
        const QString path = objectPath.path();
        ScreenPointer sp(new ScreenObjectWayland(new DBusMonitor(path)));
        m_screens.insert(path, sp);
        connectScreen(sp);
    }
}

void ScreenManagerWayland::connectScreen(ScreenPointer sp)
{
    connect(sp.get(), &AbstractScreen::sigGeometryChanged, this,
            &ScreenManagerWayland::onScreenGeometryChanged);
}

void ScreenManagerWayland::disconnectScreen(ScreenPointer sp)
{
    disconnect(sp.get(), &AbstractScreen::sigGeometryChanged, this,
               &ScreenManagerWayland::onScreenGeometryChanged);
}
