/*
 * Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
 *
 * Author:     yanghao<yanghao@uniontech.com>
 *
 * Maintainer: zhengyouge<zhengyouge@uniontech.com>
 *             yanghao<yanghao@uniontech.com>
 *             hujianzhong<hujianzhong@uniontech.com>
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

#include "dfileview_p.h"
#include "dfileview.h"
#include "fileitem.h"
#include "dfilemenumanager.h"
#include "dfilemenu.h"
#include "windowmanager.h"
#include "dstatusbar.h"
#include "fileviewhelper.h"
#include "dfilemanagerwindow.h"
#include "dtoolbar.h"
#include "dabstractfilewatcher.h"
#include "dfmheaderview.h"
#include "dfmeventdispatcher.h"
#include "dfmsettings.h"
#include "dblockdevice.h"
#include "ddiskdevice.h"
#include "ddiskmanager.h"
#include "disomaster.h"
#include "dfmopticalmediawidget.h"
#include "io/dstorageinfo.h"
#include "app/define.h"
#include "app/filesignalmanager.h"

#include "interfaces/dfmglobal.h"
#include "interfaces/diconitemdelegate.h"
#include "interfaces/dlistitemdelegate.h"
#include "dfmapplication.h"
#include "interfaces/dfmcrumbbar.h"
#include "dialogs/dialogmanager.h"

#include "controllers/appcontroller.h"
#include "dfileservices.h"
#include "controllers/pathmanager.h"
#include "controllers/vaultcontroller.h"

#include "models/dfileselectionmodel.h"
#include "dfilesystemmodel.h"

#include "shutil/fileutils.h"
#include "shutil/mimesappsmanager.h"
#include "fileoperations/filejob.h"
#include "deviceinfo/udisklistener.h"

#include "singleton.h"
#include "interfaces/dfilemenumanager.h"
#include "models/desktopfileinfo.h"
#include "dfmstandardpaths.h"

#include <dgiosettings.h>

#include <QDrag>
#include <QApplication>
#include <DFileDragClient>
#include <DAnchors>
#include <QUrlQuery>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QScrollBar>
#include <QScroller>
#include <QtConcurrent>
#include <QMutex>
#include <private/qguiapplication_p.h>
#include <private/qtextengine_p.h>
#include <qpa/qplatformtheme.h>
#include <DSysInfo>

DWIDGET_USE_NAMESPACE

#define ICON_VIEW_SPACING 5
#define LIST_VIEW_SPACING 0
#define LIST_VIEW_MINIMUM_WIDTH 80

#define DEFAULT_HEADER_SECTION_WIDTH 140

#define LOOPNUM 10   // ???????????????????????????????????????
#define WAITTIME 10   // ?????????????????????????????????????????????

#define ICON_X_OFFSET 10
#define ICON_Y_OFFSET 10
#define ICON_WIDTH_OFFSET -20
#define ICON_HEIGHT_OFFSET -20

#define DRAGICON_SIZE 96   // ??????????????????icon??????(size??????ui??????)
#define DRAGICON_OUTLINE 30   //??????????????????????????????????????????????????????????????????
#define DRAGICON_MAX 4   //????????????????????????icon??????
#define DRAGICON_ROTATE 10.0   //????????????????????????
#define DRAGICON_OPACITY 0.1   //???????????????????????????
#define DRAGICON_MAX_COUNT 99   //??????????????????

#define LIST_DRAGRECT_WIDTH 120
#define LIST_DRAGTEXT_WIDTH 116
#define LIST_DRAGTEXT_HEIGTH 47

#define BOX_LINE_WIDTH 2

static const int kMinMoveLenght { 5 };

SelectWork::SelectWork(QObject *parent)
    : QThread(parent), m_pModel(nullptr), m_bStop(false)
{
}

void SelectWork::setInitData(QList<DUrl> lst, DFileSystemModel *model)
{
    // ??????bug-51429 bug-51039 bug-51503
    // ????????????/????????????????????????,????????????????????????
    QList<DUrl>::iterator itr = lst.begin();
    for (; itr != lst.end(); ++itr) {
        QString path = (*itr).toLocalFile();
        if (VaultController::isVaultFile(path)) {
            DUrl url(VaultController::localToVault(path));
            *itr = url;
        }
    }
    m_lstNoValid = lst;
    m_pModel = model;
}

void SelectWork::startWork()
{
    m_bStop = false;
    start();
}

void SelectWork::stopWork()
{
    m_bStop = true;
}

void SelectWork::run()
{
    msleep(WAITTIME);
    // ??????????????????????????????????????????
    if (!m_lstNoValid.isEmpty()) {
        QList<DUrl>::iterator itr = m_lstNoValid.begin();
        int loopNum = 0;
        while (itr != m_lstNoValid.end()) {
            msleep(WAITTIME);
            // ??????bug-51429 bug-51039 bug-51503
            // ????????????????????????,???????????????????????????LOOPNUM??????????????????,?????????????????????
            if (loopNum > LOOPNUM) {
                itr = m_lstNoValid.erase(itr);
                continue;
            }
            if (m_bStop)
                break;
            if (!m_pModel)
                break;
            const QModelIndex &index = m_pModel->index(*itr);
            if (index.isValid()) {
                // ???????????????????????????
                emit sigSetSelect(*itr);
                itr = m_lstNoValid.erase(itr);
                loopNum = 0;
            } else {
                ++loopNum;
            }
        }
    }
    if (m_pModel) {
        // ????????????
        m_pModel->update();
    }
}

DFileView::DFileView(QWidget *parent)
    : DListView(parent), d_ptr(new DFileViewPrivate(this)), m_currentTargetUrl(DUrl())
{
    D_D(DFileView);

    d_ptr->enabledSelectionModes << NoSelection << SingleSelection
                                 << MultiSelection << ExtendedSelection
                                 << ContiguousSelection;

    d_ptr->defaultViewMode = static_cast<ViewMode>(DFMApplication::instance()->appAttribute(DFMApplication::AA_ViewMode).toInt());

#if QT_VERSION < QT_VERSION_CHECK(5, 9, 0)
    d_ptr->touchTapDistance = 15;
#else
    d_ptr->touchTapDistance = QGuiApplicationPrivate::platformTheme()->themeHint(QPlatformTheme::TouchDoubleTapDistance).toInt();
#endif

    AC_SET_OBJECT_NAME(this, AC_FILE_VIEW);
    AC_SET_ACCESSIBLE_NAME(this, AC_FILE_VIEW);

    initUI();
    initModel();
    initDelegate();
    initConnects();

    // ??????wayland TASK-37638
    // ??????????????????
    m_pSelectWork = new SelectWork();
    connect(m_pSelectWork, &SelectWork::sigSetSelect,
            this, &DFileView::slotSetSelect);

    setIconSizeBySizeIndex(DFMApplication::instance()->appAttribute(DFMApplication::AA_IconSizeLevel).toInt());
    d->updateStatusBarTimer = new QTimer(this);
    d->updateStatusBarTimer->setInterval(100);
    d->updateStatusBarTimer->setSingleShot(true);
    connect(d->updateStatusBarTimer, &QTimer::timeout, this, &DFileView::updateStatusBar);

    d->diskmgr = new DDiskManager(this);
    connect(d->diskmgr, &DDiskManager::opticalChanged, this, &DFileView::onDriveOpticalChanged);
    d->diskmgr->setWatchChanges(true);
}

DFileView::~DFileView()
{
    disconnect(this, &DFileView::rowCountChanged, this, &DFileView::onRowCountChanged);
    disconnect(selectionModel(), &QItemSelectionModel::selectionChanged, this, &DFileView::delayUpdateStatusBar);

    if (m_pSelectWork) {
        m_pSelectWork->stopWork();
        m_pSelectWork->wait();
        m_pSelectWork->deleteLater();
        m_pSelectWork = nullptr;
    }

    //??????????????????????????????????????????
    QMutexLocker lkUpdateStatusBar(&d_ptr->m_mutexUpdateStatusBar);
}

DFileSystemModel *DFileView::model() const
{
    return qobject_cast<DFileSystemModel *>(DListView::model());
}

DFMStyledItemDelegate *DFileView::itemDelegate() const
{
    return qobject_cast<DFMStyledItemDelegate *>(DListView::itemDelegate());
}

void DFileView::setItemDelegate(DFMStyledItemDelegate *delegate)
{
    D_D(DFileView);

    QAbstractItemDelegate *dg = DListView::itemDelegate();

    if (dg)
        dg->deleteLater();

    DListView::setItemDelegate(delegate);

    connect(d->statusBar->scalingSlider(), &QSlider::valueChanged, delegate, &DFMStyledItemDelegate::setIconSizeByIconSizeLevel);

    if (isIconViewMode()) {
        d->statusBar->scalingSlider()->setMinimum(delegate->minimumIconSizeLevel());
        d->statusBar->scalingSlider()->setMaximum(delegate->maximumIconSizeLevel());
    }
}

DStatusBar *DFileView::statusBar() const
{
    D_DC(DFileView);

    return d->statusBar;
}

FileViewHelper *DFileView::fileViewHelper() const
{
    D_DC(DFileView);

    return d->fileViewHelper;
}

DUrl DFileView::rootUrl() const
{
    return model()->rootUrl();
}

DFMBaseView::ViewState DFileView::viewState() const
{
    return model()->state() == DFileSystemModel::Busy ? ViewBusy : ViewIdle;
}

QList<DUrl> DFileView::selectedUrls() const
{
    QModelIndex rootIndex = this->rootIndex();
    DUrlList list;

    for (const QModelIndex &index : selectedIndexes()) {
        if (index.parent() != rootIndex)
            continue;

        list << model()->getUrlByIndex(index);
    }

    return list;
}

bool DFileView::isIconViewMode() const
{
    D_DC(DFileView);

    return d->currentViewMode == IconMode;
}

int DFileView::columnWidth(int column) const
{
    D_DC(DFileView);

    if (d->headerView) {
        int logicalIndex = d->headerView->logicalIndex(column);
        return d->headerView->sectionSize(logicalIndex);
    }

    return 100;
}

int DFileView::headerViewHeight() const
{
    D_DC(DFileView);

    return d->headerView ? d->headerView->height() : 0;
}

void DFileView::setColumnWidth(int column, int width)
{
    D_D(DFileView);

    if (!d->headerView)
        return;

    d->headerView->resizeSection(column, width);
}

int DFileView::columnCount() const
{
    D_DC(DFileView);

    return d->headerView ? d->headerView->count() : 1;
}

int DFileView::rowCount() const
{
    int count = this->count();
    int itemCountForRow = this->itemCountForRow();

    return count / itemCountForRow + int(count % itemCountForRow > 0);
}

int DFileView::itemCountForRow() const
{
    Q_D(const DFileView);

    if (!isIconViewMode())
        return 1;

    return d->iconModeColumnCount();
}

QList<int> DFileView::columnRoleList() const
{
    D_DC(DFileView);

    return d->columnRoles;
}

quint64 DFileView::windowId() const
{
    return WindowManager::getWindowId(this);
}

void DFileView::setIconSize(const QSize &size)
{
    DListView::setIconSize(size);

    updateHorizontalOffset();
    updateGeometries();
}

DFileView::ViewMode DFileView::getDefaultViewMode() const
{
    D_DC(DFileView);

    return d->defaultViewMode;
}

DFileView::ViewMode DFileView::viewMode() const
{
    D_DC(DFileView);

    return d->currentViewMode;
}

bool DFileView::testViewMode(ViewModes modes, DFileView::ViewMode mode) const
{
    return (modes | mode) == modes;
}

int DFileView::horizontalOffset() const
{
    D_DC(DFileView);

    return d->horizontalOffset;
}

bool DFileView::isSelected(const QModelIndex &index) const
{
#ifndef CLASSICAL_SECTION
    return static_cast<DFileSelectionModel *>(selectionModel())->isSelected(index);
#else
    return selectionModel()->isSelected(index);
#endif
}

int DFileView::selectedIndexCount() const
{
#ifndef CLASSICAL_SECTION
    return static_cast<const DFileSelectionModel *>(selectionModel())->selectedCount();
#else
    return selectionModel()->selectedIndexes().count();
#endif
}

QModelIndexList DFileView::selectedIndexes() const
{
#ifndef CLASSICAL_SECTION
    return static_cast<DFileSelectionModel *>(selectionModel())->selectedIndexes();
#else
    return selectionModel()->selectedIndexes();
#endif
}

QModelIndex DFileView::indexAt(const QPoint &point) const
{
    Q_D(const DFileView);

    if (isIconViewMode()) {
        for (QModelIndex &index : itemDelegate()->hasWidgetIndexs()) {
            if (index == itemDelegate()->editingIndex())
                continue;

            QWidget *widget = indexWidget(index);

            if (widget && widget->isVisible() && widget->geometry().contains(point)) {
                return index;
            }
        }
    }

    QPoint pos = QPoint(point.x() + horizontalOffset(), point.y() + verticalOffset());
    QSize item_size = itemSizeHint();

    if (pos.y() % (item_size.height() + spacing() * 2) < spacing())
        return QModelIndex();

    int index = -1;

    if (item_size.width() == -1) {
        int item_height = item_size.height() + LIST_VIEW_SPACING * 2;

        index = pos.y() / item_height;
    } else {
        int item_width = item_size.width() + ICON_VIEW_SPACING * 2;

        if (pos.x() % item_width <= ICON_VIEW_SPACING)
            return QModelIndex();

        int row_index = pos.y() / (item_size.height() + ICON_VIEW_SPACING * 2);
        int column_count = d->iconModeColumnCount(item_width);
        int column_index = pos.x() / item_width;

        if (column_index >= column_count)
            return QModelIndex();

        index = row_index * column_count + column_index;

        const QModelIndex &tmp_index = rootIndex().child(index, 0);
        QStyleOptionViewItem option = viewOptions();

        option.rect = QRect(QPoint(column_index * item_width + ICON_VIEW_SPACING,
                                   row_index * (item_size.height() + ICON_VIEW_SPACING * 2) + ICON_VIEW_SPACING),
                            item_size);

        const QList<QRect> &list = itemDelegate()->paintGeomertys(option, tmp_index);

        auto ret = std::any_of(list.begin(), list.end(), [pos](const QRect &rect) {
            return rect.contains(pos);
        });
        if (ret)
            return tmp_index;

        return QModelIndex();
    }

    return rootIndex().child(index, 0);
}

QRect DFileView::visualRect(const QModelIndex &index) const
{
    Q_D(const DFileView);

    QRect rect;

    if (index.column() != 0)
        return rect;

    QSize item_size = itemSizeHint();

    if (item_size.width() == -1) {
        rect.setLeft(LIST_VIEW_SPACING - horizontalScrollBar()->value());
        rect.setRight(viewport()->width() - LIST_VIEW_SPACING - 1);
        rect.setTop(index.row() * (item_size.height() + LIST_VIEW_SPACING * 2) + LIST_VIEW_SPACING);
        rect.setHeight(item_size.height());

        if (d->allowedAdjustColumnSize) {
            rect.setWidth(d->headerView->length());
        }
    } else {
        int item_width = item_size.width() + ICON_VIEW_SPACING * 2;
        int column_count = d->iconModeColumnCount(item_width);

        if (column_count == 0)
            return rect;

        int column_index = index.row() % column_count;
        int row_index = index.row() / column_count;

        rect.setTop(row_index * (item_size.height() + ICON_VIEW_SPACING * 2) + ICON_VIEW_SPACING);
        rect.setLeft(column_index * item_width + ICON_VIEW_SPACING);
        rect.setSize(item_size);
    }

    rect.moveLeft(rect.left() - horizontalOffset());
    rect.moveTop(rect.top() - verticalOffset());

    return rect;
}

DFileView::RandeIndexList DFileView::visibleIndexes(QRect rect) const
{
    Q_D(const DFileView);

    RandeIndexList list;

    QSize item_size = itemSizeHint();
    QSize icon_size = iconSize();

    int count = this->count();
    int spacing = this->spacing();
    int item_width = item_size.width() + spacing * 2;
    int item_height = item_size.height() + spacing * 2;

    if (item_size.width() == -1) {
        list << RandeIndex(qMax((rect.top() + spacing) / item_height, 0),
                           qMin((rect.bottom() - spacing) / item_height, count - 1));
    } else {
        rect -= QMargins(spacing, spacing, spacing, spacing);

        int column_count = d->iconModeColumnCount(item_width);

        if (column_count <= 0)
            return list;

        int begin_row_index = rect.top() / item_height;
        int end_row_index = rect.bottom() / item_height;
        int begin_column_index = rect.left() / item_width;
        int end_column_index = rect.right() / item_width;

        if (rect.top() % item_height > icon_size.height())
            ++begin_row_index;

        int icon_margin = (item_width - icon_size.width()) / 2;

        if (rect.left() % item_width > item_width - icon_margin)
            ++begin_column_index;

        if (rect.right() % item_width < icon_margin)
            --end_column_index;

        begin_row_index = qMax(begin_row_index, 0);
        begin_column_index = qMax(begin_column_index, 0);
        end_row_index = qMin(end_row_index, count / column_count);
        end_column_index = qMin(end_column_index, column_count - 1);

        if (begin_row_index > end_row_index || begin_column_index > end_column_index)
            return list;

        int begin_index = begin_row_index * column_count;

        if (end_column_index - begin_column_index + 1 == column_count) {
            list << RandeIndex(qMax(begin_index, 0), qMin((end_row_index + 1) * column_count - 1, count - 1));

            return list;
        }

        for (int i = begin_row_index; i <= end_row_index; ++i) {
            if (begin_index + begin_column_index >= count)
                break;

            list << RandeIndex(qMax(begin_index + begin_column_index, 0),
                               qMin(begin_index + end_column_index, count - 1));

            begin_index += column_count;
        }
    }

    return list;
}

QSize DFileView::itemSizeHint() const
{

    return itemDelegate()->sizeHint(viewOptions(), rootIndex());
}

bool DFileView::isDropTarget(const QModelIndex &index) const
{
    D_DC(DFileView);

    return d->dragMoveHoverIndex == index;
}

bool DFileView::cd(const DUrl &url)
{
    DFileManagerWindow *w = qobject_cast<DFileManagerWindow *>(WindowManager::getWindowById(windowId()));

    return w && w->cd(url);
}

bool DFileView::cdUp()
{
    const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(rootIndex());

    const DUrl &oldCurrentUrl = rootUrl();
    const DUrl &parentUrl = fileInfo ? fileInfo->parentUrl() : DUrl::parentUrl(oldCurrentUrl);

    if (parentUrl.isValid() && !parentUrl.isRecentFile())
        return cd(parentUrl);
    return cd(DUrl::fromComputerFile("/"));
}

bool DFileView::edit(const QModelIndex &index, QAbstractItemView::EditTrigger trigger, QEvent *event)
{
    DUrl fileUrl = model()->getUrlByIndex(index);

    if (fileUrl.isEmpty() || selectedIndexCount() > 1 || (trigger == SelectedClicked && DFMGlobal::keyShiftIsPressed()))
        return false;
#ifdef SW_LABEL
    bool isCheckRenameAction = false;
#endif
    if (trigger == SelectedClicked && !isIconViewMode()) {
        QStyleOptionViewItem option = viewOptions();

        option.rect = visualRect(index);
        ;

        const QRect &file_name_rect = itemDelegate()->fileNameRect(option, index);

        if (!file_name_rect.contains(static_cast<QMouseEvent *>(event)->pos())) {
            return false;
        } else {
#ifdef SW_LABEL
            isCheckRenameAction = true;
#endif
        }
    }
#ifdef SW_LABEL
    if (trigger == EditKeyPressed) {
        isCheckRenameAction = true;
    }

    if (isCheckRenameAction) {
        bool isCanRename = checkRenamePrivilege_sw(fileUrl);
        if (!isCanRename)
            return false;
    }
#endif
    if (trigger == EditKeyPressed || trigger == AllEditTriggers) {
        emit fileDialogRename();
    }

    return DListView::edit(index, trigger, event);
}

void DFileView::select(const QList<DUrl> &list)
{
    QModelIndex firstIndex;
    QModelIndex lastIndex;
    const QModelIndex &root = rootIndex();
    clearSelection();
    for (DUrl url : list) {
        // ???????????????????????????
        if (VaultController::isVaultFile(url.toString()))
            url = VaultController::localUrlToVault(url);
        const QModelIndex &index = model()->index(url);

        if (!index.isValid() || index == root) {
            continue;
        }

        selectionModel()->select(index, QItemSelectionModel::Select);

        if (!firstIndex.isValid())
            firstIndex = index;

        lastIndex = index;
    }

    if (lastIndex.isValid())
        selectionModel()->setCurrentIndex(lastIndex, QItemSelectionModel::Select);

    if (firstIndex.isValid())
        scrollTo(firstIndex, PositionAtTop);
}

void DFileView::selectAllAfterCutOrCopy(const QList<DUrl> &list)
{
    QModelIndex firstIndex;
    QModelIndex lastIndex;
    const QModelIndex &root = rootIndex();
    clearSelection();

    // ??????wayland TASK-37638 ???????????????????????????????????????
    QList<DUrl> lstNoValid;

    for (DUrl url : list) {
        // ???????????????????????????
        if (VaultController::isVaultFile(url.toString()))
            url = VaultController::localUrlToVault(url);
        const QModelIndex &index = model()->index(url);

        // ?????????????????????????????????
        if (!index.isValid()) {
            lstNoValid.push_back(url);
            continue;
        }

        if (index == root) {
            continue;
        }

        // ????????????????????????????????????
        selectionModel()->select(index, QItemSelectionModel::Select);

        if (!firstIndex.isValid())
            firstIndex = index;

        lastIndex = index;
    }

    if (lastIndex.isValid())
        selectionModel()->setCurrentIndex(lastIndex, QItemSelectionModel::Select);

    if (firstIndex.isValid())
        scrollTo(firstIndex, PositionAtTop);

    // ??????wayland TASK-37638 ????????????????????????????????????????????????????????????
    if (!lstNoValid.isEmpty() && m_pSelectWork) {
        if (m_pSelectWork->isRunning()) {
            m_pSelectWork->stopWork();
            m_pSelectWork->wait();
        }
        // ???????????????
        m_pSelectWork->setInitData(lstNoValid, model());
        m_pSelectWork->startWork();
    }
}

void DFileView::slotSetSelect(DUrl url)
{
    const QModelIndex &index = model()->index(url);
    if (index.isValid())
        selectionModel()->select(index, QItemSelectionModel::Select);
}

void DFileView::setDefaultViewMode(DFileView::ViewMode mode)
{
    Q_D(DFileView);
    d->defaultViewMode = mode;

    const DUrl &root_url = rootUrl();

    //fix task wayland 21328 ????????????????????????????????????????????????
    if (d->allowedAdjustColumnSize) {
        setResizeMode(QListView::Adjust);
    }

    if (!root_url.isValid())
        return;

    if (d->fileViewStateValue(root_url, "viewMode", QVariant()).isValid())
        return;

    DAbstractFileInfoPointer info = model()->fileInfo(rootIndex());

    if (!info)
        return;

    ViewModes modes = static_cast<ViewModes>(info->supportViewMode());

    //view mode support handler
    if (modes & mode) {
        switchViewMode(mode);
    }
}

void DFileView::setViewMode(DFileView::ViewMode mode)
{
    switchViewMode(mode);
    emit viewStateChanged();
}

void DFileView::sortByRole(int role, Qt::SortOrder order)
{
    D_D(DFileView);

    model()->setSortRole(role, order);

    d->oldSelectedUrls = this->selectedUrls();

    if (!d->oldSelectedUrls.isEmpty())
        d->oldCurrentUrl = model()->getUrlByIndex(currentIndex());

    clearSelection();
    model()->sort();

    if (d->headerView) {
        QSignalBlocker blocker(d->headerView);
        Q_UNUSED(blocker)
        d->headerView->setSortIndicator(model()->sortColumn(), model()->sortOrder());
    }
}

QStringList DFileView::nameFilters() const
{
    return model()->nameFilters();
}

void DFileView::setNameFilters(const QStringList &nameFilters)
{
    model()->setNameFilters(nameFilters);
}

QDir::Filters DFileView::filters() const
{
    return model()->filters();
}

void DFileView::setEnabledSelectionModes(const QSet<QAbstractItemView::SelectionMode> &list)
{
    Q_D(DFileView);

    d->enabledSelectionModes = list;

    if (!list.contains(selectionMode())) {
        const DAbstractFileInfoPointer &info = model()->fileInfo(rootIndex());

        if (!info)
            return;

        const QList<DAbstractFileInfo::SelectionMode> &supportSelectionModes = info->supportSelectionModes();

        for (DAbstractFileInfo::SelectionMode mode : supportSelectionModes) {
            if (list.contains(static_cast<SelectionMode>(mode))) {
                setSelectionMode(static_cast<SelectionMode>(mode));
                break;
            }
        }
    }
}

QSet<QAbstractItemView::SelectionMode> DFileView::enabledSelectionModes() const
{
    Q_D(const DFileView);

    return d->enabledSelectionModes;
}

QWidget *DFileView::widget() const
{
    return const_cast<DFileView *>(this);
}

QList<QAction *> DFileView::toolBarActionList() const
{
    Q_D(const DFileView);

    return d->toolbarActionGroup->actions();
}

void DFileView::setDestroyFlag(bool flag)
{
    m_destroyFlag = flag;
}

void DFileView::setAlwaysOpenInCurrentWindow(bool openInCurrentWindow)
{
    Q_D(DFileView);

    d->isAlwaysOpenInCurrentWindow = openInCurrentWindow;
}

int DFileView::verticalOffset() const
{
    return DListView::verticalOffset();
}

void DFileView::setFilters(QDir::Filters filters)
{
    model()->setFilters(filters);
}

void DFileView::setAdvanceSearchFilter(const QMap<int, QVariant> &formData, bool turnOn, bool avoidUpdateView)
{
    //    Q_UNUSED(avoidUpdateView);
    model()->setAdvanceSearchFilter(formData, turnOn, avoidUpdateView);
}

void DFileView::dislpayAsActionTriggered(QAction *action)
{
    QAction *dAction = static_cast<QAction *>(action);
    dAction->setChecked(true);
    MenuAction type = static_cast<MenuAction>(dAction->data().toInt());

    switch (type) {
    case MenuAction::IconView:
        setViewModeToIcon();
        break;
    case MenuAction::ListView:
        setViewModeToList();
        break;
    case MenuAction::ExtendView:
        break;
    default:
        break;
    }
}

void DFileView::sortByActionTriggered(QAction *action)
{
    Q_D(DFileView);

    QAction *dAction = static_cast<QAction *>(action);
    const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(rootIndex());

    if (!fileInfo)
        return;

    int action_index = d->sortByActionGroup->actions().indexOf(dAction);

    if (action_index < 0)
        return;

    int sort_role = fileInfo->sortSubMenuActionUserColumnRoles().at(action_index);

    Qt::SortOrder order = (model()->sortRole() == sort_role && model()->sortOrder() == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;

    sortByRole(sort_role, order);

    const DUrl &root_url = rootUrl();

    d->setFileViewStateValue(root_url, "sortRole", model()->sortRole());
    d->setFileViewStateValue(root_url, "sortOrder", static_cast<int>(order));
}

void DFileView::openWithActionTriggered(QAction *action)
{
    QAction *dAction = static_cast<QAction *>(action);
    QString app = dAction->property("app").toString();
    DUrl fileUrl(dAction->property("url").toUrl());
    fileService->openFileByApp(this, app, fileUrl);
}

void DFileView::onRowCountChanged()
{
    //??????????????????????????????????????????
    QPointer<DFileView> me = this;
    if (me.isNull()) {
        return;
    }

#ifndef CLASSICAL_SECTION
    static_cast<DFileSelectionModel *>(selectionModel())->m_selectedList.clear();
#endif

    delayUpdateStatusBar();
    if (me.isNull()) {
        return;
    }
    updateContentLabel();
    if (me.isNull()) {
        return;
    }
    updateModelActiveIndex();
    if (me.isNull()) {
        return;
    }
}

void DFileView::wheelEvent(QWheelEvent *event)
{
    // ?????????????????????????????????????????????87504Bug??????????????????????????????
    if (event->buttons().testFlag(Qt::LeftButton)) {
        return;
    }

    if (isIconViewMode()) {
        if (DFMGlobal::keyCtrlIsPressed()) {
            if (event->angleDelta().y() > 0) {
                increaseIcon();
            } else {
                decreaseIcon();
            }
            emit viewStateChanged();
            event->accept();
        } else {
            verticalScrollBar()->setSliderPosition(verticalScrollBar()->sliderPosition() - event->angleDelta().y());
        }
    } else if (event->modifiers() == Qt::AltModifier) {
        horizontalScrollBar()->setSliderPosition(horizontalScrollBar()->sliderPosition() - event->angleDelta().x());
    } else {
        verticalScrollBar()->setSliderPosition(verticalScrollBar()->sliderPosition() - event->angleDelta().y());
    }
}

void DFileView::keyPressEvent(QKeyEvent *event)
{
    D_D(DFileView);

    const DUrlList &urls = selectedUrls();

    switch (event->modifiers()) {
    case Qt::NoModifier:
        if (normalKeyPressEvent(event)) {
            return;
        }
        break;
    case Qt::KeypadModifier:
        if (normalKeyPressEvent(event)) {
            return;
        }
        break;
    case Qt::ControlModifier:
        switch (event->key()) {
        case Qt::Key_N: {
            DUrlList list;

            for (const DUrl &url : urls) {
                const DAbstractFileInfoPointer &info = DFileService::instance()->createFileInfo(this, url);

                if (info && info->canFetch())
                    list << url;
            }

            appController->actionNewWindow(dMakeEventPointer<DFMUrlListBaseEvent>(this, list.isEmpty() ? DUrlList() << DUrl() : list));
            return;
        }
        case Qt::Key_H:
            d->preSelectionUrls = urls;

            itemDelegate()->hideAllIIndexWidget();
            clearSelection();
            model()->toggleHiddenFiles(rootUrl());

            return;
        case Qt::Key_I:
            if (rootUrl().isNetWorkFile())
                return;

            appController->actionProperty(dMakeEventPointer<DFMUrlListBaseEvent>(this, urls));

            return;
        case Qt::Key_Up:
            cdUp();

            return;
        case Qt::Key_Down:
            appController->actionOpen(dMakeEventPointer<DFMUrlListBaseEvent>(this, urls));

            return;
        case Qt::Key_T: {
            //do not handle key press event of autoRepeat type
            if (event->isAutoRepeat())
                return;

            DUrl url;
            bool selectionvalid = false;

            if (selectedIndexCount() == 1) {
                const DAbstractFileInfoPointer &fileInfoPointer = model()->fileInfo(selectedIndexes().first());
                if (fileInfoPointer->canFetch()) {
                    selectionvalid = true;
                    url = fileInfoPointer->fileUrl();
                    if (FileUtils::isArchive(fileInfoPointer->absoluteFilePath())) {
                        url.setScheme(AVFS_SCHEME);
                    }
                }
            }
            if (!selectionvalid) {
                url = DFMApplication::instance()->appUrlAttribute(DFMApplication::AA_UrlOfNewTab);
                if (!url.isValid())
                    url = rootUrl();
            }
            DFMEventDispatcher::instance()->processEvent<DFMOpenNewTabEvent>(this, url);
            return;
        }
        default:
            break;
        }

        break;
    case Qt::ShiftModifier:
        if (event->key() == Qt::Key_Delete) {
            if (urls.isEmpty())
                return;

            // ???????????????????????????????????????????????????
            if (rootUrl().isTrashFile() && rootUrl() != DUrl::fromTrashFile("/"))
                return;

            //! ????????????????????????????????????????????????
            //! ???????????????????????????????????????????????????
            //! ???????????????????????????????????????????????????
            bool bSlient = false;
            if (urls.size() > 0 && (urls.first().isUserShareFile() || urls.first().isSMBFile() || urls.first().isRecentFile()))
                bSlient = true;

            fileService->deleteFiles(this, urls, true, bSlient);

            return;
        } else if (event->key() == Qt::Key_T) {
            appController->actionOpenInTerminal(dMakeEventPointer<DFMUrlListBaseEvent>(this, urls));

            return;
        }

        break;
    case Qt::ControlModifier | Qt::ShiftModifier:
        if (event->key() == Qt::Key_N) {
            if (itemDelegate()->editingIndex().isValid())
                return;

            clearSelection();
            appController->actionNewFolder(dMakeEventPointer<DFMUrlBaseEvent>(this, rootUrl()));

            return;
        }
        break;

    case Qt::AltModifier:
    case Qt::AltModifier | Qt::KeypadModifier:
        switch (event->key()) {
        case Qt::Key_Up:
            cdUp();

            return;
        case Qt::Key_Down:
            appController->actionOpen(dMakeEventPointer<DFMUrlListBaseEvent>(this, urls));

            return;
        case Qt::Key_Home:
            cd(DUrl::fromLocalFile(QDir::homePath()));

            return;
        }
        break;

    default:
        break;
    }

    DListView::keyPressEvent(event);
}

void DFileView::keyReleaseEvent(QKeyEvent *event)
{
    if(this == QApplication::focusWidget()){
        if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
            emit fileSignalManager->requestShowFilePreviewDialog(selectedUrls(), model()->sortedUrls());
            return;
        }
    }

    DListView::keyReleaseEvent(event);
}

void DFileView::showEvent(QShowEvent *event)
{
    DListView::showEvent(event);

    Q_D(DFileView);

    /// set menu actions filter
    DFileMenuManager::setActionWhitelist(d->menuWhitelist);
    DFileMenuManager::setActionBlacklist(d->menuBlacklist);

    d->showCount++;
    setFocus();
}

void DFileView::mousePressEvent(QMouseEvent *event)
{
    D_D(DFileView);

    //????????????????????????????????????????????????
    if (event->buttons().testFlag(Qt::LeftButton)) {
        d->m_isMouseLeftPress = true;
        // ??????????????????????????????
        startPos = event->globalPos();
    } else {
        d->m_isMouseLeftPress = false;
    }

    switch (event->button()) {
    case Qt::BackButton: {
        DFMEventDispatcher::instance()->processEvent(dMakeEventPointer<DFMBackEvent>(this), qobject_cast<DFileManagerWindow *>(window()));
        break;
    }
    case Qt::ForwardButton: {
        DFMEventDispatcher::instance()->processEvent(dMakeEventPointer<DFMForwardEvent>(this), qobject_cast<DFileManagerWindow *>(window()));
        break;
    }
    case Qt::LeftButton: {
        // ?????????source???MouseEventSynthesizedByQt?????????????????????TouchBegin????????????
        //        if (event->source() == Qt::MouseEventSynthesizedByQt) {
        //            d->lastTouchBeginPos = event->pos();

        //            // ???????????????????????????????????????????????????????????????????????????????????????????????????????????????
        //            if (QScroller::hasScroller(this)) {
        //                // ???????????? ungrab????????????????????????????????????Qt???bug
        //                QScroller::scroller(this)->deleteLater();
        //            }

        //            if (d->updateEnableSelectionByMouseTimer) {
        //                d->updateEnableSelectionByMouseTimer->stop();
        //            } else {
        //                d->updateEnableSelectionByMouseTimer = new QTimer(this);
        //                d->updateEnableSelectionByMouseTimer->setSingleShot(true);

        //                static QObject *theme_settings = reinterpret_cast<QObject *>(qvariant_cast<quintptr>(qApp->property("_d_theme_settings_object")));
        //                QVariant touchFlickBeginMoveDelay;

        //                if (theme_settings) {
        //                    touchFlickBeginMoveDelay = theme_settings->property("touchFlickBeginMoveDelay");
        //                }

        //                d->updateEnableSelectionByMouseTimer->setInterval(touchFlickBeginMoveDelay.isValid() ? touchFlickBeginMoveDelay.toInt() : 300);

        //                connect(d->updateEnableSelectionByMouseTimer, &QTimer::timeout, d->updateEnableSelectionByMouseTimer, &QTimer::deleteLater);
        //            }

        //            d->updateEnableSelectionByMouseTimer->start();
        //        }

        bool isEmptyArea = d->fileViewHelper->isEmptyArea(event->pos());

        if (dragDropMode() != NoDragDrop) {
            setDragDropMode(DragDrop);
        }

        const QModelIndex &index = indexAt(event->pos());

        d->m_currentPressedIndex = isEmptyArea ? QModelIndex() : index;

        itemDelegate()->commitDataAndCloseActiveEditor();

        if (isEmptyArea) {
            d->currentSelection = selectionModel()->selection();
            if (!DFMGlobal::keyCtrlIsPressed()) {
                itemDelegate()->hideNotEditingIndexWidget();
                if (qApp->keyboardModifiers() == Qt::NoModifier)
                    setCurrentIndex(QModelIndex());
                if (dragDropMode() != NoDragDrop) {
                    setDragDropMode(DropOnly);
                }

                // ??????????????????????????????????????????????????????
                //                if (event->source() != Qt::MouseEventSynthesizedByQt) {
                //                    clearSelection();
                //                    update();
                //                }
            }
        } else if (DFMGlobal::keyCtrlIsPressed()) {
            //const QModelIndex &index = indexAt(event->pos());

            if (selectionModel()->isSelected(index)) {
                d->mouseLastPressedIndex = index;

                DListView::mousePressEvent(event);

                selectionModel()->select(index, QItemSelectionModel::Select);

                return;
            }
        } else if (DFMGlobal::keyShiftIsPressed()) {   // ????????????shit??????????????????????????????
            if (!selectionModel()->isSelected(index)) {   // ???????????????????????????
                DListView::mousePressEvent(event);   // ????????????
            }
        }

        d->mouseLastPressedIndex = QModelIndex();

        DListView::mousePressEvent(event);
        break;
    }
    case Qt::RightButton: {
        // 47203 ??????????????????????????????????????????????????????????????????????????????????????????
        // ??????????????????????????????????????????????????????????????????????????????????????????focusInEvent?????????????????????????????????
        if (qApp->activeWindow() != this->window())
            setFocus(Qt::ActiveWindowFocusReason);
        if (d->m_isMouseLeftPress)   //????????????????????????????????????
            DListView::mousePressEvent(event);
        break;
    }
    default:
        break;
    }
}

void DFileView::mouseMoveEvent(QMouseEvent *event)
{
    //    Q_D(const DFileView);

    // source????????????????????????????????????
    //    if (event->source() == Qt::MouseEventSynthesizedByQt) {
    //        if (QScroller::hasScroller(this))
    //            return;

    //        // ??????????????????????????????move?????????????????????????????????????????????????????????
    //        if (d->updateEnableSelectionByMouseTimer
    //                && d->updateEnableSelectionByMouseTimer->isActive()) {
    //            const QPoint difference_pos = event->pos() - d->lastTouchBeginPos;

    //            if (qAbs(difference_pos.x()) > d->touchTapDistance
    //                    || qAbs(difference_pos.y()) > d->touchTapDistance) {
    //                QScroller::grabGesture(this);
    //                QScroller *scroller = QScroller::scroller(this);

    //                scroller->handleInput(QScroller::InputPress, event->localPos(), static_cast<qint64>(event->timestamp()));
    //                scroller->handleInput(QScroller::InputMove, event->localPos(), static_cast<qint64>(event->timestamp()));
    //            }

    //            return;
    //        }
    //    }
    Q_D(DFileView);
    // ??????????????????
    if (event->buttons() & Qt::LeftButton) {
        moveRect = QRect(event->globalPos(), startPos);
    }
    //fix bug 59239 drag????????????????????????mouseMoveEvent??????????????????mouseMoveEvent????????????????????????
    d->m_isMouseMoveing.store(true);
    DListView::mouseMoveEvent(event);
    d->m_isMouseMoveing.store(false);
    if (!d->m_isDragging.load())
        emit requestWindowDestruct();
}

void DFileView::mouseReleaseEvent(QMouseEvent *event)
{
    D_D(DFileView);

    // ??????????????????
    if (event->buttons() & Qt::LeftButton) {
        moveRect = QRect(-1, -1, 1, 1);
        startPos = QPoint(0, 0);
    }

    d->dragMoveHoverIndex = QModelIndex();
    d->currentSelection = QItemSelection();

    if (d->mouseLastPressedIndex.isValid() && DFMGlobal::keyCtrlIsPressed()) {
        if (d->mouseLastPressedIndex == indexAt(event->pos()))
            selectionModel()->select(d->mouseLastPressedIndex, QItemSelectionModel::Deselect);
    }

    // ???????????????????????????????????????????????????
    if (!QScroller::hasScroller(this))
        return DListView::mouseReleaseEvent(event);
}

void DFileView::updateModelActiveIndex()
{
    if (m_isRemovingCase)   // bug202007010004???????????????????????????fileInfo->makeToActive() ?????????????????? crash
        return;

    Q_D(DFileView);

    const RandeIndexList randeList = visibleIndexes(QRect(QPoint(0, verticalScrollBar()->value()), QSize(size())));

    if (randeList.isEmpty()) {
        m_isRemovingCase = false;
        return;
    }

    const RandeIndex &rande = randeList.first();
    DAbstractFileWatcher *fileWatcher = model()->fileWatcher();

    for (int i = d->visibleIndexRande.first; i < rande.first; ++i) {
        const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(model()->index(i, 0));

        if (fileInfo) {
            fileInfo->makeToInactive();

            if (fileWatcher)
                fileWatcher->setEnabledSubfileWatcher(fileInfo->fileUrl(), false);
        }
    }

    for (int i = rande.second; i < d->visibleIndexRande.second; ++i) {
        const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(model()->index(i, 0));

        if (fileInfo) {
            fileInfo->makeToInactive();
            if (fileWatcher)
                fileWatcher->setEnabledSubfileWatcher(fileInfo->fileUrl(), false);
        }
    }

    d->visibleIndexRande = rande;
    for (int i = rande.first; i <= rande.second; ++i) {
        const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(model()->index(i, 0));

        if (fileInfo) {
            fileInfo->makeToActive();

            if (!fileInfo->exists()) {
                m_isRemovingCase = true;
                model()->removeRow(i, rootIndex());
            } else if (fileWatcher) {
                fileWatcher->setEnabledSubfileWatcher(fileInfo->fileUrl());
            }
        }
    }
    m_isRemovingCase = false;
}

void DFileView::handleDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles)
{
    DListView::dataChanged(topLeft, bottomRight, roles);

    for (int i = topLeft.row(); i <= bottomRight.row(); ++i) {
        update(model()->index(i, 0));
    }
}

void DFileView::delayUpdateStatusBar()
{
    Q_D(DFileView);

    // when QItemSelectionModel::selectionChanged emit we get selectedUrls() were old selecturls
    // so we wait...
    // fix bug 63925 ????????????????????????????????????????????????????????????????????????????????????????????????rooturl????????????
    // ????????????????????????url????????????????????????url???????????????????????????
    if (m_destroyFlag || d->m_isSetRootUrl) {
        return;
    }

    //????????????????????????????????????
    DFileSystemModel * curModel = qobject_cast<DFileSystemModel *>(model());
        if (!rootUrl().isSearchFile() && curModel &&
                curModel->state() == DFileSystemModel::Idle &&
                DFileService::instance()->checkGvfsMountfileBusy(rootUrl())) {
        d->updateStatusBarTimer->stop();
        return;
    }
    d->updateStatusBarTimer->start();
}

void DFileView::updateStatusBar()
{
    Q_D(DFileView);

    //??????????????????????????????????????????????????????????????????
    QPointer<DFileView> me = this;
    QMutexLocker lkUpdateStatusBar(&d_ptr->m_mutexUpdateStatusBar);
    if (!me) {
        return;
    }
    if (model()->state() != DFileSystemModel::Idle)
        return;

    //???????????????????????????????????????????????????????????????QT?????????????????????????????????setText??????
    if (QScroller::hasScroller(this)) {
        d_ptr->updateStatusBarTimer->stop();
        d_ptr->updateStatusBarTimer->start();
        return;
    }

    DFMEvent event(this);
    event.setWindowId(windowId());
    //?????????????????????url?????????????????????localfile?????????statusBar????????????????????????
    QList<DUrl> sourceUrls = selectedUrls();
    QList<DUrl> corectUrls;
    for (DUrl srcUrl : sourceUrls) {
        if (srcUrl.scheme() == SEARCH_SCHEME) {
            corectUrls << srcUrl.searchedFileUrl();
        } else {
            corectUrls << srcUrl;
        }
    }
    event.setData(corectUrls);
    int count = selectedIndexCount();
    //????????????????????????????????????
    if (DFileService::instance()->checkGvfsMountfileBusy(rootUrl())) {
        return;
    }

    // fix bug 67301
    if (rootUrl().isSearchFile() && !corectUrls.isEmpty()) {
        if (DFileService::instance()->checkGvfsMountfileBusy(corectUrls.first()))
            return;
    }

    if (!me) {
        qDebug() << "DFileView is null,so exit";
        return;
    }
    notifySelectUrlChanged(corectUrls);
    if (count == 0) {
        d->statusBar->itemCounted(event, this->count());
    } else {
        d->statusBar->itemSelected(event, count);
    }
}

void DFileView::openIndexByOpenAction(const int &action, const QModelIndex &index)
{
    if (action == DFMApplication::instance()->appAttribute(DFMApplication::AA_OpenFileMode).toInt()) {
        //???dfiledialog???????????????????????? ??????????????????????????????enable????????? ???????????????dialog??????
        if (action == 0) {
            Qt::ItemFlags flags = model()->flags(index);
            if (!flags.testFlag(Qt::ItemIsEnabled))
                return;
        }

        if (!DFMGlobal::keyCtrlIsPressed() && !DFMGlobal::keyShiftIsPressed())
            openIndex(index);
    }
}

void DFileView::setIconSizeBySizeIndex(const int &sizeIndex)
{
    QSignalBlocker blocker(statusBar()->scalingSlider());
    Q_UNUSED(blocker)
    statusBar()->scalingSlider()->setValue(sizeIndex);
    itemDelegate()->setIconSizeByIconSizeLevel(sizeIndex);
}

#ifdef SW_LABEL
bool DFileView::checkRenamePrivilege_sw(DUrl fileUrl)
{
    QString srcFileName = fileUrl.toLocalFile();
    if (FileJob::isLabelFile(srcFileName)) {
        int nRet = FileJob::checkRenamePrivilege(srcFileName);
        if (nRet != 0) {
            emit fileSignalManager->jobFailed(nRet, "rename", srcFileName);
            return false;
        }
    }
    return true;
}
#endif

void DFileView::onRootUrlDeleted(const DUrl &rootUrl)
{
    const DAbstractFileInfoPointer &fileInfo = DFileService::instance()->createFileInfo(this, rootUrl);
    DUrl new_url = fileInfo ? fileInfo->goToUrlWhenDeleted() : DUrl::fromLocalFile(QDir::homePath());

    requestCdTo(new_url);
}

void DFileView::freshView()
{
    model()->refresh(rootUrl());
}

void DFileView::loadViewState(const DUrl &url)
{
    Q_D(DFileView);

    QVariant defIconSize = DFMApplication::instance()->appAttribute(DFMApplication::AA_IconSizeLevel).toInt();
    int iconSize = d->fileViewStateValue(url, "iconSizeLevel", defIconSize).toInt();
    setIconSizeBySizeIndex(iconSize);
    int savedViewMode = d->fileViewStateValue(url, "viewMode", -1).toInt();
    ViewMode viewMode = getDefaultViewMode();
    if (savedViewMode == -1) {
        if (DFMApplication::appObtuselySetting()->value("ApplicationAttribute", "UseParentViewMode", false).toBool()) {
            DAbstractFileInfoPointer info = fileService->createFileInfo(nullptr, url);
            DUrlList urlList = info->parentUrlList();
            for (const DUrl &url : urlList) {
                int checkViewMode = d->fileViewStateValue(url, "viewMode", -1).toInt();
                if (checkViewMode != -1) {
                    viewMode = static_cast<ViewMode>(checkViewMode);
                    break;
                }
            }
        }
    } else {
        viewMode = static_cast<ViewMode>(savedViewMode);
    }

    switchViewMode(viewMode);
}

void DFileView::saveViewState()
{
    //filter url that we are not interesting on
    const DUrl &url = rootUrl();

    if (url.isSearchFile() || !url.isValid() || url.isComputerFile()) {
        return;
    }

    Q_D(DFileView);

    d->setFileViewStateValue(url, "iconSizeLevel", statusBar()->scalingSlider()->value());
    d->setFileViewStateValue(url, "viewMode", static_cast<int>(viewMode()));
}

void DFileView::onSortIndicatorChanged(int logicalIndex, Qt::SortOrder order)
{
    Q_D(DFileView);

    model()->setSortColumn(logicalIndex, order);

    d->oldSelectedUrls = this->selectedUrls();

    if (!d->oldSelectedUrls.isEmpty())
        d->oldCurrentUrl = model()->getUrlByIndex(currentIndex());

    clearSelection();
    model()->sort();

    const DUrl &root_url = rootUrl();

    d->setFileViewStateValue(root_url, "sortRole", model()->sortRole());
    d->setFileViewStateValue(root_url, "sortOrder", static_cast<int>(order));
}

void DFileView::onDriveOpticalChanged(const QString &path)
{
    Q_D(DFileView);

    for (auto i : d->diskmgr->blockDevices({})) {
        QScopedPointer<DBlockDevice> blkdev(DDiskManager::createBlockDevice(i));
        if (path == blkdev->drive()) {
            qDebug() << QString(blkdev->device());
            ISOMaster->nullifyDevicePropertyCache(QString(blkdev->device()));
        }
    }
}

void DFileView::reset()
{
    DListView::reset();
}

void DFileView::setRootIndex(const QModelIndex &index)
{
    DListView::setRootIndex(index);
}

void DFileView::onHeaderSectionMoved(int logicalIndex, int oldVisualIndex, int newVisualIndex)
{
    Q_D(DFileView);
    Q_UNUSED(logicalIndex)
    Q_UNUSED(oldVisualIndex)
    Q_UNUSED(newVisualIndex)

    //???????????????????????????
    QVariantList logicalIndexList;
    for (int i = 0; i < d->headerView->count(); ++i) {
        int logicalIndex = d->headerView->logicalIndex(i);
        logicalIndexList << model()->columnToRole(logicalIndex);
    }

    DUrl root_url = rootUrl();
    //?????????????????????????????????????????????"search:"????????????
    if (root_url.isSearchFile()) {
        // fix bug 112908
        // ????????????????????????????????????????????????"search:?url=trash:///"???"search:?url=recent:///"
        const auto &targetUrl = root_url.searchTargetUrl();
        root_url = DUrl();
        root_url.setScheme(SEARCH_SCHEME);
        if (targetUrl.isRecentFile()) {
            root_url.setSearchTargetUrl(DUrl::fromRecentFile("/"));
        } else if (targetUrl.isTrashFile()) {
            root_url.setSearchTargetUrl(DUrl::fromTrashFile("/"));
        }
    }
    d->setFileViewStateValue(root_url, "headerList", logicalIndexList);
    //????????????????????????????????????
    DFMApplication::appObtuselySetting()->sync();

    //????????????
    updateListHeaderViewProperty();
    update();
}

void DFileView::focusInEvent(QFocusEvent *event)
{
    Q_D(const DFileView);

    // ????????????QAbstractItemView::focusInEvent???????????????currentIndex
    QAbstractScrollArea::focusInEvent(event);
    itemDelegate()->commitDataAndCloseActiveEditor();

    /// set menu actions filter
    DFileMenuManager::setActionWhitelist(d->menuWhitelist);
    DFileMenuManager::setActionBlacklist(d->menuBlacklist);
}

void DFileView::resizeEvent(QResizeEvent *event)
{
    Q_D(DFileView);

    DListView::resizeEvent(event);

    if (d->statusBar && d->statusBar->width() != width()) {
        d->statusBar->setFixedWidth(width());
    }

    updateHorizontalOffset();

    if (itemDelegate()->editingIndex().isValid())
        doItemsLayout();

    updateModelActiveIndex();
}

void DFileView::contextMenuEvent(QContextMenuEvent *event)
{
    D_DC(DFileView);

    if (!canShowContextMenu(event))
        return;

    const QModelIndex &index = indexAt(event->pos());
    bool clickedSelected = isSelected(index);
    bool isEmptyArea = d->fileViewHelper->isEmptyArea(event->pos());
    Qt::ItemFlags flags = index.flags();

    //????????????????????????item flags???????????????????????????????????????
    if (isEmptyArea && !model()->flags(rootIndex()).testFlag(Qt::ItemIsEnabled)) {
        return;
    }

    if (clickedSelected) {   //????????????
        //???????????????index?????????
        if (!flags.testFlag(Qt::ItemIsEnabled)) {
            return showEmptyAreaMenu(rootIndex().flags());   //show root Index flags
        } else {
            return showNormalMenu(index, flags);   // show opration menu
        }
    } else {   //???????????????
        itemDelegate()->hideNotEditingIndexWidget();   //????????????????????????edit??????
        clearSelection();   //???????????????indexs

        if (!index.isValid()) {   //index?????????
            return showEmptyAreaMenu(rootIndex().flags());   //show root Index flags

        } else if (!isIconViewMode() && isEmptyArea) {   //listview ????????? isEmptyArea
            return showEmptyAreaMenu(rootIndex().flags());   //show root Index flags

        } else {   //index ??????
            selectionModel()->select(index, QItemSelectionModel::SelectionFlag::Select);
            //???????????????index?????????
            if (!flags.testFlag(Qt::ItemIsEnabled)) {
                //show root Index flags
                return showEmptyAreaMenu(rootIndex().flags());
            } else {
                return showNormalMenu(index, flags);
            }

        }   //else end
    }
}

bool DFileView::canShowContextMenu(QContextMenuEvent *event)
{
    Q_D(DFileView);

    //????????????????????????????????????????????????kMinSelectLength???????????????????????????
    if (d->m_isMouseLeftPress && (abs(moveRect.width()) > kMinMoveLenght || abs(moveRect.height()) > kMinMoveLenght)) {
        return false;
    }

    //???????????????????????????????????????smb??????
    if (DFileService::instance()->checkGvfsMountfileBusy(rootUrl()))
        return false;

    //???????????????????????????????????????????????????????????????
    if (rootUrl().isSearchFile()) {
        //???????????????????????????index
        if (indexAt(event->pos()).isValid()) {
            DUrl fileUrl = model()->getUrlByIndex(indexAt(event->pos())).searchedFileUrl();
            if (DFileService::instance()->checkGvfsMountfileBusy(fileUrl))
                return false;
        } else {   //???????????????????????????????????????
            DUrl fileUrl = rootUrl().searchTargetUrl();
            if (DFileService::instance()->checkGvfsMountfileBusy(fileUrl))
                return false;
        }
    }

    return true;
}

void DFileView::dragEnterEvent(QDragEnterEvent *event)
{
    Q_D(DFileView);

    // ???????????????????????????????????????, ???????????????????????????, ??????????????????
    if (QByteArray(ISDRAGPROHIBIT) == event->mimeData()->data(QString(MIME_PROHIBIT_DRAG))) {
        event->setDropAction(Qt::IgnoreAction);
        event->ignore();
        return;
    }

    // ??????bug-65773 ????????????????????????????????????????????????????????????
    // ??????????????????DFileDragClient::setTargetUrl(data, url);
    // ???????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
    // ???????????????????????????????????????(??????????????????????????????)
    m_currentTargetUrl.clear();
    if (DFileDragClient::checkMimeData(event->mimeData())) {
        event->acceptProposedAction();
        setTargetUrlToApp(event->mimeData(), rootUrl());
        return;
    }

    //??????????????????????????????root???????????????????????????????????????????????????mimedata??????url?????????
    bool sameUser = DFMGlobal::isMimeDatafromCurrentUser(event->mimeData());
    if (sameUser) {
        if (!fetchDragEventUrlsFromSharedMemory())
            return;
    } else {
        m_urlsForDragEvent = event->mimeData()->urls();
    }

    const DAbstractFileInfoPointer &rootFileInfo = DFileService::instance()->createFileInfo(this, rootUrl());

    DUrl rootItemUrl = rootUrl();
    if (rootFileInfo && rootItemUrl.isTaggedFile() && rootFileInfo->canRedirectionFileUrl())
        rootItemUrl = rootFileInfo->redirectedFileUrl();

    for (const auto &url : m_urlsForDragEvent) {
        const DAbstractFileInfoPointer &fileInfo = DFileService::instance()->createFileInfo(this, DUrl(url));
        if (!fileInfo) {
            event->ignore();
            return;
        }
        if (!rootUrl().isTaggedFile() || (rootUrl().isTaggedFile() && rootFileInfo && rootFileInfo->canRedirectionFileUrl())) {
            bool isInSameDevice = DStorageInfo::inSameDevice(fileInfo->fileUrl(), rootItemUrl);
            if ((!isInSameDevice && !fileInfo->isReadable()) || (!DFMGlobal::keyCtrlIsPressed() && isInSameDevice && !fileInfo->canRename())) {
                event->ignore();
                return;
            }
        }

        //????????????????????????????????????????????????????????????
        if (!fileInfo || !fileInfo->canMoveOrCopy()) {
            event->ignore();
            return;
        }

        //??????????????????tag??????????????????tag??????????????????tag??????
        if (model()->rootUrl().isTaggedFile() && !fileInfo->canTag()) {
            event->ignore();
            return;
        }
    }

    d->fileViewHelper->preproccessDropEvent(event, m_urlsForDragEvent);

    if (event->mimeData()->hasFormat("XdndDirectSave0")) {
        event->setDropAction(Qt::CopyAction);
        event->acceptProposedAction();

        return;
    }

    DListView::dragEnterEvent(event);
}

void DFileView::dragMoveEvent(QDragMoveEvent *event)
{
    D_D(DFileView);

    if (isIconViewMode()) {
        d->dragMoveHoverIndex = d->fileViewHelper->isEmptyArea(event->pos()) ? rootIndex() : indexAt(event->pos());
    } else {   // fix 88616 ??????????????????????????????????????????????????????????????????????????????????????????????????????
        d->dragMoveHoverIndex = indexAt(event->pos());
        //??????index????????????rootIndex??????
        if (!d->dragMoveHoverIndex.isValid()) {
            d->dragMoveHoverIndex = rootIndex();
        }
    }

    if (d->dragMoveHoverIndex.isValid()) {
        const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(d->dragMoveHoverIndex);

        if (fileInfo) {
            d->fileViewHelper->preproccessDropEvent(event, m_urlsForDragEvent);
            if (!fileInfo->canDrop()
                    || !fileInfo->supportedDropActions().testFlag(event->dropAction())
                    || (fileInfo->isDir() && !fileInfo->isWritable())) {
                const DUrl &toUrl = model()->getUrlByIndex(d->dragMoveHoverIndex);
                if ((fileInfo->isFile() || fileInfo->fileUrl().isTrashFile())
                        && (event->source() != this)
                        && !(toUrl.isSearchFile() && (toUrl.fragment().startsWith(TRASH_ROOT) || toUrl.fragment().startsWith(RECENT_ROOT)))
                        && fileInfo->isWritable()) {
                    // ??????????????????????????????
                    d->dragMoveHoverIndex = rootIndex();
                    setViewSelectState(true);
                    return event->accept();
                }

                d->dragMoveHoverIndex = QModelIndex();
                setViewSelectState(false);
                return event->ignore();
            }

            if (fileInfo->isDir() && ((!fileInfo->fileUrl().isTaggedFile()) || (fileInfo->fileUrl().isTaggedFile() && fileInfo->canRedirectionFileUrl()))) {
                DUrl dragMoveHoverIndexUrl = fileInfo->fileUrl().isTaggedFile() ? fileInfo->redirectedFileUrl() : fileInfo->fileUrl();
                for (const auto &url : m_urlsForDragEvent) {
                    const DAbstractFileInfoPointer &dragFileInfo = DFileService::instance()->createFileInfo(this, DUrl(url));
                    if (!dragFileInfo) {
                        setViewSelectState(false);
                        event->ignore();
                        return;
                    }

                    bool isInSameDevice = DStorageInfo::inSameDevice(dragFileInfo->fileUrl(), dragMoveHoverIndexUrl);
                    if ((!isInSameDevice && !dragFileInfo->isReadable()) || (!DFMGlobal::keyCtrlIsPressed() && isInSameDevice && !dragFileInfo->canRename())) {
                        setViewSelectState(false);
                        event->ignore();
                        return;
                    }
                }
            }
            // ?????????????????????????????????????????????
            const DUrl &toUrl = model()->getUrlByIndex(d->dragMoveHoverIndex);
            if (toUrl.isSearchFile() && toUrl.fragment().startsWith(TRASH_ROOT)) {
                d->dragMoveHoverIndex = QModelIndex();
                setViewSelectState(false);
                return event->ignore();
            }

            // fix bug 64049
            // ?????????????????????????????????????????????
            const DAbstractFileInfoPointer &srcInfo = model()->fileInfo(selectionModel()->currentIndex());
            if (event->dropAction() == Qt::MoveAction && srcInfo) {
                const DUrl &parent = srcInfo->parentUrl();
                if (parent == fileInfo->fileUrl() && event->source() == d->fileViewHelper->parent()) {
                    d->dragMoveHoverIndex = QModelIndex();
                    setViewSelectState(false);
                    return event->ignore();
                }
            }

            if (DFileDragClient::checkMimeData(event->mimeData())) {
                event->acceptProposedAction();
                setTargetUrlToApp(event->mimeData(), fileInfo->fileUrl());
            } else {
                if (fileInfo->isDir() || fileInfo->isDesktopFile()) {
                    setViewSelectState(false);
                }
                event->accept();
            }

            // ???????????????????????????????????????????????????????????????
            if (fileInfo->canDrop() && fileInfo->canDragCompress()) {
                // ???????????????gvfs?????????????????????????????????
                if (!m_urlsForDragEvent.isEmpty()) {
                    const DAbstractFileInfoPointer &dragfileInfo = DFileService::instance()->createFileInfo(this, DUrl(m_urlsForDragEvent.first()));
                    if (dragfileInfo->isGvfsMountFile()) {
                        event->setDropAction(Qt::MoveAction);
                        setViewSelectState(false);
                        return event->ignore();
                    }
                }

                // ???????????????????????????????????????????????????????????????,????????????????????????
                const DUrl &toUrl = model()->getUrlByIndex(d->dragMoveHoverIndex);
                if (toUrl.isSearchFile()) {
                    // ???????????????????????????????????????????????????
                    if (!fileInfo->canRename()) {
                        setViewSelectState(false);
                        return event->ignore();
                    }
                    // ???????????????????????????????????????????????????????????????
                    if (!(toUrl.fragment().startsWith(RECENT_ROOT) || toUrl.fragment().startsWith(TRASH_ROOT))) {
                        setViewSelectState(false);
                        return event->setDropAction(Qt::CopyAction);
                    }
                }

                // ???????????????????????????????????????????????????
                const DAbstractFileInfoPointer &parentInfo = model()->fileInfo(d->dragMoveHoverIndex.parent());
                if (parentInfo && parentInfo->isWritable()) {
                    setViewSelectState(false);
                    return event->setDropAction(Qt::CopyAction);
                }
                setViewSelectState(false);
                return event->ignore();
            }
        }

        if (d->dragMoveHoverIndex == rootIndex())
            setViewSelectState(true);
    }

    update();
    // update()?????????draging???delegate??????(why???)??? ?????????viewport()->update()??????
    viewport()->update();

    if (dragDropMode() == InternalMove
        && (event->source() != this || !(event->possibleActions() & Qt::MoveAction)))
        QAbstractItemView::dragMoveEvent(event);
}

void DFileView::dragLeaveEvent(QDragLeaveEvent *event)
{
    D_D(DFileView);
    setViewSelectState(false);
    d->dragMoveHoverIndex = QModelIndex();

    DListView::dragLeaveEvent(event);
}

void DFileView::dropEvent(QDropEvent *event)
{
    D_D(DFileView);
    setViewSelectState(false);
    d->dragMoveHoverIndex = QModelIndex();
    // clean old index area
    update();

    d->fileViewHelper->preproccessDropEvent(event);

    if (event->mimeData()->property("IsDirectSaveMode").toBool()) {
        event->setDropAction(Qt::CopyAction);

        const QModelIndex &index = indexAt(event->pos());
        const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(index.isValid() ? index : rootIndex());

        if (fileInfo && fileInfo->fileUrl().isLocalFile()) {
            if (fileInfo->isDir()) {
                const_cast<QMimeData *>(event->mimeData())->setProperty("DirectSaveUrl", fileInfo->fileUrl());
            } else {
                const_cast<QMimeData *>(event->mimeData())->setProperty("DirectSaveUrl", fileInfo->parentUrl());
            }
        }

        event->accept();   // yeah! we've done with XDS so stop Qt from further event propagation.
    } else {
        QModelIndex index;
        if (isIconViewMode()) {
            index = d->fileViewHelper->isEmptyArea(event->pos()) ? QModelIndex() : indexAt(event->pos());
        } else {   // fix 88616 ??????????????????????????????????????????????????????????????????????????????????????????????????????
            index = indexAt(event->pos());
        }

        if (!index.isValid())
            index = rootIndex();

        const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(index);
        if (fileInfo) {
            if (fileInfo->fileUrl().isTrashFile())
                index = rootIndex();
            // ??????????????????????????????????????????????????????????????????,???drop??????????????????????????????
            if (fileInfo->isFile()
                    && !DFMGlobal::isDesktopFile(fileInfo->fileUrl())
                    && !fileInfo->isDragCompressFileFormat())
                index = rootIndex();
        }

        if (!index.isValid())
            return;

        // ????????????????????????????????????
        if (isSelected(index) && event->source() == this)
            return;

        if (model()->supportedDropActions() & event->dropAction() && model()->flags(index) & Qt::ItemIsDropEnabled) {
            const Qt::DropAction action = dragDropMode() == InternalMove ? Qt::MoveAction : event->dropAction();
            //fix bug 59239 drag????????????????????????dropMimeData?????????????????????????????????
            d->m_isDragging.store(true);
            if (model()->dropMimeData(event->mimeData(), action, index.row(), index.column(), index)) {
                if (action != event->dropAction()) {
                    event->setDropAction(action);
                    event->accept();
                } else {
                    event->acceptProposedAction();
                }
            }
        }
        //??????????????????
        DFileService::instance()->setCursorBusyState(false);
        stopAutoScroll();
        setState(NoState);
        viewport()->update();
    }

    if (DFileDragClient::checkMimeData(event->mimeData())) {

        QModelIndex index;
        if (isIconViewMode()) {
            index = d->fileViewHelper->isEmptyArea(event->pos()) ? QModelIndex() : indexAt(event->pos());
        } else {   // fix 88616 ??????????????????????????????????????????????????????????????????????????????????????????????????????
            index = indexAt(event->pos());
        }

        if (!index.isValid())
            index = rootIndex();

        if (!index.isValid()) {
            //fix bug 59239 drag????????????????????????dropMimeData?????????????????????????????????
            d->m_isDragging.store(false);
            if (!d->m_isMouseMoveing.load())
                emit requestWindowDestruct();
            return;
        }

        event->acceptProposedAction();
        setTargetUrlToApp(event->mimeData(), model()->getUrlByIndex(index));

        // DFileDragClient deletelater() will be called after connection destroyed
        DFileDragClient *c = new DFileDragClient(event->mimeData());
        DUrlList urlList = DUrl::fromQUrlList(event->mimeData()->urls());
        connect(c, &DFileDragClient::stateChanged, this, [this, urlList](DFileDragState state) {
            if (state == Finished) {
                select(urlList);
            }
        });
    }
    //fix bug 59239 drag????????????????????????dropMimeData?????????????????????????????????
    d->m_isDragging.store(false);
    if (!d->m_isMouseMoveing.load())
        emit requestWindowDestruct();
}

void DFileView::setSelection(const QRect &rect, QItemSelectionModel::SelectionFlags flags)
{
    D_D(DFileView);
    if (DFMGlobal::keyShiftIsPressed()) {
        //???????????????????????????????????????????????????item
        if (!d->m_currentPressedIndex.isValid()) {
            QItemSelection oldSelection = d->currentSelection;

            if (isIconViewMode()) {   //????????????
                DFileSystemModel *pModel = model();
                if (pModel) {
                    // ListView?????????????????????????????????????????????????????????????????????????????????
                    int nRowNum = pModel->rowCount();

                    QPoint offset(-horizontalOffset() + ICON_X_OFFSET, ICON_Y_OFFSET);
                    // ??????????????????????????????????????????(?????????rect??????view???????????????????????????????????????????????????????????????????????????)
                    QRect actualRect(MIN(rect.left(), rect.right()), MIN(rect.top(), rect.bottom()) + verticalOffset(), abs(rect.width()), abs(rect.height()));
                    // ???????????????????????????????????????
                    QVector<QModelIndex> selectItems;
                    for (int i = 0; i < nRowNum; ++i) {
                        const QModelIndex &index = pModel->index(i, 0);
                        const QRect &itemRect = rectForIndex(index);
                        QRect realItemRect((itemRect.topLeft() + offset), itemRect.bottomRight() + offset + QPoint(ICON_HEIGHT_OFFSET, ICON_WIDTH_OFFSET));
                        if (!(actualRect.left() > realItemRect.right() - 3
                              || actualRect.top() > realItemRect.bottom() - 3
                              || realItemRect.left() + 3 > actualRect.right()
                              || realItemRect.top() + 3 > actualRect.bottom())) {
                            if (!oldSelection.contains(index)) {
                                QItemSelectionRange selectionRange(index);
                                oldSelection.push_back(selectionRange);
                            }
                        }
                    }
                    selectionModel()->select(oldSelection, QItemSelectionModel::ClearAndSelect);
                    return;
                }
            } else {   //????????????
                QRect tmp_rect = rect;
                //???????????????????????????????????????????????????????????????
                if (tmp_rect.width() < 5 && tmp_rect.width() > -5 && tmp_rect.height() < 5 && tmp_rect.height() > -5)
                    return;

                tmp_rect.translate(horizontalOffset(), verticalOffset());
                tmp_rect.setCoords(qMin(tmp_rect.left(), tmp_rect.right()), qMin(tmp_rect.top(), tmp_rect.bottom()),
                                   qMax(tmp_rect.left(), tmp_rect.right()), qMax(tmp_rect.top(), tmp_rect.bottom()));

                const RandeIndexList &list = visibleIndexes(tmp_rect);

                for (const RandeIndex &index : list) {
                    oldSelection.append(QItemSelectionRange(rootIndex().child(index.first, 0), rootIndex().child(index.second, 0)));
                }

                return selectionModel()->select(oldSelection, flags);
            }
        }

        const QModelIndex &index = indexAt(rect.bottomRight());

        if (!index.isValid())
            return;

        const QModelIndex &lastSelectedIndex = indexAt(rect.topLeft());

        if (!lastSelectedIndex.isValid())
            return;

        selectionModel()->select(QItemSelection(lastSelectedIndex, index), QItemSelectionModel::ClearAndSelect);

        return;
    }

    if (flags == (QItemSelectionModel::Current | QItemSelectionModel::Rows | QItemSelectionModel::ClearAndSelect)) {
        QRect tmp_rect = rect;
        //???????????????????????????????????????????????????????????????
        if (tmp_rect.width() < 5 && tmp_rect.width() > -5 && tmp_rect.height() < 5 && tmp_rect.height() > -5)
            return;

        // sp4-task:?????????????????????????????????
        // ???????????????????????????
        if (isIconViewMode()) {
            DFileSystemModel *pModel = model();
            if (pModel) {
                // ListView?????????????????????????????????????????????????????????????????????????????????
                int nRowNum = pModel->rowCount();

                QPoint offset(-horizontalOffset() + ICON_X_OFFSET, ICON_Y_OFFSET);
                // ??????????????????????????????????????????(?????????rect??????view???????????????????????????????????????????????????????????????????????????)
                QRect actualRect(MIN(rect.left(), rect.right()), MIN(rect.top(), rect.bottom()) + verticalOffset(), abs(rect.width()), abs(rect.height()));
                // ???????????????????????????????????????
                QVector<QModelIndex> selectItems;
                for (int i = 0; i < nRowNum; ++i) {
                    const QModelIndex &index = pModel->index(i, 0);
                    const QRect &itemRect = rectForIndex(index);
                    QRect realItemRect((itemRect.topLeft() + offset), itemRect.bottomRight() + offset + QPoint(ICON_HEIGHT_OFFSET, ICON_WIDTH_OFFSET));
                    if (!(actualRect.left() > realItemRect.right() - 3
                          || actualRect.top() > realItemRect.bottom() - 3
                          || realItemRect.left() + 3 > actualRect.right()
                          || realItemRect.top() + 3 > actualRect.bottom()))
                        selectItems.push_back(index);
                }
                // ???????????????????????????????????????
                clearSelection();
                // ???????????????????????????????????????
                QVector<QModelIndex>::const_iterator itr = selectItems.begin();
                for (; itr != selectItems.end(); ++itr) {
                    // ???????????????????????????
                    selectionModel()->select(*itr, QItemSelectionModel::Select);
                }
            }
            return;
        }

        tmp_rect.translate(horizontalOffset(), verticalOffset());
        tmp_rect.setCoords(qMin(tmp_rect.left(), tmp_rect.right()), qMin(tmp_rect.top(), tmp_rect.bottom()),
                           qMax(tmp_rect.left(), tmp_rect.right()), qMax(tmp_rect.top(), tmp_rect.bottom()));

        const RandeIndexList &list = visibleIndexes(tmp_rect);

        if (list.isEmpty()) {
            clearSelection();

            return;
        }

#ifndef CLASSICAL_SECTION
        return selectionModel()->select(QItemSelection(rootIndex().child(list.first().first, 0),
                                                       rootIndex().child(list.last().second, 0)),
                                        flags);
#else
        QItemSelection selection;

        for (const RandeIndex &index : list) {
            selection.append(QItemSelectionRange(rootIndex().child(index.first, 0), rootIndex().child(index.second, 0)));
        }

        return selectionModel()->select(selection, flags);
#endif
    }
    DListView::setSelection(rect, flags);
}

QModelIndex DFileView::moveCursor(QAbstractItemView::CursorAction cursorAction, Qt::KeyboardModifiers modifiers)
{
    D_D(DFileView);

    QModelIndex current = currentIndex();

    if (!current.isValid()) {
        d->lastCursorIndex = DListView::moveCursor(cursorAction, modifiers);

        return d->lastCursorIndex;
    }

    if (rectForIndex(current).isEmpty()) {
        d->lastCursorIndex = model()->index(0, 0, rootIndex());

        return d->lastCursorIndex;
    }

    QModelIndex index;

    switch (cursorAction) {
    case MoveLeft:
        if (DFMGlobal::keyShiftIsPressed()) {
            index = DListView::moveCursor(cursorAction, modifiers);

            if (index == d->lastCursorIndex) {
                index = index.sibling(index.row() - 1, index.column());
            }
        } else {
            index = current.sibling(current.row() - 1, current.column());
        }

        // rekols: Loop to find the next file item that can be selected.
        while (index.model() && !(index.flags() & Qt::ItemIsSelectable) && index.isValid()) {
            index = index.sibling(index.row() - 1, index.column());
        }

        break;

    case MoveRight:
        if (DFMGlobal::keyShiftIsPressed()) {
            index = DListView::moveCursor(cursorAction, modifiers);

            if (index == d->lastCursorIndex) {
                index = index.sibling(index.row() + 1, index.column());
            }
        } else {
            index = current.sibling(current.row() + 1, current.column());
        }

        while (index.model() && !(index.flags() & Qt::ItemIsSelectable) && index.isValid()) {
            index = index.sibling(index.row() + 1, index.column());
        }

        break;

    default:
        index = DListView::moveCursor(cursorAction, modifiers);
        break;
    }

    if (index.isValid()) {
        if (viewMode() == IconMode) {
            bool last_row = indexOfRow(index) == rowCount() - 1;

            if (!last_row
                && current == index
                && (cursorAction == MoveDown
                    || cursorAction == MovePageDown
                    || cursorAction == MoveNext)) {
                // ????????????????????????????????????QListView????????????????????????????????????????????????????????????
                index = model()->index(count() - 1, 0);
                last_row = true;
            }

            if (last_row) {
                // call later
                //QTimer::singleShot(0, this, [this, index, d] {//this index unused,????????????
                QTimer::singleShot(0, this, [d] {
                    // scroll to end
                    d->verticalScrollBar->setValue(d->verticalScrollBar->maximum());
                });
            }
        }

        d->lastCursorIndex = index;

        return index;
    }

    d->lastCursorIndex = current;

    return current;
}

void DFileView::rowsAboutToBeRemoved(const QModelIndex &parent, int start, int end)
{
    QModelIndex currentIdx = currentIndex();
    for (const QModelIndex &index : selectedIndexes()) {
        if (index.parent() != parent)
            continue;

        if (index.row() >= start && index.row() <= end) {
            selectionModel()->select(index, QItemSelectionModel::Clear);
            if (index == currentIdx) {
                clearSelection();
            }
        }
    }

    Q_D(DFileView);

    if (start < d->visibleIndexRande.second) {
        if (end <= d->visibleIndexRande.first) {
            d->visibleIndexRande.first -= (end - start + 1);
            d->visibleIndexRande.second -= (end - start + 1);
        } else if (end <= d->visibleIndexRande.second) {
            d->visibleIndexRande.first = start;
            d->visibleIndexRande.second -= (end - start + 1);
        } else {
            d->visibleIndexRande.first = d->visibleIndexRande.second = -1;
        }
    }

    DListView::rowsAboutToBeRemoved(parent, start, end);
}

void DFileView::rowsInserted(const QModelIndex &parent, int start, int end)
{
    DListView::rowsInserted(parent, start, end);
}

void DFileView::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles)
{
    D_D(DFileView);

    DListView::dataChanged(topLeft, bottomRight, roles);

    if (d->oldCurrentUrl.isValid())
        setCurrentIndex(model()->index(d->oldCurrentUrl));

    for (const DUrl &url : d->oldSelectedUrls) {
        selectionModel()->select(model()->index(url), QItemSelectionModel::Select);
    }

    /// Clean
    d->oldCurrentUrl = DUrl();
    d->oldSelectedUrls.clear();
}

bool DFileView::event(QEvent *e)
{
    Q_D(DFileView);
    switch (e->type()) {
    case QEvent::KeyPress: {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(e);
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
            if (keyEvent->modifiers() == Qt::ControlModifier || keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier))
                return DListView::event(e);
            e->accept();

            if (keyEvent->modifiers() == Qt::ShiftModifier) {
                QKeyEvent nkeyEvent(keyEvent->type(), Qt::Key_Left, Qt::NoModifier);
                keyPressEvent(&nkeyEvent);
            } else {
                QKeyEvent nkeyEvent(keyEvent->type(), Qt::Key_Right, Qt::NoModifier);
                keyPressEvent(&nkeyEvent);
            }

            return true;
        }
    } break;
    case QEvent::Resize:
        d->pureResizeEvent(static_cast<QResizeEvent *>(e));
        break;
    case QEvent::ParentChange:
        window()->installEventFilter(this);
        break;
    case QEvent::FontChange:
        // blumia: to trigger DIconItemDelegate::updateItemSizeHint() to update its `d->itemSizeHint` ...
        emit iconSizeChanged(iconSize());
    default:
        break;
    }

    return DListView::event(e);
}

void DFileView::updateGeometries()
{
    Q_D(DFileView);

    if (!d->headerView || !d->allowedAdjustColumnSize) {
        return DListView::updateGeometries();
    }

    resizeContents(d->headerView->length(), contentsSize().height());

    DListView::updateGeometries();
}

bool DFileView::eventFilter(QObject *obj, QEvent *event)
{
    Q_D(DFileView);

    switch (event->type()) {
    case QEvent::Move:
        if (obj != horizontalScrollBar()->parentWidget())
            return DListView::eventFilter(obj, event);
        d->updateHorizontalScrollBarPosition();
        break;
    case QEvent::WindowStateChange:
        if (d->headerView) {
            d->toggleHeaderViewSnap(true);
            d->doFileNameColResize();
        }
        break;
    // blumia: ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
    //         ???????????????????????? Resize ????????? size ??? oldSize ???????????????????????????????????????????????????????????????
    //         ???????????????????????????????????????????????????????????????????????????????????????????????????????????? Resize ???????????????????????????????????????
    case QEvent::ShowToParent:
    case QEvent::HideToParent:
        if (d->headerView && d->cachedViewWidth != this->width()) {
            d->cachedViewWidth = this->width();
            d->toggleHeaderViewSnap(true);
        }
        break;
    default:
        break;
    }

    return DListView::eventFilter(obj, event);
}

void DFileView::paintEvent(QPaintEvent *e)
{
    DListView::paintEvent(e);
    if (bShowViewSelectBox) {
        QPainter painter(viewport());
        QColor color = palette().color(QPalette::ColorGroup::Active, QPalette::ColorRole::Highlight);
        color.setAlpha(255 * 0.4);  // ui?????????40%????????????
        QPen pen(color, BOX_LINE_WIDTH);
        painter.setPen(pen);
        painter.drawRect(QRectF(BOX_LINE_WIDTH/2, BOX_LINE_WIDTH/2, viewport()->size().width() - BOX_LINE_WIDTH, viewport()->size().height() - BOX_LINE_WIDTH));
    }
}

#if QT_CONFIG(draganddrop)
void DFileView::startDrag(Qt::DropActions supportedActions)
{
    Q_D(DFileView);
    QModelIndexList indexes = d->selectedDraggableIndexes();
    if (!indexes.isEmpty()) {
        if (indexes.count() == 1) {
            if (d->currentViewMode == DFileView::ListMode)
                showCustomDragPixmap(indexes, supportedActions);
            else
                DListView::startDrag(supportedActions);
            return;
        }
        showCustomDragPixmap(indexes, supportedActions);
    }
}
#endif   // QT_CONFIG(draganddrop)

void DFileView::onShowHiddenFileChanged()
{
    QDir::Filters filters;

    if (DFMApplication::instance()->genericAttribute(DFMApplication::GA_ShowedHiddenFiles).toBool())
        filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden;
    else
        filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;

    model()->setFilters(filters);
}

void DFileView::onShowFileSuffixChanged()
{
    update();
}

void DFileView::initDelegate()
{
    D_D(DFileView);

    setItemDelegate(new DIconItemDelegate(d->fileViewHelper));
    setIconSizeBySizeIndex(itemDelegate()->iconSizeLevel());
}

void DFileView::initUI()
{
    D_D(DFileView);

    setSpacing(ICON_VIEW_SPACING);
    setResizeMode(QListView::Adjust);
    setOrientation(QListView::LeftToRight, true);
    setTextElideMode(Qt::ElideMiddle);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDropIndicatorShown(false);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QListView::EditKeyPressed | QListView::SelectedClicked);
    //    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setDefaultDropAction(Qt::CopyAction);

    // disable auto remove item when drop of MoveAction finished
    setDragDropOverwriteMode(true);

    DListView::setSelectionRectVisible(true);

    d->displayAsActionGroup = new QActionGroup(this);
    d->sortByActionGroup = new QActionGroup(this);
    d->openWithActionGroup = new QActionGroup(this);
    d->fileViewHelper = new FileViewHelper(this);

    d->statusBar = new DStatusBar(this);
    d->statusBar->scalingSlider()->setPageStep(1);
    d->statusBar->scalingSlider()->setTickInterval(1);

    addFooterWidget(d->statusBar);

    d->headerOpticalDisc = new DFMOpticalMediaWidget(this);
    addHeaderWidget(d->headerOpticalDisc);
    d->headerOpticalDisc->hide();

    d->verticalScrollBar = verticalScrollBar();
    //d->verticalScrollBar->setParent(this); // this will make verticalScrollBar always visible

    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    if (d->allowedAdjustColumnSize) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    }

    d->toolbarActionGroup = new QActionGroup(this);

    updateToolBarActions(this);

    // context width adjustable:
    d->allowedAdjustColumnSize = DFMApplication::instance()->appAttribute(DFMApplication::AA_ViewSizeAdjustable).toBool();
}

void DFileView::initModel()
{
    D_D(DFileView);

    setModel(new DFileSystemModel(d->fileViewHelper));
#ifndef CLASSICAL_SECTION
    setSelectionModel(new DFileSelectionModel(model(), this));
#endif
}

void DFileView::initConnects()
{
    D_D(DFileView);

    connect(this, &DFileView::clicked, [=](const QModelIndex &index) {
        openIndexByOpenAction(0, index);
    });

    connect(this, &DFileView::doubleClicked, [=](const QModelIndex &index) {
        openIndexByOpenAction(1, index);
    });

    connect(this, &DFileView::rowCountChanged, this, &DFileView::onRowCountChanged, Qt::QueuedConnection);

    connect(d->displayAsActionGroup, &QActionGroup::triggered, this, &DFileView::dislpayAsActionTriggered);
    connect(d->sortByActionGroup, &QActionGroup::triggered, this, &DFileView::sortByActionTriggered);
    connect(d->openWithActionGroup, &QActionGroup::triggered, this, &DFileView::openWithActionTriggered);

    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, &DFileView::delayUpdateStatusBar);
    connect(model(), &DFileSystemModel::dataChanged, this, &DFileView::handleDataChanged);
    connect(model(), &DFileSystemModel::stateChanged, this, &DFileView::onModelStateChanged);
    connect(model(), &DFileSystemModel::sortFinished, this, &DFileView::delayUpdateStatusBar);
    connect(model(), &DFileSystemModel::rootUrlDeleted, this, &DFileView::onRootUrlDeleted);

    connect(this, &DFileView::iconSizeChanged, this, &DFileView::updateHorizontalOffset, Qt::QueuedConnection);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &DFileView::updateModelActiveIndex);

    connect(DFMApplication::instance(), &DFMApplication::iconSizeLevelChanged, this, &DFileView::setIconSizeBySizeIndex);
    connect(DFMApplication::instance(), &DFMApplication::showedHiddenFilesChanged, this, &DFileView::onShowHiddenFileChanged);
    connect(DFMApplication::instance(), &DFMApplication::showedFileSuffixChanged, this, &DFileView::onShowFileSuffixChanged);
    connect(fileSignalManager, &FileSignalManager::requestFreshAllFileView, this, &DFileView::freshView, Qt::QueuedConnection);
    connect(fileSignalManager, &FileSignalManager::requestUpdateAllFileView, this, static_cast<void (DFileView::*)()>(&DFileView::update));
    connect(DFMApplication::instance(), &DFMApplication::viewModeChanged, this, [this](const int &viewMode) {
        setDefaultViewMode(static_cast<ViewMode>(viewMode));
    });
    connect(DFMApplication::instance(), &DFMApplication::previewAttributeChanged, this, [this] {
        model()->refresh();
    });

    connect(d->statusBar->scalingSlider(), &QSlider::valueChanged, this, &DFileView::viewStateChanged);
    connect(this, &DFileView::rootUrlChanged, this, &DFileView::loadViewState);
    connect(this, &DFileView::viewStateChanged, this, &DFileView::saveViewState);

    connect(d->toolbarActionGroup, &QActionGroup::triggered, this, [this] {
        Q_D(const DFileView);

        if (d->toolbarActionGroup->actions().first()->isChecked())
            setViewModeToIcon();
        else
            setViewModeToList();
    });

    // fix bug#44171 ?????????????????????????????????????????????
    connect(model(), &DFileSystemModel::showFilterButton, this, [this]() {
        DFileManagerWindow *w = qobject_cast<DFileManagerWindow *>(WindowManager::getWindowById(windowId()));
        if (w) {
            w->showFilterButton();
        }
    });

    // fix bug 63938
    // ???????????????????????????????????????????????????
    connect(model(), &DFileSystemModel::updateFilterRule, this, [this](const FileFilter *filter) {
        DFileManagerWindow *w = qobject_cast<DFileManagerWindow *>(WindowManager::getWindowById(windowId()));
        if (w)
            w->updateAdvanceSearchBarValue(filter);
    });
}

void DFileView::increaseIcon()
{
    int iconSizeLevel = itemDelegate()->increaseIcon();

    if (iconSizeLevel >= 0) {
        setIconSizeBySizeIndex(iconSizeLevel);
    }
}

void DFileView::decreaseIcon()
{
    int iconSizeLevel = itemDelegate()->decreaseIcon();

    if (iconSizeLevel >= 0) {
        setIconSizeBySizeIndex(iconSizeLevel);
    }
}

void DFileView::openIndex(const QModelIndex &index)
{
    Q_D(DFileView);

    const DUrl &url = model()->getUrlByIndex(index);
    //????????????????????????????????????
    if (DFileService::instance()->checkGvfsMountfileBusy(url)) {
        return;
    }

    DFMOpenUrlEvent::DirOpenMode mode;
    if (d->isAlwaysOpenInCurrentWindow) {
        mode = DFMOpenUrlEvent::OpenInCurrentWindow;
    } else {
        mode = DFMApplication::instance()->appAttribute(DFMApplication::AA_AllwayOpenOnNewWindow).toBool()
                ? DFMOpenUrlEvent::ForceOpenNewWindow
                : DFMOpenUrlEvent::OpenInCurrentWindow;
    }

    if (mode == DFMOpenUrlEvent::OpenInCurrentWindow)
        DFMEventDispatcher::instance()->processEventAsync<DFMOpenUrlEvent>(this, DUrlList() << url, mode);
    else
        DFMEventDispatcher::instance()->processEvent<DFMOpenUrlEvent>(this, DUrlList() << url, mode);
}

void DFileView::keyboardSearch(const QString &search)
{
    D_D(DFileView);

    if (search.isEmpty())
        return;

    d->fileViewHelper->keyboardSearch(search.toLocal8Bit().at(0));
}

bool DFileView::setRootUrl(const DUrl &url)
{
    D_D(DFileView);
    d->m_isSetRootUrl = true;
    if (url.isEmpty()) {
        d->m_isSetRootUrl = false;
        return false;
    }

    itemDelegate()->hideAllIIndexWidget();
    clearSelection();
    if (!url.isSearchFile()) {
        setFocus();
    }
    DUrl fileUrl = url;
    //! ???????????????????????????????????????????????????????????????????????????
    //    if(url.toLocalFile().contains(VaultController::makeVaultLocalPath()))
    //    {
    //        fileUrl = VaultController::localUrlToVault(url);
    //    }

    DAbstractFileInfoPointer info = DFileService::instance()->createFileInfo(this, fileUrl);

    while (info && info->canRedirectionFileUrl()) {
        const DUrl old_url = fileUrl;

        fileUrl = info->redirectedFileUrl();

        if (old_url == fileUrl)
            break;
        //????????????????????????????????????
        if (!DFileService::instance()->checkGvfsMountfileBusy(fileUrl)) {
            info = DFileService::instance()->createFileInfo(this, fileUrl);
        } else {
            info = nullptr;
        }

        qDebug() << "url redirected, from:" << old_url << "to:" << fileUrl;
    }

    //! ???????????????????????????????????????????????????????????????????????????
    //    if(fileUrl.toLocalFile().contains(VaultController::makeVaultLocalPath()))
    //    {
    //        fileUrl = VaultController::localUrlToVault(fileUrl);
    //    }

    if (!info) {
        qDebug() << "This scheme isn't support, url" << fileUrl;
        d->m_isSetRootUrl = false;
        return false;
    }
    // TODO: drop this special case when we switch away from UDiskListener::addSubscriber in AppController.
    if (fileUrl.scheme() == BURN_SCHEME) {
        Q_ASSERT(fileUrl.burnDestDevice().length() > 0);

        QString strVolTag = DFMOpticalMediaWidget::getVolTag(fileUrl);

        QString devpath = fileUrl.burnDestDevice();
        QStringList rootDeviceNode = DDiskManager::resolveDeviceNode(devpath, {});
        if (rootDeviceNode.isEmpty()) {
            d->m_isSetRootUrl = false;
            return false;
        }
        QString udiskspath = rootDeviceNode.first();
        //getOpticalDriveMutex()->lock();// ???????????????????????????????????????????????????bug 31318:?????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
        DISOMasterNS::DeviceProperty dp = ISOMaster->getDevicePropertyCached(devpath);
        //getOpticalDriveMutex()->unlock();
        QSharedPointer<DBlockDevice> blkdev(DDiskManager::createBlockDevice(udiskspath));
        CdStatusInfo *pCdStatusInfo = &DFMOpticalMediaWidget::g_mapCdStatusInfo[strVolTag];   // bug fix 31427:?????????????????????????????????????????????????????????:?????????????????? ???????????? ???????????????????????????????????????????????????????????????????????????????????????????????? linux ???????????????

        if (!dp.devid.length()) {
            //QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            DFileService::instance()->setCursorBusyState(true);
            if (DFMOpticalMediaWidget::g_mapCdStatusInfo.contains(strVolTag))
                DFMOpticalMediaWidget::g_mapCdStatusInfo[strVolTag].bLoading = true;

            QSharedPointer<QFutureWatcher<bool>> fw(new QFutureWatcher<bool>);
            connect(fw.data(), &QFutureWatcher<bool>::finished, this, [=] {
                //QGuiApplication::restoreOverrideCursor();
                if (!pCdStatusInfo->bProcessLocked) {
                    DFileService::instance()->setCursorBusyState(false);
                    //QGuiApplication::setOverrideCursor(QCursor(Qt::ArrowCursor)); // bug 31318??? ??????????????????ArrowCursor??????????????????????????????????????????????????????
                }

                if (fw->result()) {
                    cd(fileUrl);
                }
            });
            fw->setFuture(QtConcurrent::run([=] {
                if (pCdStatusInfo->bProcessLocked)
                    return false;
                QMutexLocker locker(getOpticalDriveMutex());
                blkdev->unmount({});

                pCdStatusInfo->bProcessLocked = false;   // ??????unmount ??????????????????????????????????????????????????????????????????

                // fix bug 27211 ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????? QDbusError::Other
                // ??????????????????????????????????????????????????????????????????????????????
                QDBusError err = blkdev->lastError();
                if (err.isValid() && !err.name().toLower().contains("notmounted"))   // ??????????????????Error ?????? Other??????????????? org.freedesktop.UDisks2.Error.NotMounted
                {
                    qDebug() << "disc mount error: " << err.message() << err.name() << err.type();
                    DThreadUtil::runInMainThread([] {
                        dialogManager->showErrorDialog(tr("Disc mount error"), tr("The disc is in use, please end the running process and remount the disc."));
                    });
                    return false;
                }
                if (!ISOMaster->acquireDevice(devpath)) {
                    ISOMaster->releaseDevice();
                    blkdev->unmount({});
                    QThread::msleep(1000);
                    QScopedPointer<DDiskDevice> diskdev(DDiskManager::createDiskDevice(blkdev->drive()));
                    diskdev->eject({});
                    qDebug() << "setRootUrl failed:" << blkdev->drive();
                    if (diskdev->optical()) {
                        emit fileSignalManager->cdFolder(DUrl(COMPUTER_ROOT));   // ???????????????????????????????????????
                        QMetaObject::invokeMethod(dialogManager, std::bind(&DialogManager::showErrorDialog, dialogManager, tr("Mounting failed"), QString()), Qt::ConnectionType::QueuedConnection);
                    }
                    return false;
                }
                ISOMaster->getDeviceProperty();
                ISOMaster->releaseDevice();
                blkdev->mount({});

                return true;
            }));
            d->m_isSetRootUrl = false;
            return false;
        } else {
            d->headerOpticalDisc->updateDiscInfo(fileUrl.burnDestDevice());
            d->headerOpticalDisc->show();
            auto points = blkdev->mountPoints();
            if (points.empty()) {
                blkdev->mount({});
                // ?????????????????????????????????
                points = blkdev->mountPoints();
            }
            if (!blkdev->mountPoints().empty()) {
                d->headerOpticalDisc->setDiscMountPoint(blkdev->mountPoints()[0]);
            } else {
                d->headerOpticalDisc->setDiscMountPoint("");
            }
            d->headerOpticalDisc->setDefaultDiscName(blkdev->idLabel());
        }
    } else {
        d->headerOpticalDisc->hide();
    }

    const DUrl &rootUrl = this->rootUrl();

    qDebug() << "cd: current url:" << rootUrl << "to url:" << fileUrl;

    //    if (rootUrl == fileUrl)
    //        return true;
    //    ??????????????????????????????????????????

    // fix bug 63275
    // ??????????????????QUrlQuery????????????("=")?????????key???value?????????("&")???????????????key-value???
    // ??????????????????????????????????????????&???????????????????????????selectUrl??????
    // ???query???????????????????????????????????????????????????????????????
    QUrlQuery urlQuery;
    QByteArray encode = QUrl::toPercentEncoding(fileUrl.query(QUrl::FullyEncoded), "=");
    urlQuery.setQuery(encode);
    const DUrl &defaultSelectUrl = DUrl(urlQuery.queryItemValue("selectUrl", QUrl::FullyDecoded));

    if (defaultSelectUrl.isValid()) {
        d->preSelectionUrls << defaultSelectUrl;

        urlQuery.removeQueryItem("selectUrl");
        fileUrl.setQuery(urlQuery);
    } else {
        //????????????????????????????????????
        if (!DFileService::instance()->checkGvfsMountfileBusy(rootUrl, false)) {
            QList<DUrl> ancestors;
            if (const DAbstractFileInfoPointer &current_file_info = DFileService::instance()->createFileInfo(this, rootUrl)) {
                if (current_file_info->isAncestorsUrl(fileUrl, &ancestors))
                    d->preSelectionUrls << (ancestors.count() > 1 ? ancestors.at(ancestors.count() - 2) : rootUrl);
            }
        }
    }

    QModelIndex index = model()->setRootUrl(fileUrl);
    d->m_isSetRootUrl = false;
    setRootIndex(index);

    if (!model()->canFetchMore(index)) {
        updateContentLabel();
    }

    model()->setSortRole(d->fileViewStateValue(fileUrl, "sortRole", DFileSystemModel::FileDisplayNameRole).toInt(),
                         static_cast<Qt::SortOrder>(d->fileViewStateValue(fileUrl, "sortOrder", Qt::AscendingOrder).toInt()));

    if (d->headerView) {
        updateListHeaderViewProperty();
        // update header view sort indicator
        QSignalBlocker blocker(d->headerView);
        Q_UNUSED(blocker)
        d->headerView->setSortIndicator(model()->sortColumn(), model()->sortOrder());
    }

    if (info) {
        ViewModes modes = static_cast<ViewModes>(info->supportViewMode());

        //view mode support handler
        toolBarActionList().first()->setVisible(testViewMode(modes, IconMode));
        toolBarActionList().at(1)->setVisible(testViewMode(modes, ListMode));
    }

    emit rootUrlChanged(fileUrl);

    if (fileUrl.isSearchFile()) {
        setViewMode(ListMode);
    }

    // NOTE(zccrs): ??????????????????????????????????????????action???????????????????????????????????????????????????????????????????????????????????????????????????action??????????????????
    if (viewMode() == IconMode) {
        toolBarActionList().first()->setChecked(true);
    } else {
        toolBarActionList().at(1)->setChecked(true);
    }

    const QList<DAbstractFileInfo::SelectionMode> &supportSelectionModes = info->supportSelectionModes();

    for (DAbstractFileInfo::SelectionMode mode : supportSelectionModes) {
        if (d->enabledSelectionModes.contains(static_cast<SelectionMode>(mode))) {
            setSelectionMode(static_cast<SelectionMode>(mode));
            break;
        }
    }

    return true;
}

bool DFileView::getCanDestruct() const
{
    Q_D(const DFileView);
    return !(d->m_isDragging.load() || d->m_isMouseMoveing.load());
}

void DFileView::clearHeardView()
{
    D_D(DFileView);

    if (d->headerView) {
        removeHeaderWidget(1);

        d->headerView->disconnect();
        d->headerView = nullptr;
        d->headerViewHolder = nullptr;
    }
}

void DFileView::clearSelection()
{
    QListView::clearSelection();
    setCurrentIndex(QModelIndex());
}

void DFileView::setContentLabel(const QString &text)
{
    D_D(DFileView);

    if (!d->contentLabel) {
        d->contentLabel = new QLabel(this);
        //        font-family: SourceHanSansSC-Light;
        //        font-size: 20px;
        //        color: #bdbdbd; #D0D0D0;
        QPalette palette = d->contentLabel->palette();
        QStyleOption opt;
        opt.initFrom(d->contentLabel);
        QColor color = opt.palette.color(QPalette::Inactive, QPalette::Text);
        palette.setColor(QPalette::Text, color);
        d->contentLabel->setPalette(palette);
        auto font = d->contentLabel->font();
        font.setFamily("SourceHanSansSC-Light");
        font.setPixelSize(20);
        d->contentLabel->setFont(font);

        d->contentLabel.setCenterIn(this);
        d->contentLabel->setObjectName("contentLabel");
        d->contentLabel->setStyleSheet(this->styleSheet());
        d->contentLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        d->contentLabel->show();
    }

    d->contentLabel->setText(text);
    d->contentLabel->adjustSize();
}

void DFileView::setMenuActionWhitelist(const QSet<MenuAction> &actionList)
{
    Q_D(DFileView);

    d->menuWhitelist = actionList;

    if (focusWidget() == this) {
        DFileMenuManager::setActionWhitelist(d->menuWhitelist);
    }
}

void DFileView::setMenuActionBlacklist(const QSet<MenuAction> &actionList)
{
    Q_D(DFileView);

    d->menuBlacklist = actionList;

    if (focusWidget() == this) {
        DFileMenuManager::setActionBlacklist(d->menuBlacklist);
    }
}

void DFileView::updateHorizontalOffset()
{
    D_D(DFileView);

    if (isIconViewMode()) {
        int contentWidth = maximumViewportSize().width();
        int itemWidth = itemSizeHint().width() + spacing() * 2;
        int itemColumn = d->iconModeColumnCount(itemWidth);

        d->horizontalOffset = -(contentWidth - itemWidth * itemColumn) / 2;
    } else {
        d->horizontalOffset = 0;
    }
}

void DFileView::switchViewMode(DFileView::ViewMode mode)
{
    D_D(DFileView);

    const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(rootUrl());

    if (fileInfo) {
        const auto modes = fileInfo->supportViewMode();
        if (!(modes & mode)) {
            if (modes & DFileView::ListMode) {
                mode = DFileView::ListMode;
            } else if (modes & DFileView::IconMode) {
                mode = DFileView::IconMode;
            } else {
                return;
            }
        }
    }

    if (d->currentViewMode == mode) {
        return;
    }

    d->currentViewMode = mode;

    itemDelegate()->hideAllIIndexWidget();

    horizontalScrollBar()->parentWidget()->removeEventFilter(this);

    switch (mode) {
    case IconMode: {
        clearHeardView();
        d->columnRoles.clear();
        setOrientation(QListView::LeftToRight, true);
        setSpacing(ICON_VIEW_SPACING);
        setItemDelegate(new DIconItemDelegate(d->fileViewHelper));
        setUniformItemSizes(false);
        setResizeMode(Adjust);
        setAlternatingRowColors(false);

        d->statusBar->scalingSlider()->show();
        itemDelegate()->setIconSizeByIconSizeLevel(d->statusBar->scalingSlider()->value());
        d->toolbarActionGroup->actions().first()->setChecked(true);

        break;
    }
    case ListMode: {
        setItemDelegate(new DListItemDelegate(d->fileViewHelper));
        setUniformItemSizes(true);
        setResizeMode(Fixed);
        setAlternatingRowColors(false);

        if (!d->headerView) {
            if (d->allowedAdjustColumnSize) {
                d->headerViewHolder = new QWidget(this);
                d->headerView = new DFMHeaderView(Qt::Horizontal, d->headerViewHolder);

                connect(d->headerView, &DFMHeaderView::viewResized, this, [d] {
                    d->headerViewHolder->setFixedHeight(d->headerView->height());
                });
                connect(d->headerView, &DFMHeaderView::sectionResized, d->headerView, &DFMHeaderView::adjustSize);
                connect(d->headerView, SIGNAL(sectionHandleDoubleClicked(int)), this, SLOT(_q_onSectionHandleDoubleClicked(int)));
            } else {
                d->headerView = new DFMHeaderView(Qt::Horizontal, this);
                d->headerViewHolder = d->headerView;
            }

            updateListHeaderViewProperty();

            d->headerView->setHighlightSections(false);
            d->headerView->setSectionsClickable(true);
            d->headerView->setSortIndicatorShown(true);
            d->headerView->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            d->headerView->setContextMenuPolicy(Qt::CustomContextMenu);

            if (selectionModel()) {
                d->headerView->setSelectionModel(selectionModel());
            }

            connect(d->headerView, &QHeaderView::sectionResized,
                    this, static_cast<void (DFileView::*)()>(&DFileView::update));
            connect(d->headerView, &QHeaderView::sortIndicatorChanged,
                    this, &DFileView::onSortIndicatorChanged);
            connect(d->headerView, &QHeaderView::customContextMenuRequested,
                    this, &DFileView::popupHeaderViewContextMenu);
            connect(d->headerView, &DFMHeaderView::mouseReleased, this, [=] {
                //????????????????????????headerview???view?????????????????????????????????????????????
                if (d->headerView->width() != width())
                    d->toggleHeaderViewSnap(false);
                QList<int> roleList = columnRoleList();
                QVariantMap state;
                for (const int role : roleList) {
                    int colWidth = columnWidth(model()->roleToColumn(role));

                    if (colWidth > 0)
                        state[QString::number(role)] = colWidth;
                }
                DFMApplication::appObtuselySetting()->setValue("WindowManager", "ViewColumnState", state);
            });
            connect(horizontalScrollBar(), &QScrollBar::valueChanged, d->headerView,
                    [d](int value) {
                        if (d->headerView) {
                            d->headerView->move(-value, d->headerView->y());
                        }
                    });

            connect(d->headerView, &DFMHeaderView::sectionMoved, this, &DFileView::onHeaderSectionMoved);

            if (d->allowedAdjustColumnSize) {
                connect(d->headerView, &QHeaderView::sectionResized,
                        this, &DFileView::updateGeometries);
            }

            d->headerView->setAttribute(Qt::WA_TransparentForMouseEvents, model()->state() == DFileSystemModel::Busy);
        }

        addHeaderWidget(d->headerViewHolder);

        setOrientation(QListView::TopToBottom, false);
        setSpacing(LIST_VIEW_SPACING);
        d->statusBar->scalingSlider()->hide();
        d->toolbarActionGroup->actions().at(1)->setChecked(true);

        if (d->allowedAdjustColumnSize) {
            horizontalScrollBar()->parentWidget()->installEventFilter(this);
            // ?????????????????????
            d->cachedViewWidth = this->width();
            //fix task wayland 21328 ????????????????????????????????????????????????
            d->adjustFileNameCol = true;   //fix 31609 ????????????????????????????????????????????????????????????
            updateListHeaderViewProperty();
        }

        break;
    }
    case ExtendMode: {
        break;
    }
    default:
        break;
    }

    setFocus();

    emit viewModeChanged(mode);
}

void DFileView::showEmptyAreaMenu(const Qt::ItemFlags &indexFlags)
{
    Q_UNUSED(indexFlags)
    D_D(DFileView);

    const QModelIndex &index = rootIndex();
    const DAbstractFileInfoPointer &info = model()->fileInfo(index);
    QVector<MenuAction> actions = info->menuActionList(DAbstractFileInfo::SpaceArea);

    // ?????????????????????????????????
    if (DGioSettings::isSchemaInstalled("com.deepin.dde.filemanager.contextmenu")) {
        static const DGioSettings menuSwitch("com.deepin.dde.filemanager.contextmenu",
                                             "/com/deepin/dde/filemanager/contextmenu/");
        auto showRefreh = menuSwitch.value("Refresh");
        if (!showRefreh.isValid() || !showRefreh.toBool())
            actions.removeAll(MenuAction::RefreshView);
    } else {
        actions.removeAll(MenuAction::RefreshView);
        qWarning() << "com.deepin.dde.filemanager.contextmenu is not installed.";
    }

    if (actions.isEmpty())
        return;
    // sp3 feature: root??????, ?????????????????????, ???????????????????????????????????????????????????????????????
    if (DFMGlobal::isRootUser() || DFMGlobal::isServerSys() || !DFMGlobal::isDeveloperMode()) {
        actions.removeAll(MenuAction::OpenAsAdmin);
    }

    const QMap<MenuAction, QVector<MenuAction>> &subActions = info->subMenuActionList(DAbstractFileInfo::SpaceArea);

    QSet<MenuAction> disableList = DFileMenuManager::getDisableActionList(model()->getUrlByIndex(index));

    if (model()->state() != DFileSystemModel::Idle) {
        disableList << MenuAction::SortBy;
    }

    //    if (!indexFlags.testFlag(Qt::ItemIsEditable))
    //        disableList << MenuAction::NewDocument << MenuAction::NewFolder << MenuAction::Paste;

    const bool &tabAddable = WindowManager::tabAddableByWinId(windowId());
    if (!tabAddable)
        disableList << MenuAction::OpenInNewTab;

    if (!count())
        disableList << MenuAction::SelectAll;

    DFileMenu *menu = DFileMenuManager::genereteMenuByKeys(actions, disableList, true, subActions);
    if (!menu) {
        return;
    }

    QAction *tmp_action = menu->actionAt(fileMenuManger->getActionString(MenuAction::DisplayAs));
    DFileMenu *displayAsSubMenu = static_cast<DFileMenu *>(tmp_action ? tmp_action->menu() : Q_NULLPTR);
    tmp_action = menu->actionAt(fileMenuManger->getActionString(MenuAction::SortBy));
    DFileMenu *sortBySubMenu = static_cast<DFileMenu *>(tmp_action ? tmp_action->menu() : Q_NULLPTR);

    for (QAction *action : d->displayAsActionGroup->actions()) {
        d->displayAsActionGroup->removeAction(action);
    }

    if (displayAsSubMenu) {
        foreach (QAction *action, displayAsSubMenu->actions()) {
            action->setActionGroup(d->displayAsActionGroup);
            action->setCheckable(true);
            action->setChecked(false);
        }

        QAction *currentViewModeAction = nullptr;

        if (d->currentViewMode == IconMode) {
            currentViewModeAction = displayAsSubMenu->actionAt(fileMenuManger->getActionString(MenuAction::IconView));
        } else if (d->currentViewMode == ListMode) {
            currentViewModeAction = displayAsSubMenu->actionAt(fileMenuManger->getActionString(MenuAction::ListView));
        } else if (d->currentViewMode == ExtendMode) {
            currentViewModeAction = displayAsSubMenu->actionAt(fileMenuManger->getActionString(MenuAction::ExtendView));
        }

        if (currentViewModeAction)
            currentViewModeAction->setChecked(true);
    }

    for (QAction *action : d->sortByActionGroup->actions()) {
        d->sortByActionGroup->removeAction(action);
    }

    if (sortBySubMenu) {
        foreach (QAction *action, sortBySubMenu->actions()) {
            action->setActionGroup(d->sortByActionGroup);
            action->setCheckable(true);
            action->setChecked(false);
        }

        QAction *action = sortBySubMenu->actionAt(info->sortSubMenuActionUserColumnRoles().indexOf(model()->sortRole()));

        if (action)
            action->setChecked(true);
    }

    DFileMenuManager::loadEmptyAreaPluginMenu(menu, rootUrl(), false);

    //????????????
    if (DFileMenuManager::isCustomMenuSupported(rootUrl()))
        DFileMenuManager::extendCustomMenu(menu, false, rootUrl(), {}, {});

    //so????????????
    bool isFileDialog = this->property("isFileDialog").toBool();
    if (!isFileDialog && DFileMenuManager::isExtensionsSupported(rootUrl()))
        DFileMenuManager::extensionPluginCustomMenu(menu, false, rootUrl(), {}, {});

    menu->setEventData(rootUrl(), selectedUrls(), windowId(), this);

    fileViewHelper()->handleMenu(menu);
    //fix bug 33305 ?????????????????????????????????????????????????????????????????????????????????this???????????????????????????menu???exec?????????menu???deleteLater??????
    QPointer<DFileView> me = this;
    menu->exec();
    menu->deleteLater(me);
}

void DFileView::showNormalMenu(const QModelIndex &index, const Qt::ItemFlags &indexFlags)
{
    if (!index.isValid())
        return;
    DUrlList list = selectedUrls();

    DFileMenu *menu;

#ifdef SW_LABEL
    DAbstractFileInfoPointer info = model()->fileInfo(index);
    info->updateLabelMenuItems();
#else
    const DAbstractFileInfoPointer &info = model()->fileInfo(index);
#endif

    if (!info || !info->exists()) {
        qDebug() << "info is null or not exists,so exit";
        return;
    }

    QSet<MenuAction> disableList;
    QSet<MenuAction> unusedList;

    // blumia: when touching this part, do the same change in canvasgridview.cpp
    if (list.size() == 1) {

        if (!info->isWritable() && !info->isFile() && !info->isSymLink()) {
            disableList << MenuAction::Delete;
        }

        if (!indexFlags.testFlag(Qt::ItemIsEditable)) {
            disableList << MenuAction::Rename;
        }
    }

    // ?????????????????????????????????????????????????????????
    static bool lock = false;
    qint64 curTime = QDateTime::currentMSecsSinceEpoch();
    static qint64 lastTime = QDateTime::currentMSecsSinceEpoch();

    // ?????????????????????????????????, < 500 ??????????????????????????????, ????????????
    // ????????????????????????????????????????????????????????????, exec() ??????????????????, lock??????????????????
    // ??????bug: 47478
    qint64 intervalime = curTime - lastTime;
    if (lock && (intervalime < 500)) {
        qDebug() << "reject show menu";
        return;
    }

    if (VaultController::isRootDirectory(info->fileUrl().fragment())) {
        //! create vault menu.
        menu = DFileMenuManager::createVaultMenu(this->topLevelWidget());
    } else {
        if (DFileService::instance()->checkGvfsMountfileBusy(info->fileUrl()))
            return;
        menu = DFileMenuManager::createNormalMenu(info->fileUrl(), list, disableList, unusedList, static_cast<int>(windowId()), false);
        menu->setAccessibleInfo(AC_FILE_MENU_FILEVIEW);
    }
    lock = true;

    if (!menu) {
        lock = false;
        return;
    }

    //????????????
    {
        //???????????????????????????
        const DUrl &viewRootUrl = rootUrl();
        bool customMenu = DFileMenuManager::isCustomMenuSupported(viewRootUrl);
        if (customMenu && (viewRootUrl == DUrl::fromLocalFile(DFMStandardPaths::location(DFMStandardPaths::DesktopPath)))) {
            //??????????????? ????????? ?????????
            const DUrl &computerDesktopFile = DesktopFileInfo::computerDesktopFileUrl();
            const DUrl &trashDesktopFile = DesktopFileInfo::trashDesktopFileUrl();
            const DUrl &homeDesktopFile = DesktopFileInfo::homeDesktopFileUrl();

            //???????????????????????????????????? ???????????????????????????
            for (const DUrl &url : list) {
                if (url == computerDesktopFile
                    || url == trashDesktopFile
                    || url == homeDesktopFile) {
                    customMenu = false;
                    break;
                }
            }
        }

        if (customMenu)
            DFileMenuManager::extendCustomMenu(menu, true, viewRootUrl, info->fileUrl(), list);

        //so????????????
        bool isFileDialog = this->property("isFileDialog").toBool();
        if (!isFileDialog && DFileMenuManager::isExtensionsSupported(rootUrl()))
            DFileMenuManager::extensionPluginCustomMenu(menu, true, rootUrl(), {}, {});
    }

    menu->setEventData(rootUrl(), selectedUrls(), windowId(), this, index);

    fileViewHelper()->handleMenu(menu);
    // ???????????????this???????????????????????????view????????????view???????????????????????????????????????
    // ????????????????????? activewindow????????????????????????????????????????????????view????????????
    //fix bug 33305 ?????????????????????????????????????????????????????????????????????????????????this????????????
    //???????????????menu???exec?????????menu???deleteLater??????
    QPointer<QWidget> window = qApp->activeWindow();
    lastTime = QDateTime::currentMSecsSinceEpoch();
    menu->exec();
    menu->deleteLater(window);
    lock = false;
}

void DFileView::updateListHeaderViewProperty()
{
    D_D(DFileView);

    if (!d->headerView)
        return;

    d->headerView->setModel(Q_NULLPTR);
    d->headerView->setModel(model());

    d->headerView->setDefaultSectionSize(DEFAULT_HEADER_SECTION_WIDTH);

    if (d->allowedAdjustColumnSize) {
        d->headerView->setSectionResizeMode(QHeaderView::Interactive);
        d->headerView->setMinimumSectionSize(LIST_VIEW_MINIMUM_WIDTH);
    } else {
        d->headerView->setSectionResizeMode(QHeaderView::Fixed);
        d->headerView->setMinimumSectionSize(DEFAULT_HEADER_SECTION_WIDTH);
    }

    d->headerView->setSortIndicator(model()->sortColumn(), model()->sortOrder());
    d->columnRoles.clear();

    // set value from config file.
    const QVariantMap &state = DFMApplication::appObtuselySetting()->value("WindowManager", "ViewColumnState").toMap();

    for (int i = 0; i < d->headerView->count(); ++i) {
        int logicalIndex = d->headerView->logicalIndex(i);
        d->columnRoles << model()->columnToRole(i);

        if (d->allowedAdjustColumnSize) {
            int colWidth = state.value(QString::number(d->columnRoles.last()), -1).toInt();
            if (colWidth > 0) {
                d->headerView->resizeSection(model()->roleToColumn(d->columnRoles.last()), colWidth);
            }
        } else {
            int column_width = model()->columnWidth(i);
            if (column_width >= 0) {
                d->headerView->resizeSection(logicalIndex, column_width + COLUMU_PADDING * 2);
            } else {
                d->headerView->setSectionResizeMode(logicalIndex, QHeaderView::Stretch);
            }
        }

        const QString &column_name = model()->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();

        if (!d->columnForRoleHiddenMap.contains(column_name)) {
            d->headerView->setSectionHidden(logicalIndex, !model()->columnDefaultVisibleForRole(model()->columnToRole(i)));
        } else {
            d->headerView->setSectionHidden(logicalIndex, d->columnForRoleHiddenMap.value(column_name));
        }
    }

    if (d->adjustFileNameCol) {
        d->doFileNameColResize();
    }

    updateColumnWidth();
}

void DFileView::updateExtendHeaderViewProperty()
{
    D_D(DFileView);

    if (!d->headerView)
        return;

    d->headerView->setModel(Q_NULLPTR);
    d->headerView->setModel(model());
    d->headerView->setDefaultSectionSize(DEFAULT_HEADER_SECTION_WIDTH);

    if (!d->allowedAdjustColumnSize) {
        d->headerView->setSectionResizeMode(QHeaderView::Fixed);
        d->headerView->setSectionResizeMode(0, QHeaderView::Stretch);
        d->headerView->setMinimumSectionSize(DEFAULT_HEADER_SECTION_WIDTH);
    }

    d->columnRoles.clear();
    d->columnRoles << model()->columnToRole(0);
}

void DFileView::updateColumnWidth()
{
    D_D(DFileView);

    if (!d->allowedAdjustColumnSize) {
        int column_count = d->headerView->count();
        int i = 0;
        int j = column_count - 1;

        for (; i < column_count; ++i) {
            int logicalIndex = d->headerView->logicalIndex(i);
            if (d->headerView->isSectionHidden(logicalIndex))
                continue;

            d->headerView->resizeSection(logicalIndex, model()->columnWidth(i) + LEFT_PADDING + LIST_MODE_LEFT_MARGIN + 2 * COLUMU_PADDING);
            break;
        }

        for (; j > 0; --j) {
            int logicalIndex = d->headerView->logicalIndex(j);
            if (d->headerView->isSectionHidden(logicalIndex))
                continue;

            d->headerView->resizeSection(logicalIndex, model()->columnWidth(j) + RIGHT_PADDING + LIST_MODE_RIGHT_MARGIN + 2 * COLUMU_PADDING);
            break;
        }

        if (d->firstVisibleColumn != i) {
            if (d->firstVisibleColumn > 0)
                d->headerView->resizeSection(d->headerView->logicalIndex(d->firstVisibleColumn), model()->columnWidth(d->firstVisibleColumn) + 2 * COLUMU_PADDING);

            d->firstVisibleColumn = i;
        }

        if (d->lastVisibleColumn != j) {
            if (d->lastVisibleColumn > 0)
                d->headerView->resizeSection(d->headerView->logicalIndex(d->lastVisibleColumn), model()->columnWidth(d->lastVisibleColumn) + 2 * COLUMU_PADDING);

            d->lastVisibleColumn = j;
        }
    }
}

void DFileView::popupHeaderViewContextMenu(const QPoint &pos)
{
    D_D(DFileView);
    const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(rootIndex());
    DFileMenu *menu = new DFileMenu();

    if (fileInfo && fileInfo->columnIsCompact()) {
        /*contextmenu of headview for sort function*/
        int column = d->headerView->logicalIndexAt(pos.x());

        const QList<int> &childRoles = fileInfo->userColumnChildRoles(column);

        if (childRoles.isEmpty()) {
            //fix bug 33305 ?????????????????????????????????????????????????????????????????????????????????this????????????
            //???????????????menu???exec?????????menu???deleteLater??????
            QPointer<DFileView> me = this;
            menu->deleteLater(me);

            return;
        }

        for (int i = 0; i < childRoles.count() * 2; ++i) {
            int childRole = childRoles.at(i / 2);
            QAction *action = new QAction(menu);

            if (i % 2 == 0) {
                action->setText(fileInfo->userColumnDisplayName(childRole).toString());
            } else if (i % 2 == 1) {
                action->setText(fileInfo->userColumnDisplayName(childRole).toString() + tr("(Reverse)"));
            }

            action->setCheckable(true);

            if (!d->headerView->isSectionHidden(d->headerView->sortIndicatorSection())
                && model()->sortRole() == childRole) {
                if (i % 2 == 1 && model()->sortOrder() == Qt::DescendingOrder) {
                    action->setChecked(true);
                }
                if (i % 2 == 0 && model()->sortOrder() == Qt::AscendingOrder) {
                    action->setChecked(true);
                }
            }

            connect(action, &QAction::triggered, this, [this, i, childRoles] {
                if (i % 2 == 0) {
                    sortByRole(childRoles.at(i / 2), Qt::AscendingOrder);
                } else if (i % 2 == 1) {
                    sortByRole(childRoles.at(i / 2), Qt::DescendingOrder);
                }
            });

            menu->addAction(action);
        }
    } else {
        for (int i = 0; i < d->headerView->count(); ++i) {
            //????????????????????????
            if (model()->columnToRole(i) == DFileSystemModel::FileNameRole || model()->columnToRole(i) == DFileSystemModel::FileDisplayNameRole)
                continue;

            QAction *action = new QAction(menu);

            action->setText(model()->columnNameByRole(model()->columnToRole(i)).toString());
            action->setCheckable(true);
            action->setChecked(!d->headerView->isSectionHidden(i));

            connect(action, &QAction::triggered, this, [this, action, i, d] {
                action->setChecked(!action->isChecked());
                d->columnForRoleHiddenMap[action->text()] = action->isChecked();

                d->headerView->setSectionHidden(i, action->isChecked());

                // fix bug#36610 ???????????????????????????????????????????????????
                if (d->allowedAdjustColumnSize) {
                    updateListHeaderViewProperty();
                } else {
                    updateColumnWidth();
                }
            });

            menu->addAction(action);
        }
    }
    //fix bug 33305 ?????????????????????????????????????????????????????????????????????????????????this????????????
    //???????????????menu???exec?????????menu???deleteLater??????
    QPointer<DFileView> me = this;
    menu->exec(QCursor::pos());
    menu->deleteLater(me);
}

void DFileView::onModelStateChanged(int state)
{
    D_D(DFileView);

    DFMEvent event(this);

    event.setWindowId(windowId());
    event.setData(rootUrl());

    if (state == DFileSystemModel::Busy) {
        QString tipText;

        if (const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(rootIndex())) {
            tipText = fileInfo->loadingTip();
        }

        d->statusBar->setLoadingIncatorVisible(state == DFileSystemModel::Busy, tipText);

        setContentLabel(QString());

        if (d->headerView) {
            d->headerView->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
    } else if (state == DFileSystemModel::Idle) {
        d->statusBar->setLoadingIncatorVisible(state == DFileSystemModel::Busy);

        if (!d->preSelectionUrls.isEmpty()) {
            const QModelIndex &index = model()->index(d->preSelectionUrls.first());

            setCurrentIndex(index);
            scrollTo(index, PositionAtTop);
        }

        for (const DUrl &url : d->preSelectionUrls) {
            selectionModel()->select(model()->index(url), QItemSelectionModel::Select);
        }

        d->preSelectionUrls.clear();

        delayUpdateStatusBar();
        updateContentLabel();

        if (d->headerView) {
            d->headerView->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        }
    }

    // ????????????????????????
    notifyStateChanged();
}

void DFileView::updateContentLabel()
{
    if (model()->state() != DFileSystemModel::Idle
        || model()->canFetchMore(rootIndex())) {
        setContentLabel(QString());

        return;
    }

    int count = this->count();

    if (count <= 0) {
        const DAbstractFileInfoPointer &fileInfo = model()->fileInfo(rootIndex());

        if (fileInfo) {
            setContentLabel(fileInfo->subtitleForEmptyFloder());
            return;
        }
    }

    setContentLabel(QString());
}

void DFileView::updateToolBarActions(QWidget *widget, QString theme)
{
    Q_UNUSED(theme)
    D_D(DFileView);
    if (widget == this) {
        QAction *icon_view_mode_action;
        QAction *list_view_mode_action;
        const QList<QAction *> actions = d->toolbarActionGroup->actions();

        if (actions.count() > 1) {
            icon_view_mode_action = actions.first();
            list_view_mode_action = actions.at(1);
        } else {
            icon_view_mode_action = new QAction(this);
            list_view_mode_action = new QAction(this);

            icon_view_mode_action->setCheckable(true);
            icon_view_mode_action->setChecked(true);

            list_view_mode_action->setCheckable(true);

            d->toolbarActionGroup->addAction(icon_view_mode_action);
            d->toolbarActionGroup->addAction(list_view_mode_action);
        }

        icon_view_mode_action->setIcon(QIcon::fromTheme("dfm_viewlist_icons"));
        list_view_mode_action->setIcon(QIcon::fromTheme("dfm_viewlist_details"));
    }
}

bool DFileView::normalKeyPressEvent(const QKeyEvent *event)
{
    D_D(DFileView);

    const DUrlList urls = selectedUrls();
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!itemDelegate()->editingIndex().isValid()) {
            appController->actionOpen(dMakeEventPointer<DFMUrlListBaseEvent>(this, urls), true);
            return true;
        }
        break;
    case Qt::Key_Backspace: {
        // blmark: revert commit vbfdf8e575447249ba284402bfac8a512bae2d10e
        cdUp();
        return true;
    }
    case Qt::Key_Delete: {
        QString rootPath = rootUrl().toLocalFile();
        if (FileUtils::isGvfsMountFile(rootPath) || deviceListener->isInRemovableDeviceFolder(rootPath) || VaultController::isVaultFile(rootPath)) {
            appController->actionCompleteDeletion(dMakeEventPointer<DFMUrlListBaseEvent>(this, urls));
        } else {
            //! refresh after vault file deleted.
            if (urls.size() > 0) {
                QString filepath = urls.front().toLocalFile();
                if (VaultController::isVaultFile(filepath) && !d->isVaultDelSigConnected) {
                    d->isVaultDelSigConnected = true;
                }
            }

            // ???????????????????????????????????????????????????
            if (rootUrl().isTrashFile() && rootUrl() != DUrl::fromTrashFile("/"))
                break;

            appController->actionDelete(dMakeEventPointer<DFMUrlListBaseEvent>(this, urls));
        }
        break;
    }
    case Qt::Key_End:
        if (urls.isEmpty()) {
            setCurrentIndex(model()->index(count() - 1, 0));
            return true;
        }
    default:
        break;
    }

    return false;
}

void DFileView::refresh()
{
    model()->refresh();
}

bool DFileView::fetchDragEventUrlsFromSharedMemory()
{
    QSharedMemory sm;
    sm.setKey(DRAG_EVENT_URLS);

    if (!sm.isAttached()) {
        if (!sm.attach()) {
            qDebug() << "FQSharedMemory detach failed.";
            return false;
        }
    }

    QBuffer buffer;
    QDataStream in(&buffer);
    QList<QUrl> urls;

    sm.lock();
    //?????????????????????????????????????????????????????????????????????
    buffer.setData(static_cast<char *>(const_cast<void *>(sm.constData())), sm.size());
    buffer.open(QBuffer::ReadOnly);   //??????????????????
    in >> m_urlsForDragEvent;   //????????????????????????????????????????????????????????????????????????????????????
    qInfo() << "drop file urls = " << m_urlsForDragEvent << "to current url = " << rootUrl();
    sm.unlock();   //??????
    sm.detach();   //???????????????????????????

    return true;
}

void DFileView::setTargetUrlToApp(const QMimeData *data, const DUrl &url)
{
    //??????target????????????????????????DFileDragClient::setTargetUrl
    if (!m_currentTargetUrl.isValid() || m_currentTargetUrl.path() != url.path()) {
        m_currentTargetUrl = url;
        DFileDragClient::setTargetUrl(data, url);
    }
}

void DFileView::showCustomDragPixmap(const QModelIndexList &indexes, Qt::DropActions supportedActions)
{
    Q_D(DFileView);

    QMimeData *data = model()->mimeData(indexes);
    if (!data || indexes.isEmpty())
        return;

    QPixmap pixmap;
    if (indexes.count() == 1) {
        pixmap = d->getPixmap(indexes.at(0));
    } else {
        pixmap = d->renderToPixmap(indexes);
    }
    QDrag *drag = new QDrag(this);
    drag->setPixmap(pixmap);
    drag->setMimeData(data);
    drag->setHotSpot(QPoint(static_cast<int>(pixmap.size().width() / (2 * pixmap.devicePixelRatio())),
                            static_cast<int>(pixmap.size().height() / (2 * pixmap.devicePixelRatio()))));
    Qt::DropAction dropAction = Qt::IgnoreAction;
    Qt::DropAction defaultDropAction = QAbstractItemView::defaultDropAction();
    if (defaultDropAction != Qt::IgnoreAction && (supportedActions & defaultDropAction))
        dropAction = defaultDropAction;
    else if (supportedActions & Qt::CopyAction && dragDropMode() != QAbstractItemView::InternalMove)
        dropAction = Qt::CopyAction;

    drag->exec(supportedActions, dropAction);
}

void DFileView::setViewSelectState(bool bSelect)
{
    bShowViewSelectBox = bSelect;
    viewport()->update();
}

int DFileViewPrivate::iconModeColumnCount(int itemWidth) const
{
    Q_Q(const DFileView);

    //    int frameAroundContents = 0;
    //    if (q->style()->styleHint(QStyle::SH_ScrollView_FrameOnlyAroundContents))
    //        frameAroundContents = q->style()->pixelMetric(QStyle::PM_DefaultFrameWidth) * 2;

    int horizontalMargin = /*q->verticalScrollBarPolicy()==Qt::ScrollBarAsNeeded
            ? q->style()->pixelMetric(QStyle::PM_ScrollBarExtent, 0, q->verticalScrollBar()) + frameAroundContents
            : */
            0;

    int contentWidth = q->maximumViewportSize().width();

    if (itemWidth <= 0)
        itemWidth = q->itemSizeHint().width() + q->spacing() * 2;

    return qMax((contentWidth - horizontalMargin - 1) / itemWidth, 1);
}

QVariant DFileViewPrivate::fileViewStateValue(const DUrl &url, const QString &key, const QVariant &defalutValue)
{
    return DFMApplication::appObtuselySetting()->value("FileViewState", url).toMap().value(key, defalutValue);
}

void DFileViewPrivate::setFileViewStateValue(const DUrl &url, const QString &key, const QVariant &value)
{
    QVariantMap map = DFMApplication::appObtuselySetting()->value("FileViewState", url).toMap();

    map[key] = value;

    DFMApplication::appObtuselySetting()->setValue("FileViewState", url, map);
}

void DFileViewPrivate::updateHorizontalScrollBarPosition()
{
    Q_Q(DFileView);

    QWidget *widget = static_cast<QWidget *>(q->horizontalScrollBar()->parentWidget());

    // ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????
    widget->move(widget->x(), q->height() - statusBar->height() - widget->height());
}

void DFileViewPrivate::pureResizeEvent(QResizeEvent *event)
{
    Q_Q(DFileView);

    if (currentViewMode == DFileView::ListMode) {   //????????????????????????????????????
        if (showCount == 1) {   //fix ??????25717 ????????????????????????????????????????????????????????????????????????????????????????????????????????????????????????
            adjustFileNameCol = q->width() >= headerView->width();
            showCount++;   //???????????????????????????????????????1
        }
    }

    if (!allowedAdjustColumnSize) {
        // auto switch list mode
        if (currentViewMode == DFileView::ListMode
            && DFMApplication::instance()->appAttribute(DFMApplication::AA_ViewAutoCompace).toBool()) {
            if (q->model()->setColumnCompact(event->size().width() < 600)) {
                q->updateListHeaderViewProperty();
                q->doItemsLayout();
            }
        }
    } else {
        doFileNameColResize();
    }
}

void DFileViewPrivate::doFileNameColResize()
{
    Q_Q(DFileView);

    if (allowedAdjustColumnSize && headerView && adjustFileNameCol) {
        int fileNameColRole = q->model()->roleToColumn(DFileSystemModel::FileDisplayNameRole);
        int columnCount = headerView->count();
        int columnWidthSumOmitFileName = 0;
        for (int i = 0; i < columnCount; ++i) {
            if (i == fileNameColRole || headerView->isSectionHidden(i))
                continue;
            columnWidthSumOmitFileName += q->columnWidth(i);
        }

        int targetWidth = q->width() - columnWidthSumOmitFileName;
        if (targetWidth >= headerView->minimumSectionSize()) {
            headerView->resizeSection(fileNameColRole, q->width() - columnWidthSumOmitFileName);
        } else {
            // fix bug#39026 ??????????????????????????????????????????????????????????????????????????????????????????????????????????????????
            // ?????????????????????????????????targetWidth????????????headerView->minimumSectionSize()???80??????
            // ????????????????????????if??????????????????????????????????????????????????????
            headerView->resizeSection(fileNameColRole, headerView->minimumSectionSize());
        }
    }
}

void DFileViewPrivate::toggleHeaderViewSnap(bool on)
{
    adjustFileNameCol = on;
    //    DFMApplication::appObtuselySetting()->setValue("WindowManager", "HeaderViewSnapped", on);
}

void DFileViewPrivate::_q_onSectionHandleDoubleClicked(int logicalIndex)
{
    Q_Q(DFileView);

    if (q->model()->state() != DFileSystemModel::Idle)
        return;

    int row_count = q->model()->rowCount();

    if (row_count < 1) {
        return;
    }

    QStyleOptionViewItem option = q->viewOptions();

    option.rect.setWidth(QWIDGETSIZE_MAX);
    option.rect.setHeight(q->itemSizeHint().height());

    int column_max_width = 0;

    for (int i = 0; i < row_count; ++i) {
        const QModelIndex &index = q->model()->index(i, 0);
        const QList<QRect> &list = q->itemDelegate()->paintGeomertys(option, index, true);

        // ???0????????????????????????????????????????????????????????????????????????????????????????????????
        int width = 0;

        if (logicalIndex == 0) {
            width = list.at(1).right() + COLUMU_PADDING / 2;
        } else {
            width = list.at(logicalIndex + 1).width() + COLUMU_PADDING * 2;
        }

        if (width > column_max_width) {
            column_max_width = width;
        }
    }

    for (int i = headerView->count() - 1; i >= 0; --i) {
        if (headerView->isSectionHidden(i))
            continue;

        // ????????????????????????????????????margin
        if (i == logicalIndex)
            column_max_width += LIST_MODE_RIGHT_MARGIN;

        break;
    }

    headerView->resizeSection(logicalIndex, column_max_width);
}

QModelIndexList DFileViewPrivate::selectedDraggableIndexes() const
{
    Q_Q(const DFileView);

    QModelIndexList indexes = q->selectedIndexes();
    auto isNotDragEnabled = [=](const QModelIndex &index) {
        return !(q->model()->flags(index) & Qt::ItemIsDragEnabled);
    };

    indexes.erase(std::remove_if(indexes.begin(), indexes.end(), isNotDragEnabled),
                  indexes.end());

    return indexes;
}

QPixmap DFileViewPrivate::renderToPixmap(const QModelIndexList &indexes) const
{
    Q_Q(const DFileView);

    qreal scale = 1;
    QWidget *window = q->window();
    if (window) {
        QWindow *windowHandle = window->windowHandle();
        if (windowHandle)
            scale = windowHandle->devicePixelRatio();
    }

    //??????????????????index??????
    QModelIndexList indexesWithoutPressed = indexes;
    auto needRemove = [=](const QModelIndex &index) {
        return index.row() == m_currentPressedIndex.row();
    };
    indexesWithoutPressed.erase(std::remove_if(indexesWithoutPressed.begin(), indexesWithoutPressed.end(), needRemove),
                                indexesWithoutPressed.end());

    QRect pixRect(0, 0, DRAGICON_SIZE + DRAGICON_OUTLINE * 2, DRAGICON_SIZE + DRAGICON_OUTLINE * 2);
    QPixmap pixmap(pixRect.size() * scale);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QStyleOptionViewItem option = q->viewOptions();
    option.state |= QStyle::State_Selected;
    option.rect = option.rect.translated(DRAGICON_OUTLINE, DRAGICON_OUTLINE);

    qreal offsetX = pixRect.width() / 2;
    qreal offsetY = pixRect.height() / 2;
    for (int i = qMin(DRAGICON_MAX - 1, indexesWithoutPressed.length() - 1); i >= 0; --i) {
        //??????????????????
        qreal rotate = DRAGICON_ROTATE * (ceil((i + 1.0) / 2.0) / 2.0 + 1.0) * (i % 2 == 1 ? -1 : 1);
        //???????????????
        painter.setOpacity(1.0 - (i + 5) * DRAGICON_OPACITY);

        //??????
        painter.translate(offsetX, offsetY);   //???????????????????????????????????????
        painter.rotate(rotate);
        painter.translate(-offsetX, -offsetY);   //???????????????

        //??????icon
        q->itemDelegate()->paintDragIcon(&painter, option, indexesWithoutPressed.at(i), QSize(DRAGICON_SIZE, DRAGICON_SIZE));

        //??????????????????
        painter.translate(offsetX, offsetY);
        painter.rotate(-rotate);
        painter.translate(-offsetX, -offsetY);
    }

    //?????????????????????icon
    painter.setOpacity(0.8);
    q->itemDelegate()->paintDragIcon(&painter, option, m_currentPressedIndex, QSize(DRAGICON_SIZE, DRAGICON_SIZE));
    QSize iconSize = q->itemDelegate()->getIndexIconSize(option, m_currentPressedIndex, QSize(DRAGICON_SIZE, DRAGICON_SIZE));

    //?????????????????????????????????99??????????????????99+
    int length = indexes.length() > DRAGICON_MAX_COUNT ? 28 : 24;   //???????????????1???2??????????????????24???3??????????????????28
    int x = DRAGICON_OUTLINE + (DRAGICON_SIZE + iconSize.width() - length) / 2;
    int y = DRAGICON_OUTLINE + (DRAGICON_SIZE + iconSize.height() - length) / 2;

    QColor pointColor(244, 74, 74);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(1);
    painter.setPen(pointColor);
    painter.setBrush(pointColor);
    painter.drawEllipse(x, y, length, length);

    painter.setPen(Qt::white);
    //????????????????????????????????????Arial???12??????,??????
    QFont ft("Arial");
    ft.setPixelSize(12);
    ft.setBold(true);
    painter.setFont(ft);
    QString countStr = indexes.length() > DRAGICON_MAX_COUNT ? QString::number(DRAGICON_MAX_COUNT).append("+") : QString::number(indexes.length());
    painter.drawText(QRect(x, y, length, length), Qt::AlignCenter, countStr);

    return pixmap;
}

QPixmap DFileViewPrivate::getPixmap(const QModelIndex &index) const
{
    Q_Q(const DFileView);

    qreal scale = 1;
    QWidget *window = q->window();
    if (window) {
        QWindow *windowHandle = window->windowHandle();
        if (windowHandle)
            scale = windowHandle->devicePixelRatio();
    }

    QRect pixRect(0, 0, LIST_DRAGRECT_WIDTH, DRAGICON_SIZE + LIST_DRAGTEXT_HEIGTH);
    QPixmap pixmap(pixRect.size() * scale);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QStyleOptionViewItem option = q->viewOptions();
    option.state |= QStyle::State_Selected;
    option.rect = option.rect.translated((LIST_DRAGRECT_WIDTH - DRAGICON_SIZE)/2, 0);

    // ?????????????????????icon
    q->itemDelegate()->paintDragIcon(&painter, option, m_currentPressedIndex, QSize(DRAGICON_SIZE, DRAGICON_SIZE));

    // ???????????????
    QString fileName = q->model()->data(index, DFileSystemModel::FileDisplayNameRole).toString();
    QTextLayout layout;
    layout.setText(fileName);
    layout.setFont(painter.font());
    painter.setPen(Qt::white);
    QRectF boundingRect((LIST_DRAGRECT_WIDTH - LIST_DRAGTEXT_WIDTH)/2, DRAGICON_SIZE, LIST_DRAGTEXT_WIDTH, LIST_DRAGTEXT_HEIGTH);
    QTextOption::WrapMode wordWrap(QTextOption::WrapAtWordBoundaryOrAnywhere);
    Qt::TextElideMode mode(Qt::ElideLeft);
    int textLineHeight = q->fontMetrics().lineSpacing();
    int flags = Qt::AlignHCenter;
    QColor shadowColor;
    QBrush background(q->palette().color(QPalette::ColorGroup::Active, QPalette::ColorRole::Highlight));
    DFMGlobal::elideText(&layout, boundingRect.size(), wordWrap, mode, textLineHeight, flags, nullptr,
                         &painter, boundingRect.topLeft(), shadowColor, QPointF(0, 1),
                         background);

    return pixmap;
}

#include "moc_dfileview.cpp"
