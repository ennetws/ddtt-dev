#pragma once

#include "SurfaceMeshPlugins.h"
#include "RichParameterSet.h"
#include "interfaces/ModePluginDockWidget.h"

class particles: public SurfaceMeshModePlugin{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "particles.plugin.starlab")
    Q_INTERFACES(ModePlugin)

    QIcon icon(){ return QIcon(":/images/particles.png"); }

public:
    particles() : widget(NULL), dockwidget(NULL) {}

    // Main functionality
    void create();
    void destroy(){}
    void decorate();

	bool keyPressEvent(QKeyEvent*);
	bool mouseMoveEvent(QMouseEvent*);

    QWidget * widget;
	ModePluginDockWidget * dockwidget;

    // Always usable
    bool isApplicable() { return true; }

	bool isBlendingReady;

public slots:
	void processShapes();
    void prepareBlending();
	void reVoxelize();
	void postCorrespond();

signals:
	void shapesProcessed();
};
