/*
 * Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
 *
 * Author:     wangchunlin<wangchunlin@uniontech.com>
 *
 * Maintainer: wangchunlin<wangchunlin@uniontech.com>
 *             xinglinkun<xinglinkun@uniontech.com>
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
#include "frame.h"
#include "checkbox.h"
#include "constants.h"
#include "wallpaperlist.h"
#include "wallpaperitem.h"
#include "dbus/deepin_wm.h"
#include "thumbnailmanager.h"
#include "appearance_interface.h"
#include "screen/screenhelper.h"
#include "dbusinterface/introspectable_interface.h"
#include "screensavercontrol.h"
#include "button.h"
#include "dfileservices.h"
#include "desktopinfo.h"
#include "waititem.h"
#ifndef DISABLE_SCREENSAVER
#include "screensaver_interface.h"
#endif
#include "dfileservices.h"

#include <DButtonBox>
#include <DIconButton>
#include <DWindowManagerHelper>
#include <DSysInfo>

#include <QApplication>
#include <QDesktopWidget>
#include <QKeyEvent>
#include <QDebug>
#include <QPainter>
#include <QScrollBar>
#include <QScreen>
#include <QVBoxLayout>
#include <QCheckBox>
#include <DApplicationHelper>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QLabel>

#define DESKTOP_BUTTON_ID "desktop"
#define LOCK_SCREEN_BUTTON_ID "lock-screen"
#define DESKTOP_AND_LOCKSCREEN_BUTTON_ID "desktop-lockscreen"
#define SCREENSAVER_BUTTON_ID "screensaver"
#define SessionManagerService "com.deepin.SessionManager"
#define SessionManagerPath "/com/deepin/SessionManager"


#define BUTTON_NARROW_WIDTH     79
#define BUTTON_WIDE_WIDTH       164

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE
DCORE_USE_NAMESPACE
DFM_USE_NAMESPACE
using namespace com::deepin;
static bool previewBackground()
{
    if (DWindowManagerHelper::instance()->windowManagerName() == DWindowManagerHelper::DeepinWM)
        return false;

    return DWindowManagerHelper::instance()->windowManagerName() == DWindowManagerHelper::KWinWM
           || !DWindowManagerHelper::instance()->hasBlurWindow();
}

//????????????????????????
static const QByteArrayList array_policy {"30", "60", "300", "600", "900", "1800", "3600", "login", "wakeup"};

Frame::Frame(QString screenName, Mode mode, QWidget *parent)
    : DBlurEffectWidget(parent)
    , m_sessionManagerInter(new SessionManager(SessionManagerService,
                                               SessionManagerPath,
                                               QDBusConnection::sessionBus(),this))
    , m_mode(mode)
    , m_wallpaperList(new WallpaperList(this))
    , m_closeButton(new DIconButton(this))
    , m_dbusWmInter (new WMInter("com.deepin.wm", "/com/deepin/wm", QDBusConnection::sessionBus(), this))
    , m_dbusAppearance(new ComDeepinDaemonAppearanceInterface(AppearanceServ,
                                                              AppearancePath,
                                                              QDBusConnection::sessionBus(),
                                                              this))    
    , m_mouseArea(new DRegionMonitor(this))
    , m_screenName(screenName)
{
    m_mouseArea->setCoordinateType(DRegionMonitor::Original);

    setFocusPolicy(Qt::NoFocus);
    setWindowFlags(Qt::BypassWindowManagerHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    if (DesktopInfo().waylandDectected()){
        winId();
        auto win = windowHandle();
        if (win){
            qDebug() << "set wayland role override";
            win->setProperty("_d_dwayland_window-type","wallpaper-set");
        }else {
            qCritical() << "wayland role error,windowHandle is nullptr!";
        }
    }
    setBlendMode(DBlurEffectWidget::BehindWindowBlend);
    initUI();
    initSize();

    connect(m_mouseArea, &DRegionMonitor::buttonPress, this, [this](const QPoint & p, const int button) {
        if (button == 4) {
            m_wallpaperList->prevPage();
        } else if (button == 5) {
            m_wallpaperList->nextPage();
        } else {
            qreal scale = devicePixelRatioF();
            if(!ScreenHelper::screenManager()->screen(m_screenName)){
                qCritical() << "lost screen " << m_screenName << "closed";
                return ;
            }
            const QRect sRect = ScreenHelper::screenManager()->screen(m_screenName)->geometry();
            QRect nativeRect = geometry();

            // ?????????????????????geometry
            nativeRect.moveTopLeft((nativeRect.topLeft() - sRect.topLeft()) * scale + sRect.topLeft());
            nativeRect.setSize(nativeRect.size() * scale);

            if (!nativeRect.contains(p)) {
                qDebug() << "button pressed on blank area quit.";
                hide();
            }

            activateWindow();
        }
    });

    connect(ScreenMrg, &AbstractScreenManager::sigScreenGeometryChanged,this,[this](){
        initSize();
        if (!isHidden())
            m_wallpaperList->updateItemThumb();

        qDebug() << "reset geometry" << this->isVisible() << this->geometry();
        this->activateWindow();
    });

    m_closeButton->hide();
    connect(m_wallpaperList, &WallpaperList::mouseOverItemChanged,
            this, &Frame::handleNeedCloseButton);
    connect(m_wallpaperList, &WallpaperList::itemPressed,
            this, &Frame::onItemPressed);
    connect(m_sessionManagerInter, &SessionManager::LockedChanged, this, [this](bool locked){
        this->setVisible(!locked);
    });

    loading();
    m_loadTimer.setSingleShot(true);
    connect(&m_loadTimer,&QTimer::timeout,this,[this](){
        this->refreshList();
    });
}

Frame::~Frame()
{
    QStringList list = m_needDeleteList;
    QTimer::singleShot(1000, [list]() {
        ComDeepinDaemonAppearanceInterface dbusAppearance(AppearanceServ,
                                                          AppearancePath,
                                                          QDBusConnection::sessionBus(),
                                                          nullptr);

        for (const QString &path : list) {
            dbusAppearance.Delete("background", path);
            qDebug() << "delete background" << path;
        }
    });

    disconnect(ScreenMrg, nullptr,this,nullptr);
    //????????????
    if (m_itemwait) {
        delete m_itemwait;
        m_itemwait = nullptr;
    }

    if (m_toolLayout) {
        delete m_toolLayout;
        m_toolLayout = nullptr;
    }

    if (m_wallpaperCarouselLayout) {
        delete m_wallpaperCarouselLayout;
        m_wallpaperCarouselLayout = nullptr;
    }
}

void Frame::show()
{
    if (previewBackground()) {
        if (m_dbusDeepinWM) {
            // ????????????????????????
            m_dbusDeepinWM->deleteLater();
            m_dbusDeepinWM = nullptr;
        }

        if (!m_backgroundManager) {
            m_backgroundManager = new BackgroundManager(true, this);

            connect(m_backgroundManager, SIGNAL(sigBackgroundBuilded(int)),this,SLOT(onRest()));
        }
    } else if (!m_dbusDeepinWM) {
        if (m_backgroundManager) {
            // ????????????????????????
            m_backgroundManager->deleteLater();
            m_backgroundManager = nullptr;
        }

        m_dbusDeepinWM = new DeepinWM(DeepinWMServ,
                                      DeepinWMPath,
                                      QDBusConnection::sessionBus(),
                                      this);
    }

    if (m_dbusDeepinWM)
        m_dbusDeepinWM->RequestHideWindows();

    m_mouseArea->registerRegion();

    DBlurEffectWidget::show();
}

void Frame::hide()
{
    DBlurEffectWidget::hide();
}

void Frame::setMode(Frame::Mode mode)
{
    if (m_mode == mode)
        return;

    if (m_mode == ScreenSaverMode) {
        if (m_backgroundManager) {
            m_backgroundManager->setVisible(true);
        }
#ifndef DISABLE_SCREENSAVER
        m_dbusScreenSaver->Stop();
#endif
    }

#ifndef DISABLE_SCREENSAVER
    m_mode = mode;
    reLayoutTools();
#endif
    refreshList();
}

QPair<QString, QString> Frame::desktopBackground() const
{
    return QPair<QString, QString>(m_screenName, m_cureentWallpaper);
}

void Frame::handleNeedCloseButton(QString path, QPoint pos)
{
    if (!path.isEmpty()) {
        m_closeButton->adjustSize();
        m_closeButton->move(pos.x() - 10, pos.y() - 10);
        m_closeButton->show();
        m_closeButton->disconnect();

        connect(m_closeButton, &DIconButton::clicked, this, [this, path] {
            m_dbusAppearance->Delete("background", path); // ??????????????????????????????????????????
            m_needDeleteList << path;
            m_wallpaperList->removeWallpaper(path);
            m_closeButton->hide();
        }, Qt::UniqueConnection);
    } else {
        m_closeButton->hide();
    }
}

void Frame::onRest()
{
    ScreenPointer screen = ScreenMrg->screen(m_screenName);
    if (m_backgroundManager){
        BackgroundWidgetPointer bwp = m_backgroundManager->backgroundWidget(screen);
        if (bwp){
            m_screenName = screen->name();
            bwp->lower();
            initSize();
            if (!isHidden())
                m_wallpaperList->updateItemThumb();
            raise();
            qDebug() << "onRest frame" << this->isVisible() << this->geometry();
            this->activateWindow();
            return;
        }
    }

    qDebug() << m_screenName << "lost exit!";
    close();
}

void Frame::showEvent(QShowEvent *event)
{
#ifndef DISABLE_SCREENSAVER
    m_switchModeControl->adjustSize();
#endif

    activateWindow();

    QTimer::singleShot(1, this, &Frame::refreshList);

    DBlurEffectWidget::showEvent(event);
}

void Frame::hideEvent(QHideEvent *event)
{
    DBlurEffectWidget::hideEvent(event);

    if (m_dbusDeepinWM)
        m_dbusDeepinWM->CancelHideWindows();
    m_mouseArea->unregisterRegion();

    if (m_mode == WallpaperMode) {
        if (!m_cureentWallpaper.isEmpty() && m_dbusDeepinWM)
            m_dbusDeepinWM->SetTransientBackground("");

        if (ThumbnailManager *manager = ThumbnailManager::instance(devicePixelRatioF()))
            manager->stop();
    }
#ifndef DISABLE_SCREENSAVER
    else if (m_mode == ScreenSaverMode) {
        m_dbusScreenSaver->Stop();
    }
#endif

    // ????????????
    if (m_dbusDeepinWM) {
        m_dbusDeepinWM->deleteLater();
        m_dbusDeepinWM = nullptr;
    }

    if (m_backgroundManager) {
        m_backgroundManager->setVisible(false);
        m_backgroundManager->deleteLater();
        m_backgroundManager = nullptr;
    }

    emit done();
}

void Frame::keyPressEvent(QKeyEvent *event)
{
    QWidgetList widgetList; //??????????????????????????????
    //??????????????????
    if (m_mode == Mode::WallpaperMode) {
        widgetList << m_wallpaperCarouselCheckBox; //????????????????????????
        //?????????????????????
        if (m_wallpaperCarouselControl->isVisible()){
            for (QAbstractButton *button: m_wallpaperCarouselControl->buttonList()) {
                widgetList << qobject_cast<QWidget *>(button);
            }
        }
    }
    //??????????????????
#ifndef DISABLE_SCREENSAVER
    else {
        //?????????????????????
        for (QAbstractButton *button: m_waitControl->buttonList()) {
            widgetList << qobject_cast<QWidget *>(button);
        }
        //????????????????????????
        widgetList << m_lockScreenBox;
    }
#endif

    if (!widgetList.contains(focusWidget())) {
        widgetList.clear();

        //?????????????????????
#ifndef DISABLE_SCREENSAVER
        for (QAbstractButton *button: m_switchModeControl->buttonList()) {
            widgetList << qobject_cast<QWidget *>(button);
        }
#endif
    }

    switch(event->key())
    {
    case Qt::Key_Escape:
        hide();
        qDebug() << "escape key pressed, quit.";
        break;
    case Qt::Key_Right:
        qDebug() << "Right";
        //????????????????????????
        if(widgetList.indexOf(focusWidget(),0) < widgetList.count()-1) {
            widgetList.at(widgetList.indexOf(focusWidget(),0)+1)->setFocus();
        }
        break;
    case Qt::Key_Left:
        qDebug() << "Left";
        //????????????????????????
        if (widgetList.indexOf(focusWidget(),0) > 0) {
            widgetList.at(widgetList.indexOf(focusWidget(),0)-1)->setFocus();
        }
        break;
    default:
        DBlurEffectWidget::keyPressEvent(event);
        break;
    }
}

void Frame::paintEvent(QPaintEvent *event)
{
    DBlurEffectWidget::paintEvent(event);

    QPainter pa(this);

    pa.setCompositionMode(QPainter::CompositionMode_SourceOut);
    pa.setPen(QPen(QColor(255, 255, 255, 20), 1));
    pa.drawLine(QPoint(0, 0), QPoint(width(), 0));
}

bool Frame::event(QEvent *event)
{
#ifndef DISABLE_SCREENSAVER
    if (event->type() == QEvent::LayoutRequest
            || event->type() == QEvent::Resize) {
        adjustModeSwitcherPoint();
    }
#endif
    return DBlurEffectWidget::event(event);
}

bool Frame::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *key = static_cast<QKeyEvent *>(event);
        qDebug() << "keyPress";
        if(!key) {
            qDebug()<<"key is null.";
            return false;
        }

        if (key->key() == Qt::Key_Tab) {
            qDebug() << "Tab";
#ifndef DISABLE_SCREENSAVER
            //?????????????????????tab?????????????????????????????????????????????????????????
            if (object == m_wallpaperCarouselCheckBox
                    || m_wallpaperCarouselControl->buttonList().contains(qobject_cast<QAbstractButton*>(object))
                    || object == m_lockScreenBox
                    || m_waitControl->buttonList().contains(qobject_cast<QAbstractButton*>(object))) {
                m_switchModeControl->buttonList().first()->setFocus();
                return  true;
            }
            //???????????????tab??????????????????????????????????????????WallpaperItem????????????????????????Button??????
            if (m_switchModeControl->buttonList().contains(qobject_cast<QAbstractButton*>(object))) {
                if (nullptr == m_wallpaperList->getCurrentItem()) {
                    return false;
                }
                QList<Button *> childButtons = m_wallpaperList->getCurrentItem()->findChildren<Button *>();
                if (!childButtons.isEmpty()) {
                    childButtons.first()->setFocus();
                    return true;
                }
            }
            //???????????????tab????????????????????????????????????WallpaperItem???tab??????????????????
#else
            //?????????????????????tab???????????????????????????????????????????????????
            if (object == m_wallpaperCarouselCheckBox
                    || m_wallpaperCarouselControl->buttonList().contains(qobject_cast<QAbstractButton*>(object))) {
                if (nullptr == m_wallpaperList->getCurrentItem()) {
                    return false;
                }
                QList<Button *> childButtons = m_wallpaperList->getCurrentItem()->findChildren<Button *>();
                if (!childButtons.isEmpty()) {
                    childButtons.first()->setFocus();
                    return true;
                }
            }
#endif
        }
        else if (key->key() == Qt::Key_Backtab) { //BackTab
            qDebug() << "BackTab(Shift Tab)";
#ifndef DISABLE_SCREENSAVER
            //?????????????????????backtab??????????????????????????????????????????????????????WallpaperItem????????????????????????Button??????
            if (object == m_wallpaperCarouselCheckBox
                    || m_wallpaperCarouselControl->buttonList().contains(qobject_cast<QAbstractButton*>(object))
                    || object == m_lockScreenBox
                    || m_waitControl->buttonList().contains(qobject_cast<QAbstractButton*>(object))) {
                if (nullptr == m_wallpaperList->getCurrentItem()) {
                    return false;
                }
                QList<Button *> childButtons = m_wallpaperList->getCurrentItem()->findChildren<Button *>();
                if (!childButtons.isEmpty()) {
                    childButtons.first()->setFocus();
                    return true;
                }
            }

            //???????????????backtab????????????????????????????????????WallpaperItem???backtab??????????????????

            //?????????????????????backtab?????????????????????????????????????????????????????????
            if (m_switchModeControl->buttonList().contains(qobject_cast<QAbstractButton*>(object))) {
                if (m_mode == WallpaperMode) {
                    m_wallpaperCarouselCheckBox->setFocus();
                } else {
                    m_waitControl->buttonList().first()->setFocus();
                }
                return true;
            }
#else
            if (object == m_wallpaperCarouselCheckBox
                    || m_wallpaperCarouselControl->buttonList().contains(qobject_cast<QAbstractButton*>(object))) {
                if (nullptr == m_wallpaperList->getCurrentItem()) {
                    return false;
                }
                QList<Button *> childButtons = m_wallpaperList->getCurrentItem()->findChildren<Button *>();
                if (!childButtons.isEmpty()) {
                    childButtons.first()->setFocus();
                    return true;
                }
            }
#endif
        }

#ifndef DISABLE_SCREENSAVER
        else if (key->key() == Qt::Key_Right || key->key() == Qt::Key_Left) { //[Q]CheckBox?????????????????????Tab?????????
            //if (QString(object->metaObject()->className()) == "CheckBox") { //????????????????????????????????????????????????????????????
            if (object == m_wallpaperCarouselCheckBox || object == m_lockScreenBox){
                keyPressEvent(key);
                return true;
            }
            else {
                return false;
            }
        }
#else
            else if (key->key() == Qt::Key_Right || key->key() == Qt::Key_Left) {
                if (object == m_wallpaperCarouselCheckBox){
                    keyPressEvent(key);
                    return true;
                }
                else {
                    return false;
                }
            }
#endif
        else if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {  //bug#30790???????????????
            return true;
        }
        else {
            return false;
        }
    }
    return  false; //????????????????????????
}

#ifndef DISABLE_SCREENSAVER
void Frame::setMode(QAbstractButton *toggledBtn, bool on)
{
    Q_UNUSED(on);

    int mode = m_switchModeControl->buttonList().indexOf(toggledBtn);
    setMode(Mode(mode));
}

void Frame::reLayoutTools()
{
    if (m_mode == ScreenSaverMode) {
        m_waitControlLabel->show();
        m_waitControl->show();
        m_lockScreenBox->show();
#ifndef DISABLE_WALLPAPER_CAROUSEL
        m_wallpaperCarouselCheckBox->hide();
        m_wallpaperCarouselControl->hide();
        layout()->removeItem(m_wallpaperCarouselLayout);
        static_cast<QBoxLayout *>(layout())->insertLayout(0, m_toolLayout);
#endif
    } else {
        m_waitControlLabel->hide();
        m_waitControl->hide();
        m_lockScreenBox->hide();
#ifndef DISABLE_WALLPAPER_CAROUSEL
        // ????????????????????????????????????????????????
        if (DSysInfo::deepinType() != DSysInfo::DeepinServer) {
            m_wallpaperCarouselCheckBox->show();
            m_wallpaperCarouselControl->setVisible(m_wallpaperCarouselCheckBox->isChecked());
        }
        layout()->removeItem(m_toolLayout);
        static_cast<QBoxLayout *>(layout())->insertLayout(0, m_wallpaperCarouselLayout);
#endif
    }
}
#endif

#if !defined(DISABLE_SCREENSAVER) || !defined(DISABLE_WALLPAPER_CAROUSEL)
void Frame::adjustModeSwitcherPoint()
{
    // ?????????????????????????????????
#ifndef DISABLE_SCREENSAVER
    m_switchModeControl->adjustSize();
#endif

    // ??????????????????????????????????????????????????????layout???sizeHint
    int tools_width = 0;

#ifndef DISABLE_SCREENSAVER
    {
        auto tools_layout_margins = m_toolLayout->contentsMargins();
        int width = m_waitControlLabel->sizeHint().width() +
                    m_waitControl->sizeHint().width() +
                    m_lockScreenBox->sizeHint().width();

        tools_width = tools_layout_margins.left() + width +
                      m_toolLayout->count() * m_toolLayout->spacing();
    }
#endif

#ifndef DISABLE_WALLPAPER_CAROUSEL
    {
        int width = m_wallpaperCarouselCheckBox->sizeHint().width() +
                    m_wallpaperCarouselControl->sizeHint().width() +
                    m_wallpaperCarouselLayout->contentsMargins().left() +
                    m_wallpaperCarouselLayout->contentsMargins().right() +
                    m_wallpaperCarouselLayout->spacing();

        bool visble = m_wallpaperCarouselControl->isVisible();
        if (visble && width > tools_width) {
            tools_width = width;
        } else if (!visble && m_mode == WallpaperMode) {
            tools_width = 0; // ?????????????????????????????????????????? ????????????
        }
    }
#endif

#ifndef DISABLE_SCREENSAVER
    // ?????????????????????????????????????????????????????????????????????
    int x = width() / 2 - m_switchModeControl->width() / 2;
    if (x < tools_width) {
        //???????????????V20????????????????????????????????????8?????????????????????????????????????????????????????????????????????????????????????????????????????????
        x = width()  - m_switchModeControl->width() - 5;
    }

    m_switchModeControl->move(x, (m_wallpaperList->y() - m_switchModeControl->height()) / 2);
#endif
}
#endif

static QString timeFormat(int second)
{
    quint8 s = static_cast<quint8>(second % 60);
    quint8 m = static_cast<quint8>(second / 60);
    quint8 h = m / 60;
    quint8 d = h / 24;

    m = m % 60;
    h = h % 24;

    QString time_string;

    if (d > 0) {
        time_string.append(QString::number(d)).append("d");
    }

    if (h > 0) {
        if (!time_string.isEmpty()) {
            time_string.append(' ');
        }

        time_string.append(QString::number(h)).append("h");
    }

    if (m > 0) {
        if (!time_string.isEmpty()) {
            time_string.append(' ');
        }

        time_string.append(QString::number(m)).append("m");
    }

    if (s > 0 || time_string.isEmpty()) {
        if (!time_string.isEmpty()) {
            time_string.append(' ');
        }

        time_string.append(QString::number(s)).append("s");
    }

    return time_string;
}

void Frame::initUI()
{
    m_closeButton->setIcon(QIcon::fromTheme("dfm_close_round_normal"));
    m_closeButton->setFixedSize(24, 24);
    m_closeButton->setIconSize({24, 24});
    m_closeButton->setFlat(true);
    m_closeButton->setFocusPolicy(Qt::NoFocus);

    QVBoxLayout *layout = new QVBoxLayout(this);

    layout->setMargin(0);
    layout->setSpacing(0);

    DPalette pal = DApplicationHelper::instance()->palette(this);
    QColor textColor = pal.color(QPalette::Normal, QPalette::BrightText);

#ifndef DISABLE_WALLPAPER_CAROUSEL
    m_wallpaperCarouselLayout = new QHBoxLayout;
    m_wallpaperCarouselCheckBox = new CheckBox(tr("Wallpaper Slideshow"), this);
    m_wallpaperCarouselCheckBox->installEventFilter(this); //??????Tab????????????
    // ????????????????????????????????????????????????
    if (DSysInfo::deepinType() == DSysInfo::DeepinServer) {
        m_wallpaperCarouselCheckBox->setChecked(false);
        m_wallpaperCarouselCheckBox->setEnabled(false);
        m_wallpaperCarouselCheckBox->setVisible(false);
    } else {
        m_wallpaperCarouselCheckBox->setChecked(true);
    }
    QPalette wccPal = m_wallpaperCarouselCheckBox->palette();
    wccPal.setColor(QPalette::All, QPalette::WindowText, textColor);
    m_wallpaperCarouselCheckBox->setPalette(wccPal);
    m_wallpaperCarouselControl = new DButtonBox(this);
    m_wallpaperCarouselControl->installEventFilter(this);
    QList<DButtonBoxButton *> wallpaperSlideshowBtns;
    m_wallpaperCarouselCheckBox->setFocusPolicy(Qt::StrongFocus);
    m_wallpaperCarouselControl->setFocusPolicy(Qt::NoFocus);
    qDebug() << "DSysInfo::deepinType = " << QString::number(DSysInfo::DeepinProfessional);

    {
        QString slideShow = getWallpaperSlideShow();
        int currentPolicyIndex = array_policy.indexOf(slideShow.toLatin1());
#if 0   //!????????????????????????????????????????????????????????????????????????????????????
        // ???????????????????????????????????????
        if (current_policy_index < 0) {
            const QString &policy = slideShow;

            if (!policy.isEmpty()) {
                array_policy.prepend(policy.toLatin1());
            } else {
                m_wallpaperCarouselCheckBox->setChecked(false);
            }

            // wallpaper change default per 10 minutes
            current_policy_index = 3;
        }
#else
        if (currentPolicyIndex < 0) {
            m_wallpaperCarouselCheckBox->setChecked(false);
            currentPolicyIndex = 3;
        }
#endif
        for (const QByteArray &time : array_policy) {
            DButtonBoxButton *btn;
            if (time == "login") {
                btn = new DButtonBoxButton(tr("When login"), this);
            } else if (time == "wakeup") {
                btn = new DButtonBoxButton(tr("When wakeup"), this);
            } else {
                bool ok = false;
                int t = time.toInt(&ok);
                btn = new DButtonBoxButton(ok ? timeFormat(t) : time, this);
            }

            btn->installEventFilter(this);
            btn->setMinimumWidth(40);
            wallpaperSlideshowBtns.append(btn);
        }

        m_wallpaperCarouselControl->setButtonList(wallpaperSlideshowBtns, true);
        wallpaperSlideshowBtns[currentPolicyIndex]->setChecked(true);
        m_wallpaperCarouselControl->setVisible(m_wallpaperCarouselCheckBox->isChecked());
    }

    m_wallpaperCarouselLayout->setSpacing(10);
    m_wallpaperCarouselLayout->setContentsMargins(20, 5, 20, 5);
    m_wallpaperCarouselLayout->addWidget(m_wallpaperCarouselCheckBox);
    m_wallpaperCarouselLayout->addWidget(m_wallpaperCarouselControl);
    m_wallpaperCarouselLayout->addItem(new QSpacerItem(1, HeaderSwitcherHeight));
    m_wallpaperCarouselLayout->addStretch();

    layout->addLayout(m_wallpaperCarouselLayout);

    connect(m_wallpaperCarouselCheckBox, &QCheckBox::clicked, this, [this] (bool checked) {
        m_wallpaperCarouselControl->setVisible(checked);
#if !defined(DISABLE_SCREENSAVER) || !defined(DISABLE_WALLPAPER_CAROUSEL)
        adjustModeSwitcherPoint();
#endif

        int checkedIndex = m_wallpaperCarouselControl->buttonList().indexOf(m_wallpaperCarouselControl->checkedButton());
        if (!checked) {
            setWallpaperSlideShow(QString());
        } else if (checkedIndex >= 0) {
            setWallpaperSlideShow(array_policy.at(checkedIndex));
        }
    });
    connect(m_wallpaperCarouselControl, &DButtonBox::buttonToggled, this, [this] (QAbstractButton * toggledBtn, bool state) {
        if(state){
            setWallpaperSlideShow(array_policy.at(m_wallpaperCarouselControl->buttonList().indexOf(toggledBtn)));
        }
    });

#endif

#ifndef DISABLE_SCREENSAVER
    m_toolLayout = new QHBoxLayout;

    m_waitControl = new DButtonBox(this);
    m_waitControl->installEventFilter(this);
    m_lockScreenBox = new CheckBox(tr("Require a password on wakeup"), this);
    m_lockScreenBox->installEventFilter(this); //??????Tab????????????
    QPalette lsPal = m_lockScreenBox->palette();
    lsPal.setColor(QPalette::All, QPalette::WindowText, textColor);
    m_lockScreenBox->setPalette(lsPal);

    QVector<int> time_array {60, 300, 600, 900, 1800, 3600, 0};
    QList<DButtonBoxButton *> timeArrayBtns;

    if (!m_dbusScreenSaver) {
        m_dbusScreenSaver = new ComDeepinScreenSaverInterface("com.deepin.ScreenSaver", "/com/deepin/ScreenSaver",
                                                              QDBusConnection::sessionBus(), this);
    }

    int current_wait_time_index = time_array.indexOf(m_dbusScreenSaver->linePowerScreenSaverTimeout());

    // ???????????????????????????????????????
    if (current_wait_time_index < 0) {
        int timeout = m_dbusScreenSaver->linePowerScreenSaverTimeout();
        time_array.prepend(timeout);
        current_wait_time_index = 0;
    }

    for (const int time : time_array) {
        if (time > 0) {
            DButtonBoxButton *btn = new DButtonBoxButton(timeFormat(time), this);
            btn->installEventFilter(this);
            btn->setMinimumWidth(40);
            timeArrayBtns.append(btn);
        }
    }

    timeArrayBtns.append(new DButtonBoxButton(tr("Never"), this));
    timeArrayBtns.last()->installEventFilter(this);
    m_waitControlLabel = new QLabel(tr("Wait:"), this);
    QPalette wcPal = m_waitControlLabel->palette();
    wcPal.setColor(QPalette::All, QPalette::WindowText, textColor);
    m_waitControlLabel->setPalette(wcPal);
    m_waitControl->setButtonList(timeArrayBtns, true);
    timeArrayBtns[current_wait_time_index]->setChecked(true);
    m_lockScreenBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_lockScreenBox->setChecked(m_dbusScreenSaver->lockScreenAtAwake());

    m_toolLayout->setSpacing(10);
    m_toolLayout->setContentsMargins(20, 10, 20, 10);
    m_toolLayout->addWidget(m_waitControlLabel);
    m_toolLayout->addWidget(m_waitControl);
    m_toolLayout->addSpacing(10);
    m_toolLayout->addWidget(m_lockScreenBox, 1, Qt::AlignLeft);

#ifdef DISABLE_WALLPAPER_CAROUSEL
    // ??????????????????????????????????????????
    QWidget *fake_layout = new QWidget(this);

    fake_layout->setFixedHeight(m_waitControl->height());
    fake_layout->setWindowFlags(Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    fake_layout->lower();
    m_toolLayout->addWidget(fake_layout);
#endif

    layout->addLayout(m_toolLayout);
    layout->addWidget(m_wallpaperList);
    layout->addSpacing(10);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    //###(zccrs): ?????????switModeControl????????????????????????????????????mos??????????????????
    // ??????anchors??????????????????
    DButtonBoxButton *wallpaperBtn = new DButtonBoxButton(tr("Wallpaper"), this);
    wallpaperBtn->installEventFilter(this);
    wallpaperBtn->setMinimumWidth(40);
    //wallpaperBtn->setFocusPolicy(Qt::NoFocus);
    m_switchModeControl = new DButtonBox(this);
    m_switchModeControl->setFocusPolicy(Qt::NoFocus);

    if (!ScreenSaverCtrlFunction::needShowScreensaver()) {
        m_switchModeControl->setButtonList({wallpaperBtn}, true);
        wallpaperBtn->setChecked(true);
        wallpaperBtn->installEventFilter(this);
    } else {
        DButtonBoxButton *screensaverBtn = new DButtonBoxButton(tr("Screensaver"), this);
        screensaverBtn->installEventFilter(this);
        screensaverBtn->setMinimumWidth(40);
        //screensaverBtn->setFocusPolicy(Qt::NoFocus);
        m_switchModeControl->setButtonList({wallpaperBtn, screensaverBtn}, true);
        if (m_mode == ScreenSaverMode) {
            screensaverBtn->setChecked(true);
        }
    }

    if (m_mode == WallpaperMode) {
        wallpaperBtn->setChecked(true);
    }

    connect(m_waitControl, &DButtonBox::buttonToggled, this, [this, time_array] (QAbstractButton * toggleBtn, bool) {
        int index = m_waitControl->buttonList().indexOf(toggleBtn);
        m_dbusScreenSaver->setBatteryScreenSaverTimeout(time_array[index]);
        m_dbusScreenSaver->setLinePowerScreenSaverTimeout(time_array[index]);
    });

    connect(m_switchModeControl, &DButtonBox::buttonToggled, this, static_cast<void(Frame::*)(QAbstractButton *, bool)>(&Frame::setMode));
    connect(m_lockScreenBox, &QCheckBox::toggled, m_dbusScreenSaver, &ComDeepinScreenSaverInterface::setLockScreenAtAwake);

    reLayoutTools();
#elif !defined(DISABLE_WALLPAPER_CAROUSEL)
    layout->addWidget(m_wallpaperList);
#endif

    layout->addStretch();
}

void Frame::initSize()
{
    if(!ScreenHelper::screenManager()->screen(m_screenName)){
        qCritical() << "lost screen " << m_screenName;
        return;
    }
    const QRect screenRect = ScreenHelper::screenManager()->screen(m_screenName)->geometry();
    int actualHeight;
#if defined(DISABLE_SCREENSAVER) && defined(DISABLE_WALLPAPER_CAROUSEL)
    actualHeight = FrameHeight;
#else
    actualHeight = FrameHeight + HeaderSwitcherHeight;
#endif
    setFixedSize(screenRect.width() - 20, actualHeight);

    qInfo() << "move befor: " << this->geometry() << m_wallpaperList->geometry();
    move(screenRect.x() + 10, screenRect.y() + screenRect.height() - height());
    m_wallpaperList->setFixedSize(screenRect.width() - 20, ListHeight);
    qInfo() << "this move : " << this->geometry() << m_wallpaperList->geometry();
}

void Frame::loading()
{
    if (m_itemwait == nullptr) {
        m_itemwait = new WaitItem;
    }

    m_itemwait->initSize(m_wallpaperList->size());
    m_wallpaperList->m_contentLayout->addWidget(m_itemwait);
    m_wallpaperList->m_contentWidget->adjustSize();

    QString lablecontant;

    if (m_mode == WallpaperMode) {
        lablecontant = QString(tr("Loading wallpapers..."));
    } else {
        lablecontant = QString(tr("Loading screensavers..."));
    }

    m_itemwait->setContantText(lablecontant);
}
void Frame::refreshList()
{
    //fix bug 47159
    //??????dbus???????????????????????????????????????????????????????????????????????????????????????????????????????????????
    //????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
    //?????????????????????????????????????????????
    if (!isVisible())
        return;

    m_wallpaperList->hide();
    m_wallpaperList->clear();

    loading();

    m_wallpaperList->show();
    if (m_mode == WallpaperMode) {
        m_dbusAppearance->setTimeout(5000);
        QDBusPendingCall call = m_dbusAppearance->List("background");
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, call,watcher] {
            if (call.isError()) {
                qWarning() << "failed to get all backgrounds: " << call.error().message();
                watcher->deleteLater();
                m_loadTimer.start(5000);
            } else {
                //remove itemwait and delete itemwait
                m_wallpaperList->m_contentLayout->removeWidget(m_itemwait);
                m_loadTimer.stop();

                if (m_itemwait != nullptr) {
                    delete m_itemwait;
                    m_itemwait = nullptr;
                }

                QDBusReply<QString> reply = call.reply();
                QString value = reply.value();
                QStringList strings = processListReply(value);
                QString currentPath = QString(m_backgroundManager->backgroundImages().value(m_screenName));
                if (currentPath.contains("/usr/share/backgrounds/default_background.jpg")) {
                    DAbstractFileInfoPointer tempInfo = DFileService::instance()->createFileInfo(nullptr, DUrl(currentPath));
                    if (tempInfo) {
                        currentPath = tempInfo->rootSymLinkTarget().toString();
                    }
                }

                foreach (QString path, strings) {
                    if (m_needDeleteList.contains(QUrl(path).path())) {
                        continue;
                    }
                    WallpaperItem *item = m_wallpaperList->addWallpaper(path);
                    item->setData(item->getPath());
                    item->setDeletable(m_deletableInfo.value(path));
                    item->addButton(DESKTOP_BUTTON_ID, tr("Desktop","button"), BUTTON_NARROW_WIDTH, 0, 0, 1, 1);
                    item->addButton(LOCK_SCREEN_BUTTON_ID, tr("Lock Screen","button"), BUTTON_NARROW_WIDTH, 0, 1, 1, 1);
                    item->addButton(DESKTOP_AND_LOCKSCREEN_BUTTON_ID, tr("Both"), BUTTON_WIDE_WIDTH, 1, 0, 1, 2);
                    item->show();
                    connect(item, &WallpaperItem::buttonClicked, this, &Frame::onItemButtonClicked);
                    connect(item, &WallpaperItem::tab, this, [=](){
                        //?????????????????????tab?????????????????????????????????????????????????????????
                        if (m_mode == WallpaperMode) {
                            m_wallpaperCarouselCheckBox->setFocus();
                        }
#ifndef DISABLE_SCREENSAVER
                        else {
                            m_waitControl->buttonList().first()->setFocus();
                        }
#endif
                    });
                    connect(item, &WallpaperItem::backtab, this, [=]() {
                        //?????????????????????backtab?????????????????????????????????????????????????????????
#ifndef DISABLE_SCREENSAVER
                        m_switchModeControl->buttonList().first()->setFocus();
#else
                        if (m_wallpaperCarouselCheckBox)
                            m_wallpaperCarouselCheckBox->setFocus();
#endif
                    });

                    //??????????????????????????????????????????
                    if (m_backgroundManager == nullptr) {
                        qCritical() << "Critical!" << "m_backgroundManager has deleted!";
                        this->hide();
                        return;
                    } else if (path.remove("file://") == currentPath.remove("file://")) { //???????????????????????????file:///??????
                        item->pressed();
                    }
                }

                m_wallpaperList->setFixedWidth(width());
                m_wallpaperList->updateItemThumb();
                m_wallpaperList->show();
            }
        });
    }
#ifndef DISABLE_SCREENSAVER
    else if (m_mode == ScreenSaverMode) {
        if (!m_dbusScreenSaver) {
            m_dbusScreenSaver = new ComDeepinScreenSaverInterface("com.deepin.ScreenSaver", "/com/deepin/ScreenSaver",
                                                                  QDBusConnection::sessionBus(), this);
        }

        const QStringList &saverNameList = m_dbusScreenSaver->allScreenSaver();
        const QString &currentScreensaver = m_dbusScreenSaver->currentScreenSaver();
        if (saverNameList.isEmpty() && !m_dbusScreenSaver->isValid()) {
            qWarning() << "com.deepin.ScreenSaver allScreenSaver fail. retry";
            m_loadTimer.start(5000);
            return;
        }

        //remove itemwait and delete itemwait
        m_wallpaperList->m_contentLayout->removeWidget(m_itemwait);
        m_loadTimer.stop();
        if (m_itemwait != nullptr) {
            delete m_itemwait;
            m_itemwait = nullptr;
        }

        bool isPressed = false; //?????????????????????????????????????????????
        for (const QString &name : saverNameList) {
            if("flurry" == name){
                continue;//?????????????????????flurry?????????
            }

            const QString &coverPath = m_dbusScreenSaver->GetScreenSaverCover(name);

            WallpaperItem *item = m_wallpaperList->addWallpaper(coverPath);
            item->setData(name);
            item->setUseThumbnailManager(false);
            item->setDeletable(false);
            item->addButton(SCREENSAVER_BUTTON_ID, tr("Apply","button"), BUTTON_WIDE_WIDTH, 0, 0, 1, 2);
            item->show();
            connect(item, &WallpaperItem::tab, this, [=]() {
                if (m_mode == WallpaperMode) {
                    m_wallpaperCarouselCheckBox->setFocus();
                } else {
                    m_waitControl->buttonList().first()->setFocus();
                }
            });
            connect(item, &WallpaperItem::backtab, this, [=]() {
                m_switchModeControl->buttonList().first()->setFocus();
            });
            connect(item, &WallpaperItem::buttonClicked, this, &Frame::onItemButtonClicked);
            //??????????????????????????????????????????
            if(!isPressed && !name.isEmpty() && name == currentScreensaver) {
                item->pressed();
                isPressed = true;
            }
        }
        if (!isPressed && m_wallpaperList->count() > 0) {
            qWarning() << "no screen saver item selected,and select default 0.";
            m_wallpaperList->setCurrentIndex(0);
        }

        m_wallpaperList->setFixedWidth(width());
        m_wallpaperList->updateItemThumb();
        m_wallpaperList->show();
    }
#endif
}

void Frame::onItemPressed(const QString &data)
{
    if (m_mode == WallpaperMode) {
        if (m_dbusDeepinWM)
            m_dbusDeepinWM->SetTransientBackground(data);

        if (m_backgroundManager)
            m_backgroundManager->setBackgroundImage(m_screenName, data);

        m_cureentWallpaper = data;

        // ???????????????????????????????????????
        if (m_closeButton && m_closeButton->isVisible()) {
            m_closeButton->hide();
        }

        QStringList used;
        if (m_backgroundManager)
            used = m_backgroundManager->backgroundImages().values();
        {
            for (int i = 0; i < m_wallpaperList->count(); ++i) {
                WallpaperItem *item = dynamic_cast<WallpaperItem *>(m_wallpaperList->item(i));
                if (item) {
                    bool isCustom = item->data().contains("custom-wallpapers");
                    if (!isCustom) {
                        continue;
                    }

                    bool isCurrent = used.contains(item->data());
                    bool isDeletable = item->getDeletable();
                    item->setDeletable(!isCurrent && (isDeletable || isCustom));
                }
            }
        }
    }
#ifndef DISABLE_SCREENSAVER
    else if (m_mode == ScreenSaverMode) {
        m_dbusScreenSaver->Preview(data, 1);
        qDebug() << "screensaver start" << data;
        if (m_backgroundManager && m_backgroundManager->isVisible()) {
            QThread::msleep(300); // TODO: ??????????????????????????????????????????????????????????????????
            m_backgroundManager->setVisible(false);
        }
    }
#endif
}

void Frame::onItemButtonClicked(const QString &buttonID)
{
    WallpaperItem *item = qobject_cast<WallpaperItem *>(sender());

    if (!item)
        return;

    if (buttonID == DESKTOP_BUTTON_ID) {
        // ????????????
        applyToDesktop();
    } else if (buttonID == LOCK_SCREEN_BUTTON_ID) {
        // ????????????
        applyToGreeter();
    } else if (buttonID == DESKTOP_AND_LOCKSCREEN_BUTTON_ID) {
        // ???????????????????????????
        applyToDesktop();
        applyToGreeter();
    }
#ifndef DISABLE_SCREENSAVER
    else if (buttonID == SCREENSAVER_BUTTON_ID) {
        m_dbusScreenSaver->setCurrentScreenSaver(item->data());
    }
#endif

    hide();
}

QStringList Frame::processListReply(const QString &reply)
{
    QStringList result;

    QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8());
    if (doc.isArray()) {
        QJsonArray arr = doc.array();
        foreach (QJsonValue val, arr) {
            QJsonObject obj = val.toObject();
            QString id = obj["Id"].toString();
            result.append(id);
            m_deletableInfo[id] = obj["Deletable"].toBool();
        }
    }

    return result;
}

QString Frame::getWallpaperSlideShow()
{
    if (nullptr == m_dbusAppearance) {
        qWarning() << "m_dbusAppearance is nullptr";
        return QString();
    }

    QString wallpaperSlideShow = m_dbusAppearance->GetWallpaperSlideShow(m_screenName);
    qInfo() << "dbus Appearance GetWallpaperSlideShow is called, result: " << wallpaperSlideShow;
    return wallpaperSlideShow;
}

void Frame::setWallpaperSlideShow(QString slideShow)
{
    if (nullptr == m_dbusAppearance) {
        qWarning() << "m_dbusAppearance is nullptr";
        return;
    }

    qInfo() << "dbus Appearance SetWallpaperSlideShow is called";
    m_dbusAppearance->SetWallpaperSlideShow(m_screenName, slideShow);
}

void Frame::applyToDesktop()
{
    if (nullptr == m_dbusAppearance) {
        qWarning() << "m_dbusAppearance is nullptr";
        return;
    }

    if (m_cureentWallpaper.isEmpty()) {
        qWarning() << "cureentWallpaper is empty";
        return;
    }

    qInfo() << "dbus Appearance SetMonitorBackground is called " << m_screenName << " " << m_cureentWallpaper;
    m_dbusAppearance->SetMonitorBackground(m_screenName, m_cureentWallpaper);

    emit backgroundChanged();
}

void Frame::applyToGreeter()
{
    if (nullptr == m_dbusAppearance) {
        qWarning() << "m_dbusAppearance is nullptr";
        return;
    }

    if (m_cureentWallpaper.isEmpty()) {
        qWarning() << "cureentWallpaper is empty";
        return;
    }

    qInfo() << "dbus Appearance greeterbackground is called " << m_cureentWallpaper;
    m_dbusAppearance->Set("greeterbackground", m_cureentWallpaper);
}
