#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QMainWindow>
#include <qtextedit.h>

class CanvasConfigDialog : public QDialog {
	Q_OBJECT

	QComboBox *resolution;
	QComboBox *replayBuffer;
	QComboBox *server;
	QLineEdit *key;
	QCheckBox * hideScenes;

	friend class CanvasDock;

public:
	CanvasConfigDialog(QMainWindow *parent = nullptr);
	~CanvasConfigDialog();
public slots:
};
