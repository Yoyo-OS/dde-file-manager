/*
 * This file was generated by qdbusxml2cpp version 0.8
 * Command line was: qdbusxml2cpp -i controllers/fileoperation.h -i dbusservice/dbustype/dbusinforet.h -c FileOperationAdaptor -l FileOperation -a dbusadaptor/fileoperation_adaptor fileoperation.xml
 *
 * qdbusxml2cpp is Copyright (C) 2016 The Qt Company Ltd.
 *
 * This is an auto-generated file.
 * This file may have been hand-edited. Look for HAND-EDIT comments
 * before re-generating it.
 */

#ifndef FILEOPERATION_ADAPTOR_H
#define FILEOPERATION_ADAPTOR_H

#include <QtCore/QObject>
#include <QtDBus/QtDBus>
#include "controllers/fileoperation.h"
#include "dbusservice/dbustype/dbusinforet.h"
QT_BEGIN_NAMESPACE
class QByteArray;
template<class T> class QList;
template<class Key, class Value> class QMap;
class QString;
class QStringList;
class QVariant;
QT_END_NAMESPACE

/*
 * Adaptor class for interface com.deepin.filemanager.daemon.Operations
 */
class FileOperationAdaptor: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.filemanager.daemon.Operations")
    Q_CLASSINFO("D-Bus Introspection", ""
"  <interface name=\"com.deepin.filemanager.daemon.Operations\">\n"
"    <method name=\"NewCreateFolderJob\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"fabspath\"/>\n"
"      <arg direction=\"out\" type=\"(so)\"/>\n"
"      <annotation value=\"DBusInfoRet\" name=\"org.qtproject.QtDBus.QtTypeName.Out0\"/>\n"
"    </method>\n"
"    <method name=\"NewCreateTemplateFileJob\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"templateFile\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"targetDir\"/>\n"
"      <arg direction=\"out\" type=\"(so)\"/>\n"
"      <annotation value=\"DBusInfoRet\" name=\"org.qtproject.QtDBus.QtTypeName.Out0\"/>\n"
"    </method>\n"
"    <method name=\"NewCopyJob\">\n"
"      <arg direction=\"in\" type=\"as\" name=\"filelist\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"targetDir\"/>\n"
"      <arg direction=\"out\" type=\"(so)\"/>\n"
"      <annotation value=\"DBusInfoRet\" name=\"org.qtproject.QtDBus.QtTypeName.Out0\"/>\n"
"    </method>\n"
"    <method name=\"NewMoveJob\">\n"
"      <arg direction=\"in\" type=\"as\" name=\"filelist\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"targetDir\"/>\n"
"      <arg direction=\"out\" type=\"(so)\"/>\n"
"      <annotation value=\"DBusInfoRet\" name=\"org.qtproject.QtDBus.QtTypeName.Out0\"/>\n"
"    </method>\n"
"    <method name=\"NewRenameJob\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"oldFile\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"newFile\"/>\n"
"      <arg direction=\"out\" type=\"(so)\"/>\n"
"      <annotation value=\"DBusInfoRet\" name=\"org.qtproject.QtDBus.QtTypeName.Out0\"/>\n"
"    </method>\n"
"    <method name=\"NewDeleteJob\">\n"
"      <arg direction=\"in\" type=\"as\" name=\"filelist\"/>\n"
"      <arg direction=\"out\" type=\"(so)\"/>\n"
"      <annotation value=\"DBusInfoRet\" name=\"org.qtproject.QtDBus.QtTypeName.Out0\"/>\n"
"    </method>\n"
"    <method name=\"test\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"oldFile\"/>\n"
"      <arg direction=\"in\" type=\"s\" name=\"newFile\"/>\n"
"      <arg direction=\"out\" type=\"s\" name=\"result1\"/>\n"
"      <arg direction=\"out\" type=\"o\" name=\"result2\"/>\n"
"      <arg direction=\"out\" type=\"b\" name=\"result3\"/>\n"
"    </method>\n"
"  </interface>\n"
        "")
public:
    FileOperationAdaptor(FileOperation *parent);
    virtual ~FileOperationAdaptor();

    inline FileOperation *parent() const
    { return static_cast<FileOperation *>(QObject::parent()); }

public: // PROPERTIES
public Q_SLOTS: // METHODS
    DBusInfoRet NewCopyJob(const QStringList &filelist, const QString &targetDir);
    DBusInfoRet NewCreateFolderJob(const QString &fabspath);
    DBusInfoRet NewCreateTemplateFileJob(const QString &templateFile, const QString &targetDir);
    DBusInfoRet NewDeleteJob(const QStringList &filelist);
    DBusInfoRet NewMoveJob(const QStringList &filelist, const QString &targetDir);
    DBusInfoRet NewRenameJob(const QString &oldFile, const QString &newFile);
    QString test(const QString &oldFile, const QString &newFile, QDBusObjectPath &result2, bool &result3);
Q_SIGNALS: // SIGNALS
};

#endif
