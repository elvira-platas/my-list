QT += widgets
CONFIG += c++17
TEMPLATE = app
TARGET = writer-todo

SOURCES += main.cpp
RESOURCES += resources.qrc

unix:!macx {
    target.path = /usr/local/bin
    INSTALLS += target
}
