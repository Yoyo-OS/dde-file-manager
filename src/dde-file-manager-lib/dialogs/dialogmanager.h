/*
 * Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
 *
 * Author:     gongheng<gongheng@uniontech.com>
 *
 * Maintainer: zhengyouge<zhengyouge@uniontech.com>
 *             gongheng<gongheng@uniontech.com>
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

#ifndef DIALOGMANAGER_H
#define DIALOGMANAGER_H

#include <memory>

#include <QObject>
#include <QMap>
#include <QIcon>
#include <QPointer>
#include <QMutex>
#include <QSharedPointer>

#include "durl.h"
#include "dfmglobal.h"
#include "dtkwidget_global.h"

class DTaskDialog;
class FileJob;
class DAbstractFileInfo;
class DUrl;
class DFMEvent;
class DFMUrlBaseEvent;
class DFMUrlListBaseEvent;
class PropertyDialog;
class CloseAllDialogIndicator;
class TrashPropertyDialog;
class ComputerPropertyDialog;
class QTimer;
class DMultiFilePropertyDialog;
class QDiskInfo;

DFM_BEGIN_NAMESPACE
class FilePreviewDialog;
DFM_END_NAMESPACE

DWIDGET_BEGIN_NAMESPACE
class DDialog;
DWIDGET_END_NAMESPACE

class DialogManager : public QObject
{
    Q_OBJECT
public:
    enum messageType {
        msgInfo = 1,
        msgWarn = 2,
        msgErr = 3
    };

public:
    explicit DialogManager(QObject *parent = nullptr);
    ~DialogManager();
    QPoint getPropertyPos(int dialogWidth, int dialogHeight);
    QPoint getPerportyPos(int dialogWidth, int dialogHeight, int count, int index);
    bool isTaskDialogEmpty();
    DTaskDialog *taskDialog() const;

private:
    void initData();
    void initTaskDialog();
    void initCloseIndicatorDialog();
    void initConnect();

public slots:
    void handleConflictRepsonseConfirmed(const QMap<QString, QString> &jobDetail, const QMap<QString, QVariant> &response);
    void addJob(QSharedPointer<FileJob> job);
    void removeJob(const QString &jobId, bool isRemoveOpticalJob = false);
    QString getJobIdByUrl(const DUrl &url);
    void removeAllJobs();
    void updateJob();
    void startUpdateJobTimer();
    void stopUpdateJobTimer();

    void abortJob(const QMap<QString, QString> &jobDetail);
    void abortJobByDestinationUrl(const DUrl &url);

    void showCopyMoveToSelfDialog(const QMap<QString, QString> &jobDetail);

    void showUrlWrongDialog(const DUrl &url);
    int showRunExcutableScriptDialog(const DUrl &url, quint64 winId);
    int showRunExcutableFileDialog(const DUrl &url, quint64 winId);
    int showAskIfAddExcutableFlagAndRunDialog(const DUrl &url, quint64 winId);
    int showRenameNameSameErrorDialog(const QString &name, const DFMEvent &event);
    // ???????????????????????????????????????..???????????????????????????
    int showRenameNameDotDotErrorDialog(const DFMEvent &event);
    void showRenameBusyErrDialog(const DFMEvent &event);
    int showOpticalBlankConfirmationDialog(const DFMUrlBaseEvent &event);
    int showOpticalImageOpSelectionDialog(const DFMUrlBaseEvent &event);
    void showOpticalJobFailureDialog(int type, const QString &err, const QStringList &details);
    void showOpticalJobCompletionDialog(const QString &msg, const QString &icon);
    int showDeleteFilesClearTrashDialog(const DFMUrlListBaseEvent &event);
    //! ?????????????????????????????????
    int showNormalDeleteConfirmDialog(const DFMUrlListBaseEvent &event);
    int showRemoveBookMarkDialog(const DFMEvent &event);
    void showOpenWithDialog(const DFMEvent &event);
    void showOpenFilesWithDialog(const DFMEvent &event);
    void showPropertyDialog(const DFMUrlListBaseEvent &event);
    void showShareOptionsInPropertyDialog(const DFMUrlListBaseEvent &event);
    void showTrashPropertyDialog(const DFMEvent &event);
    void showComputerPropertyDialog();
    void showDevicePropertyDialog(const DFMEvent &event);
    void showDiskErrorDialog(const QString &id, const QString &errorText);
    void showBreakSymlinkDialog(const QString &targetName, const DUrl &linkfile);
    void showConnectToServerDialog(quint64 winId);
    void showUserSharePasswordSettingDialog(quint64 winId);
    void showGlobalSettingsDialog(quint64 winId);
    void showDiskSpaceOutOfUsedDialogLater();
    void showDiskSpaceOutOfUsedDialog();
    void show4gFat32Dialog();
    void showFailToCreateSymlinkDialog(const QString &errorString);
    void showMoveToTrashConflictDialog(const DUrlList &urls);
    void showDeleteSystemPathWarnDialog(quint64 winId);
    void showFilePreviewDialog(const DUrlList &selectUrls, const DUrlList &entryUrls);
    void showRestoreFailedDialog(const DUrlList &urlList);
    void showRestoreFailedPerssionDialog(const QString &srcPath, const QString &targetPath);
    void showRestoreFailedSourceNotExists(const DUrlList &urlList);
    void showMultiFilesRenameDialog(const QList<DUrl> &selectedUrls);
    void showAddUserShareFailedDialog(const QString &sharePath);
    void showNoPermissionDialog(const DFMUrlListBaseEvent &event);
    void showErrorDialog(const QString &title, const QString &message);
    void removePropertyDialog(const DUrl &url);
    void closeAllPropertyDialog();
    void updateCloseIndicator();
    void raiseAllPropertyDialog();
    void handleFocusChanged(QWidget *old, QWidget *now);
    void showTaskProgressDlgOnActive();
    int showUnableToLocateDir(const QString &dir);

    void refreshPropertyDialogs(const DUrl &oldUrl, const DUrl &newUrl);

    int showMessageDialog(messageType messageLevel, const QString &title, const QString &message = "", QString btnTxt = tr("Confirm","button"));
    void showBluetoothTransferDlg(const DUrlList &files);

    void showFormatDialog(const QString &devId); // sp3 feat ??????usb????????????????????????????????????????????????????????????????????????????????????????????????????????????
    int showStopScanningDialog();              // ????????????????????????????????????

    /**
    * DUrlListCompare ??????????????????url???????????????m_urlList??????
    */
    bool DUrlListCompare(DUrlList urls);

#ifdef SW_LABEL
    void onJobFailed_SW(int nRet, const QString &jobType, const QString &srcfilename);
    int showPrivilegeDialog_SW(int nRet, const QString &srcfilename);
#endif

private:
    QIcon m_dialogInfoIcon;
    QIcon m_dialogWarningIcon;
    QIcon m_dialogErrorIcon;
    QMutex m_mutexJob;
    DTaskDialog *m_taskDialog {nullptr};
    CloseAllDialogIndicator *m_closeIndicatorDialog {nullptr};
    TrashPropertyDialog *m_trashDialog {nullptr};
    ComputerPropertyDialog *m_computerDialog {nullptr};
    QMap<QString, QSharedPointer<FileJob> > m_jobs {};
    QMap<DUrl, PropertyDialog *> m_propertyDialogs {};
    QTimer *m_closeIndicatorTimer {nullptr};
    QTimer *m_updateJobTaskTimer {nullptr};
    dde_file_manager::FilePreviewDialog *m_filePreviewDialog {nullptr};
    Dtk::Widget::DDialog *m_noPemesrsionDialog {nullptr};

    std::unique_ptr<DMultiFilePropertyDialog> m_multiFilesPropertyDialog;

    //! wayland??????????????????????????????
    DUrlList m_urlList;
};

#endif // DIALOGMANAGER_H
