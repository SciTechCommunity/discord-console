QT += core concurrent network websockets script
QT -= gui
CONFIG += c++14
TARGET = discord-console
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app
SOURCES += main.cpp
DISTFILES += \
    secrets \
    depends
