TEMPLATE = app
CONFIG += windows c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.cpp

QMAKE_LFLAGS += -static -static-libgcc

win32: LIBS += -lsetupapi -lgdi32
#win32: LIBS += -lcomctl32

RC_ICONS = comonitor.ico

RC_FILE = Comonitor.rc

HEADERS += \
    resource.h
