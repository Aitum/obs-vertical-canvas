#pragma once

#include <QDockWidget>
#include <QListWidget>
#include <QWidget>

#include "source-tree.hpp"

class CanvasDock;
class SourceTree;

class CanvasSourcesDock : public QFrame {
	Q_OBJECT
	friend class CanvasDock;

private:
	SourceTree *sourceList;
	CanvasDock *canvasDock;

	obs_sceneitem_t *GetCurrentSceneItem();
	int GetTopSelectedSourceItem();
	void ShowSourcesContextMenu(obs_sceneitem_t *item);
	static bool remove_items(obs_scene_t *, obs_sceneitem_t *item, void *param);

public:
	CanvasSourcesDock(CanvasDock *canvas_dock, QWidget *parent = nullptr);
	~CanvasSourcesDock();
};
