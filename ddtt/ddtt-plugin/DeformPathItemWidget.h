#pragma once

#include <QGraphicsProxyWidget>
#include <QSlider>
#include <QLabel>
#include <QPushButton>

class DeformationPath;

class DeformPathItemWidget : public QGraphicsProxyWidget
{
	Q_OBJECT
public:
	DeformPathItemWidget(DeformationPath * usedPath = NULL);

	DeformationPath * path;

    QSlider * slider;
    QLabel * label;
	QPushButton * executeButton;

public slots:
	void init();
    void sliderValueChanged(int val);

signals:
	void widgetCreated();
};
