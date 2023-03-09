#pragma once

#include <obs.hpp>
#include "qt-display.hpp"
#include "vertical-canvas.hpp"

bool IsAlwaysOnTop(QWidget *window);
void SetAlwaysOnTop(QWidget *window, bool enable);

class OBSProjector : public OBSQTDisplay {
	Q_OBJECT

private:
	CanvasDock *canvas = nullptr;

	static void OBSRender(void *data, uint32_t cx, uint32_t cy);

	void mousePressEvent(QMouseEvent *event) override;
	void closeEvent(QCloseEvent *event) override;

	bool isAlwaysOnTop;
	bool isAlwaysOnTopOverridden = false;
	int savedMonitor = -1;

	bool ready = false;

	void UpdateProjectorTitle(QString name = "");

	QRect prevGeometry;
	void SetMonitor(int monitor);

	QScreen *screen = nullptr;

private slots:
	void EscapeTriggered();
	void OpenFullScreenProjector();
	void ResizeToContent();
	void OpenWindowedProjector();
	void AlwaysOnTopToggled(bool alwaysOnTop);
	void ScreenRemoved(QScreen *screen_);

public:
	OBSProjector(CanvasDock *canvas_, int monitor);
	~OBSProjector();

	int GetMonitor();
	void RenameProjector(QString oldName, QString newName);
	void SetHideCursor();

	bool IsAlwaysOnTop() const;
	bool IsAlwaysOnTopOverridden() const;
	void SetIsAlwaysOnTop(bool isAlwaysOnTop, bool isOverridden);
};
