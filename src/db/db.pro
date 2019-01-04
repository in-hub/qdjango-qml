include(../../qdjango.pri)

QT -= gui
QT += sql

DEFINES += QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII

DEFINES += QDJANGO_DB_BUILD

TARGET = qdjango-db
win32 {
    DESTDIR = $$OUT_PWD
}

HEADERS += \
    QDjango.h \
    QDjango_p.h \
    QDjangoMetaModel.h \
    QDjangoModel.h \
    QDjangoQuerySet.h \
    QDjangoQuerySet_p.h \
    QDjangoWhere.h \
    QDjangoWhere_p.h
SOURCES += \
    QDjango.cpp \
    QDjangoMetaModel.cpp \
    QDjangoModel.cpp \
    QDjangoQuerySet.cpp \
    QDjangoWhere.cpp

# Installation
include(../src.pri)
headers.path = $$[QT_INSTALL_HEADERS]/qdjango/db
QMAKE_PKGCONFIG_INCDIR = $$headers.path
