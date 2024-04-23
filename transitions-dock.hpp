#pragma once

#include <QDockWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QWidget>

class CanvasDock;

class CanvasTransitionsDock : public QFrame {
	Q_OBJECT
	friend class CanvasDock;

private:
	CanvasDock *canvasDock;
	QComboBox *transition;
	QSpinBox *duration;
	QPushButton *removeButton;
	QPushButton *propsButton;

public:
	CanvasTransitionsDock(CanvasDock *canvas_dock, QWidget *parent = nullptr);
	~CanvasTransitionsDock();
};
