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

#include "backgroundmanager.h"
#include "screen/screenhelper.h"
#include "util/util.h"

#include <qpa/qplatformwindow.h>
#include <QImageReader>

BackgroundManager::BackgroundManager(bool preview, QObject *parent)
    : QObject(parent)
    , windowManagerHelper(DWindowManagerHelper::instance())
    , m_preview(preview)
    , m_interface("com.yoyo.Settings",
                  "/Theme",
                  "com.yoyo.Theme",
                  QDBusConnection::sessionBus(), this)
{
    init();
    QDBusConnection::sessionBus().connect("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",  "NameOwnerChanged", this, SLOT(onWmDbusStarted(QString, QString, QString)));
}

BackgroundManager::~BackgroundManager()
{
    if (gsettings) {
        gsettings->deleteLater();
        gsettings = nullptr;
    }

    if (wmInter) {
        wmInter->deleteLater();
        wmInter = nullptr;
    }

    windowManagerHelper = nullptr;

    m_backgroundMap.clear();
    QDBusConnection::sessionBus().disconnect("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",  "NameOwnerChanged", this, SLOT(onWmDbusStarted(QString, QString, QString)));
}

void BackgroundManager::onRestBackgroundManager()
{
    //?????????????????????????????????????????????????????????????????????isEnable??????false???????????????????????????
    if (m_preview || isEnabled()) {
        if (wmInter) {
            return;
        }

        wmInter = new WMInter("com.deepin.wm", "/com/deepin/wm", QDBusConnection::sessionBus(), this);
        gsettings = new DGioSettings("com.deepin.dde.appearance", "", this);

        if (!m_preview) {
            connect(wmInter, &WMInter::WorkspaceSwitched, this, [this] (int, int to) {
                currentWorkspaceIndex = to;
                pullImageSettings();
                onResetBackgroundImage();
            });

            connect(gsettings, &DGioSettings::valueChanged, this, [this] (const QString & key, const QVariant & value) {
                Q_UNUSED(value);
                if (key == "background-uris") {
                    pullImageSettings();
                    onResetBackgroundImage();
                }
            });
        }

        //????????????
        connect(ScreenHelper::screenManager(), &AbstractScreenManager::sigScreenChanged,
                this, &BackgroundManager::onBackgroundBuild);
        disconnect(ScreenHelper::screenManager(), &AbstractScreenManager::sigScreenChanged,
                   this, &BackgroundManager::onSkipBackgroundBuild);

        //??????????????????
        connect(ScreenHelper::screenManager(), &AbstractScreenManager::sigDisplayModeChanged,
                this, &BackgroundManager::onBackgroundBuild);
        disconnect(ScreenHelper::screenManager(), &AbstractScreenManager::sigDisplayModeChanged,
                   this, &BackgroundManager::onSkipBackgroundBuild);

        //??????????????????
        connect(ScreenHelper::screenManager(), &AbstractScreenManager::sigScreenGeometryChanged,
                this, &BackgroundManager::onScreenGeometryChanged);

        //???????????????,?????????????????????????????????
//        connect(ScreenHelper::screenManager(), &AbstractScreenManager::sigScreenAvailableGeometryChanged,
//                this, &BackgroundManager::onScreenGeometryChanged);

        //????????????
        pullImageSettings();
        onBackgroundBuild();
    } else {

        // ????????????
        if (gsettings) {
            gsettings->deleteLater();
            gsettings = nullptr;
        }

        if (wmInter) {
            wmInter->deleteLater();
            wmInter = nullptr;
        }

        currentWorkspaceIndex = 0;

        //????????????
        connect(ScreenHelper::screenManager(), &AbstractScreenManager::sigScreenChanged,
                this, &BackgroundManager::onSkipBackgroundBuild);
        disconnect(ScreenHelper::screenManager(), &AbstractScreenManager::sigScreenChanged,
                   this, &BackgroundManager::onBackgroundBuild);

        //??????????????????
        connect(ScreenHelper::screenManager(), &AbstractScreenManager::sigDisplayModeChanged,
                this, &BackgroundManager::onSkipBackgroundBuild);
        disconnect(ScreenHelper::screenManager(), &AbstractScreenManager::sigDisplayModeChanged,
                   this, &BackgroundManager::onBackgroundBuild);

        //??????????????????????????????
        disconnect(ScreenHelper::screenManager(), &AbstractScreenManager::sigScreenGeometryChanged,
                   this, &BackgroundManager::onScreenGeometryChanged);
        disconnect(ScreenHelper::screenManager(), &AbstractScreenManager::sigScreenAvailableGeometryChanged,
                   this, &BackgroundManager::onScreenGeometryChanged);

        //????????????
        m_backgroundMap.clear();

        //???????????????
        onSkipBackgroundBuild();
    }
}

void BackgroundManager::onScreenGeometryChanged()
{
    bool changed = false;
    for (ScreenPointer sp : m_backgroundMap.keys()) {
        BackgroundWidgetPointer bw = m_backgroundMap.value(sp);
        qDebug() << "screen geometry change:" << sp.get() << bw.get();
        if (bw.get() != nullptr) {
            //bw->windowHandle()->handle()->setGeometry(sp->handleGeometry()); //????????????????????????widget???geometry????????????
            //fix bug32166 bug32205
            if (bw->geometry() == sp->geometry()) {
                qDebug() << "background geometry is equal to screen geometry,and discard changes" << bw->geometry();
                continue;
            }
            qInfo() << "background geometry change from" << bw->geometry() << "to" << sp->geometry()
                    << "screen name" << sp->name();
            bw->setGeometry(sp->geometry());
            changed = true;
        }
    }

    //????????????
    if (changed)
        onResetBackgroundImage();
}

void BackgroundManager::init()
{
    if (!m_preview) {
        connect(windowManagerHelper, &DWindowManagerHelper::windowManagerChanged,
                this, &BackgroundManager::onRestBackgroundManager);
        connect(windowManagerHelper, &DWindowManagerHelper::hasCompositeChanged,
                this, &BackgroundManager::onRestBackgroundManager);
    }
}

void BackgroundManager::onWallpaperChanged(QString path)
{
    pullImageSettings();
    emit wallpaperChanged();
}

void BackgroundManager::pullImageSettings()
{
    m_backgroundImagePath.clear();
    for (ScreenPointer sc : ScreenMrg->logicScreens()) {
        QString path = getBackgroundFromWm(sc->name());
        m_backgroundImagePath.insert(sc->name(), path);
    }
}

QString BackgroundManager::getBackgroundFromWm(const QString &screen)
{
    QString ret;
    
    if (m_interface.isValid()) {
        ret = m_interface.property("wallpaper").toString();
    }
    return ret;
}

QString BackgroundManager::getBackgroundFromWmConfig(const QString &screen)
{
    QString imagePath;

    QString homePath = QStandardPaths::standardLocations(QStandardPaths::HomeLocation).first();
    QFile wmFile(homePath + "/.config/deepinwmrc");
    if (wmFile.open(QIODevice::ReadOnly | QIODevice::Text)) {

        // ???????????????????????????????????????????????????
        while (!wmFile.atEnd()) {
            QString line = wmFile.readLine();
            int index = line.indexOf("@");
            int indexEQ = line.indexOf("=");
            if (index <= 0 || indexEQ <= index+1) {
                continue;
            }

            int workspaceIndex = line.left(index).toInt();
            QString screenName = line.mid(index+1, indexEQ-index-1);
            if (workspaceIndex != currentWorkspaceIndex || screenName != screen) {
                continue;
            }

            imagePath = line.mid(indexEQ+1).trimmed();
            break;
        }

        wmFile.close();
    }

    return imagePath;
}

QString BackgroundManager::getDefaultBackground() const
{
    //??????????????????
    QString defaultPath;
    if (gsettings) {
        for (const QString &path : gsettings->value("background-uris").toStringList()){
            if (path.isEmpty() || !QFile::exists(QUrl(path).toLocalFile())) {
                continue;
            }
            else {
                defaultPath = path;
                qInfo() << "default background path:" << path;
                break;
            }
        }
    }
    // ??????????????????
    if (defaultPath.isEmpty()) {
        defaultPath = QString("file:///usr/share/backgrounds/default_background.jpg");
    }
    return defaultPath;
}

BackgroundWidgetPointer BackgroundManager::createBackgroundWidget(ScreenPointer screen)
{
    BackgroundWidgetPointer bwp(new BackgroundWidget);
    bwp->setAccessableInfo(screen->name());
    bwp->setProperty("isPreview", m_preview);
    bwp->setProperty("myScreen", screen->name()); // assert screen->name is unique
    //bwp->createWinId();   //????????????4k??????bug
    //bwp->windowHandle()->handle()->setGeometry(screen->handleGeometry()); //????????????????????????widget???geometry????????????//?????????????????????
    bwp->setGeometry(screen->geometry()); //?????????????????????
    qDebug() << "screen name" << screen->name() << "geometry" << screen->geometry() << bwp.get();

    if (m_preview) {
        DesktopUtil::set_prview_window(bwp.data());
    } else {
        DesktopUtil::set_desktop_window(bwp.data());
    }

    return bwp;
}

bool BackgroundManager::isEnabled() const
{
    // ?????????kwin????????????????????????????????????
//    return windowManagerHelper->windowManagerName() == DWindowManagerHelper::KWinWM || !windowManagerHelper->hasComposite();
    return m_backgroundEnable;
}

void BackgroundManager::setVisible(bool visible)
{
    m_visible = visible;
    for (BackgroundWidgetPointer w : m_backgroundMap.values()) {
        w->setVisible(visible);
    }
}

bool BackgroundManager::isVisible() const
{
    return m_visible;
}

BackgroundWidgetPointer BackgroundManager::backgroundWidget(ScreenPointer sp) const
{
    return m_backgroundMap.value(sp);
}

void BackgroundManager::setBackgroundImage(const QString &screen, const QString &path)
{
    if (screen.isEmpty() || path.isEmpty())
        return;

    m_backgroundImagePath[screen] = path;
    onResetBackgroundImage();
}


void BackgroundManager::onBackgroundBuild()
{
    //??????????????????
    AbstractScreenManager::DisplayMode mode = ScreenMrg->lastChangedMode();
    qInfo() << "screen mode" << mode << "screen count" << ScreenMrg->screens().size();

    //???????????????
    if ((AbstractScreenManager::Showonly == mode) || (AbstractScreenManager::Duplicate == mode) //??????????????????
            || (ScreenMrg->screens().count() == 1)) {  //????????????

        ScreenPointer primary = ScreenMrg->primaryScreen();
        if (primary == nullptr) {
            qCritical() << "get primary screen failed return";
            //???????????????view??????
            m_backgroundMap.clear();
            emit sigBackgroundBuilded(mode);
            return;
        }

        BackgroundWidgetPointer bwp = m_backgroundMap.value(primary);
        m_backgroundMap.clear();
        if (!bwp.isNull()) {
            if (bwp->geometry() != primary->geometry())
                bwp->setGeometry(primary->geometry());
        } else {
            bwp = createBackgroundWidget(primary);
        }

        m_backgroundMap.insert(primary, bwp);

        //????????????
        onResetBackgroundImage();

        if (m_visible)
            bwp->show();
        else
            qWarning() << "Disable show the background widget, of screen:" << primary->name() << primary->geometry();
    } else { //??????
        auto screes = ScreenMrg->logicScreens();
        for (auto sp : m_backgroundMap.keys()) {
            if (!screes.contains(sp)) {
                auto rmd = m_backgroundMap.take(sp);
                qInfo() << "screen:" << rmd->property("myScreen") << "is invalid, delete it.";
            }
        }
        for (ScreenPointer sc : screes) {
            BackgroundWidgetPointer bwp = m_backgroundMap.value(sc);
            if (!bwp.isNull()) {
                if (bwp->geometry() != sc->geometry())
                    bwp->setGeometry(sc->geometry());
            } else {
                qInfo() << "screen:" << sc->name() << "  added, create it.";
                bwp = createBackgroundWidget(sc);
                m_backgroundMap.insert(sc, bwp);
            }

            if (m_visible)
                bwp->show();
            else
                qWarning() << "Disable show the background widget, of screen:" << sc->name() << sc->geometry();
        }

        onResetBackgroundImage();
    }

    //??????view??????
    emit sigBackgroundBuilded(mode);
}

void BackgroundManager::onSkipBackgroundBuild()
{
    //??????view??????
    emit sigBackgroundBuilded(ScreenMrg->lastChangedMode());
}

void BackgroundManager::onResetBackgroundImage()
{
    auto getPix = [](const QString & path, const QPixmap & defalutPixmap)->QPixmap{
        if (path.isEmpty())
            return defalutPixmap;

        QString currentWallpaper = path.startsWith("file:") ? QUrl(path).toLocalFile() : path;
        QPixmap backgroundPixmap(currentWallpaper);
        // fix whiteboard shows when a jpeg file with filename xxx.png
        // content formart not epual to extension
        if (backgroundPixmap.isNull()) {
            QImageReader reader(currentWallpaper);
            reader.setDecideFormatFromContent(true);
            backgroundPixmap = QPixmap::fromImage(reader.read());
        }
        return backgroundPixmap.isNull() ? defalutPixmap : backgroundPixmap;
    };

    QPixmap defaultImage;

    QMap<QString, QString> recorder; //?????????????????????
    for (ScreenPointer sp : m_backgroundMap.keys()) {
        QString userPath;
        if (!m_backgroundImagePath.contains(sp->name())) {
            userPath = getBackgroundFromWm(sp->name());
        } else {
            userPath = m_backgroundImagePath.value(sp->name());
        }

        if (!userPath.isEmpty())
            recorder.insert(sp->name(), userPath);

        QPixmap backgroundPixmap = getPix(userPath, defaultImage);
        if (backgroundPixmap.isNull()) {
            qCritical() << "screen " << sp->name() << "backfround path" << userPath
                        << "can not read!";
            continue;
        }

        BackgroundWidgetPointer bw = m_backgroundMap.value(sp);
        QSize trueSize = sp->handleGeometry().size(); //?????????????????????????????????
        auto pix = backgroundPixmap.scaled(trueSize,
                                           Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation);

        if (pix.width() > trueSize.width() || pix.height() > trueSize.height()) {
            pix = pix.copy(QRect(static_cast<int>((pix.width() - trueSize.width()) / 2.0),
                                 static_cast<int>((pix.height() - trueSize.height()) / 2.0),
                                 trueSize.width(),
                                 trueSize.height()));
        }

        qDebug() << sp->name() << "background path" << userPath << "truesize" << trueSize << "devicePixelRatio"
                 << bw->devicePixelRatioF() << pix << "widget" << bw.get();
        pix.setDevicePixelRatio(bw->devicePixelRatioF());
        bw->setPixmap(pix);
    }

    //????????????
    m_backgroundImagePath = recorder;
}

void BackgroundManager::onWmDbusStarted(QString name, QString oldOwner, QString newOwner)
{
    Q_UNUSED(oldOwner)
    Q_UNUSED(newOwner)
    //?????????????????????????????????????????????????????????????????????????????????
    if (name == QString("com.deepin.wm") && QDBusConnection::sessionBus().interface()->isServiceRegistered("com.deepin.wm")) {
        qInfo() << "dbus server com.deepin.wm started." << m_wmInited;
        if (!m_wmInited) {
            pullImageSettings();
            onResetBackgroundImage();
        }
        m_wmInited = true;
        QDBusConnection::sessionBus().disconnect("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",  "NameOwnerChanged", this, SLOT(onWmDbusStarted(QString, QString, QString)));
    }
}
