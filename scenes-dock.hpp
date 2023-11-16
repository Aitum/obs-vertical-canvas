#pragma once

#include <QDockWidget>
#include <QListWidget>
#include <QWidget>

class CanvasDock;

class CanvasScenesDock : public QFrame {
	Q_OBJECT
	friend class CanvasDock;
private:
	QListWidget* sceneList;
	CanvasDock *canvasDock;

	void ChangeSceneIndex(bool relative, int offset, int invalidIdx);
	void ShowScenesContextMenu(QListWidgetItem *item);
	void SetGridMode(bool checked);
	bool IsGridMode();
public:
	
	CanvasScenesDock(CanvasDock* canvas_dock, QWidget *parent = nullptr);
	~CanvasScenesDock();
};
