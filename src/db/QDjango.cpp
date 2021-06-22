/*
 * Copyright (C) 2010-2015 Jeremy Lainé
 * Contact: https://github.com/jlaine/qdjango
 *
 * This file is part of the QDjango Library.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QThread>
#include <QStack>

#include "QDjango.h"

static const char *connectionPrefix = "_qdjango_";

static QMap<QByteArray, QDjangoMetaModel> globalMetaModels = QMap<QByteArray, QDjangoMetaModel>();
static QDjangoDatabase *globalDatabase = nullptr;
static QDjangoDatabase::DatabaseType globalDatabaseType = QDjangoDatabase::UnknownDB;
static bool globalDebugEnabled = false;

/// \cond

QDjangoDatabase::QDjangoDatabase(QObject *parent)
    : QObject(parent), connectionId(0)
{
}

void QDjangoDatabase::threadFinished()
{
    QThread *thread = qobject_cast<QThread*>(sender());
    if (!thread)
        return;

    // cleanup database connection for the thread
    QMutexLocker locker(&mutex);
    disconnect(thread, SIGNAL(finished()), this, SLOT(threadFinished()));
    const QString connectionName = copies.value(thread).connectionName();
    copies.remove(thread);
    if (connectionName.startsWith(QLatin1String(connectionPrefix)))
        QSqlDatabase::removeDatabase(connectionName);
}

static void closeDatabase()
{
    delete globalDatabase;
    globalDatabase = nullptr;
}

static QDjangoDatabase::DatabaseType getDatabaseType(QSqlDatabase &db)
{
    const QString driverName = db.driverName();
    if (driverName == QLatin1String("QMYSQL") ||
        driverName == QLatin1String("QMYSQL3"))
        return QDjangoDatabase::MySqlServer;
    else if (driverName == QLatin1String("QSQLITE") ||
             driverName == QLatin1String("QSQLITE2"))
        return QDjangoDatabase::SQLite;
    else if (driverName == QLatin1String("QPSQL"))
        return QDjangoDatabase::PostgreSQL;
    else if (driverName.startsWith(QLatin1String("QODBC"))) {
        QSqlQuery query(db);
        if (query.exec(QStringLiteral("SELECT sqlite_version()")))
            return QDjangoDatabase::SQLite;

        if (query.exec(QStringLiteral("SELECT @@version")) && query.next() &&
            query.value(0).toString().contains(QLatin1String("Microsoft SQL")))
                return QDjangoDatabase::MSSqlServer;

        if (query.exec(QStringLiteral("SELECT version()")) && query.next()) {
            if (query.value(0).toString().contains(QLatin1String("PostgreSQL")))
                return QDjangoDatabase::PostgreSQL;
            else
                return QDjangoDatabase::MySqlServer;
        }
    }
    return QDjangoDatabase::UnknownDB;
}

static void initDatabase(QSqlDatabase db)
{
    QDjangoDatabase::DatabaseType databaseType = QDjangoDatabase::databaseType(db);
    if (databaseType == QDjangoDatabase::SQLite) {
        // enable foreign key constraint handling
        QDjangoQuery query(db);
        query.prepare(QStringLiteral("PRAGMA foreign_keys=on"));
        query.exec();
    }
}

QDjangoQuery::QDjangoQuery(QSqlDatabase db)
    : QSqlQuery(db)
{
    if (QDjangoDatabase::databaseType(db) == QDjangoDatabase::MSSqlServer) {
        // default to fast-forward cursor
        setForwardOnly(true);
    }
}

void QDjangoQuery::addBindValue(const QVariant &val, QSql::ParamType paramType)
{
    // this hack is required so that we do not store a mix of local
    // and UTC times
    if (val.type() == QVariant::DateTime)
        QSqlQuery::addBindValue(val.toDateTime().toLocalTime(), paramType);
    else
        QSqlQuery::addBindValue(val, paramType);
}

bool QDjangoQuery::exec()
{
    if (globalDebugEnabled) {
        qDebug() << "SQL query" << lastQuery();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		const auto bv = boundValues();
		for (int i = 0; i < bv.size(); ++i)
		{
			qDebug() << "SQL   " << i << "="
					 << bv[i].toString().toLatin1().data();
		}
#else
        QMapIterator<QString, QVariant> i(boundValues());
        while (i.hasNext()) {
            i.next();
            qDebug() << "SQL   " << i.key().toLatin1().data() << "="
                     << i.value().toString().toLatin1().data();
        }
#endif
    }
    if (!QSqlQuery::exec()) {
        if (globalDebugEnabled)
            qWarning() << "SQL error" << lastError();
        return false;
    }
    return true;
}

bool QDjangoQuery::exec(const QString &query)
{
    if (globalDebugEnabled)
        qDebug() << "SQL query" << query;
    if (!QSqlQuery::exec(query)) {
        if (globalDebugEnabled)
            qWarning() << "SQL error" << lastError();
        return false;
    }
    return true;
}

/// \endcond

/*!
    Returns the database used by QDjango.

    If you call this method from any thread but the application's main thread,
    a new connection to the database will be created. The connection will
    automatically be torn down once the thread finishes.

    \sa setDatabase()
*/
QSqlDatabase QDjango::database()
{
    if (!globalDatabase)
        return QSqlDatabase();

    // if we are in the main thread, return reference connection
    QThread *thread = QThread::currentThread();
    if (thread == globalDatabase->thread())
        return globalDatabase->reference;

    // if we have a connection for this thread, return it
    QMutexLocker locker(&globalDatabase->mutex);
    if (globalDatabase->copies.contains(thread))
        return globalDatabase->copies[thread];

    // create a new connection for this thread
    QObject::connect(thread, SIGNAL(finished()), globalDatabase, SLOT(threadFinished()));
    QSqlDatabase db = QSqlDatabase::cloneDatabase(globalDatabase->reference,
        QLatin1String(connectionPrefix) + QString::number(globalDatabase->connectionId++));
    db.open();
    initDatabase(db);
    globalDatabase->copies.insert(thread, db);
    return db;
}

/*!
    Sets the database used by QDjango.

    You must call this method from your application's main thread.

    \sa database()
*/
void QDjango::setDatabase(QSqlDatabase database, bool skipInit, int dbType)
{
    if (skipInit && globalDatabase && globalDatabase->reference.driverName() == database.driverName())
    {
        globalDatabase->reference = database;
        return;
    }

    if (dbType > 0)
    {
        globalDatabaseType = QDjangoDatabase::DatabaseType(dbType);
    }
    else
    {
        globalDatabaseType = getDatabaseType(database);
    }

    if (database.isValid() && globalDatabaseType == QDjangoDatabase::UnknownDB) {
        qWarning() << "Unsupported database driver" << database.driverName();
    }

    if (!globalDatabase) {
        globalDatabase = new QDjangoDatabase();
        qAddPostRoutine(closeDatabase);
    }
    if (!skipInit)
    {
        initDatabase(database);
    }
    globalDatabase->reference = database;
}

/*!
    Returns whether debugging information should be printed.

    \sa setDebugEnabled()
*/
bool QDjango::isDebugEnabled()
{
    return globalDebugEnabled;
}

/*!
    Sets whether debugging information should be printed.

    \sa isDebugEnabled()
*/
void QDjango::setDebugEnabled(bool enabled)
{
    globalDebugEnabled = enabled;
}

static void qdjango_topsort(const QByteArray &modelName, QHash<QByteArray, bool> &visited,
                            QStack<QDjangoMetaModel> &stack)
{
    visited[modelName] = true;
    QDjangoMetaModel model = globalMetaModels[modelName];
    for (const QByteArray &foreignModel : model.foreignFields()) {
        if (!visited[foreignModel])
            qdjango_topsort(foreignModel, visited, stack);
    }

    stack.push(model);
}

static QStack<QDjangoMetaModel> qdjango_sorted_metamodels()
{
    QStack<QDjangoMetaModel> stack;
    stack.reserve(globalMetaModels.size());
    QHash<QByteArray, bool> visited;
    visited.reserve(globalMetaModels.size());
    for (auto it = globalMetaModels.constBegin(), end = globalMetaModels.constEnd(); it != end; ++it )
        visited[it.key()] = false;

    for (auto it = globalMetaModels.constBegin(), end = globalMetaModels.constEnd(); it != end; ++it ) {
        if (!visited[it.key()])
            qdjango_topsort(it.key(), visited, stack);
    }

    return stack;
}

/*!
    Creates the database tables for all registered models.

    \return true if all the tables were created, false otherwise.
*/
bool QDjango::createTables()
{
    bool result = true;
    QStack<QDjangoMetaModel> stack = qdjango_sorted_metamodels();
    for (const QDjangoMetaModel &model : stack) {
        if (!model.createTable())
            result = false;
    }

    return result;
}

/*!
    Drops the database tables for all registered models.

    \return true if all the tables were dropped, false otherwise.
*/
bool QDjango::dropTables()
{
    bool result = true;
    QStack<QDjangoMetaModel> stack = qdjango_sorted_metamodels();
    for (int i = stack.size() - 1; i >= 0; --i) {
        QDjangoMetaModel model = stack.at(i);
        if (!model.dropTable())
            result = false;
    }

    return result;
}

/*!
    Returns the QDjangoMetaModel with the given \a name.
 */
QDjangoMetaModel QDjango::metaModel(const char *name)
{
    if (globalMetaModels.contains(name))
        return globalMetaModels.value(name);

    // otherwise, try to find a model anyway
    for (auto it = globalMetaModels.constBegin(), end = globalMetaModels.constEnd(); it != end; ++it ) {
        if (qstricmp(name, it.key().data()) == 0)
            return globalMetaModels.value(it.key());
    }

    return QDjangoMetaModel();
}


QDjangoMetaModel QDjango::registerModel(const QObject *object)
{
    const auto name = object->objectName().toUtf8();
    if (name.isEmpty() == false && !globalMetaModels.contains(name))
        globalMetaModels.insert(name, QDjangoMetaModel(object));
    return globalMetaModels[name];
}


void QDjango::unregisterModel(const QObject* object)
{
    const auto name = object->objectName().toUtf8();
    if (name.isEmpty() == false && globalMetaModels.contains(name))
        globalMetaModels.remove(name);
}


QDjangoMetaModel QDjango::registerModel(const QMetaObject *meta)
{
    const QByteArray name = meta->className();
    if (!globalMetaModels.contains(name))
        globalMetaModels.insert(name, QDjangoMetaModel(meta));
    return globalMetaModels[name];
}

QDjangoDatabase::DatabaseType QDjangoDatabase::databaseType(const QSqlDatabase &db)
{
    Q_UNUSED(db)
    return globalDatabaseType;
}
