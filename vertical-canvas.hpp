#pragma once

#include <mutex>
#include <memory>
#include <obs-frontend-api.h>
#include <QDockWidget>
#include <qpushbutton.h>
#include <QVBoxLayout>

#include <graphics/vec2.h>
#include <graphics/matrix4.h>

#include "config-dialog.hpp"
#include "obs.hpp"
#include "qt-display.hpp"

#define ITEM_LEFT (1 << 0)
#define ITEM_RIGHT (1 << 1)
#define ITEM_TOP (1 << 2)
#define ITEM_BOTTOM (1 << 3)
#define ITEM_ROT (1 << 4)

enum class ItemHandle : uint32_t {
	None = 0,
	TopLeft = ITEM_TOP | ITEM_LEFT,
	TopCenter = ITEM_TOP,
	TopRight = ITEM_TOP | ITEM_RIGHT,
	CenterLeft = ITEM_LEFT,
	CenterRight = ITEM_RIGHT,
	BottomLeft = ITEM_BOTTOM | ITEM_LEFT,
	BottomCenter = ITEM_BOTTOM,
	BottomRight = ITEM_BOTTOM | ITEM_RIGHT,
	Rot = ITEM_ROT
};

typedef std::function<bool(QObject *, QEvent *)> EventFilterFunc;

class OBSEventFilter : public QObject {
	Q_OBJECT
public:
	OBSEventFilter(EventFilterFunc filter_) : filter(filter_) {}

protected:
	bool eventFilter(QObject *obj, QEvent *event)
	{
		return filter(obj, event);
	}

public:
	EventFilterFunc filter;
};

class CanvasDock : public QDockWidget {
	Q_OBJECT

private:
	QAction *action;
	QVBoxLayout *mainLayout;
	OBSQTDisplay *preview;
	OBSWeakSource source;
	std::unique_ptr<OBSEventFilter> eventFilter;

	std::vector<obs_sceneitem_t *> hoveredPreviewItems;
	std::vector<obs_sceneitem_t *> selectedItems;
	std::mutex selectMutex;
	bool drawSpacingHelpers = true;

	vec2 startPos{};
	vec2 mousePos{};
	vec2 lastMoveOffset{};
	vec2 scrollingFrom{};
	vec2 scrollingOffset{};
	bool mouseDown = false;
	bool mouseMoved = false;
	bool mouseOverItems = false;
	bool cropping = false;
	bool locked = false;
	bool scrollMode = false;
	bool fixedScaling = false;
	bool selectionBox = false;
	int32_t scalingLevel = 0;
	float scalingAmount = 1.0f;
	float groupRot = 0.0f;

	bool changed{};
	obs_sceneitem_crop startCrop{};
	vec2 startItemPos{};
	vec2 cropSize{};
	OBSSceneItem stretchGroup;
	OBSSceneItem stretchItem;
	ItemHandle stretchHandle = ItemHandle::None;
	float rotateAngle{};
	vec2 rotatePoint{};
	vec2 offsetPoint{};
	vec2 stretchItemSize{};
	matrix4 screenToItem{};
	matrix4 itemToScreen{};
	matrix4 invGroupTransform{};
	obs_scene_t *scene = nullptr;
	obs_view_t *view = nullptr;
	video_t *video = nullptr;
	gs_texrender_t *texrender = nullptr;
	gs_stagesurf_t *stagesurface = nullptr;
	QPushButton *virtualCamButton;
	QPushButton *recordButton;
	QPushButton *replayButton;
	QPushButton *streamButton;
	QComboBox *scenesCombo;
	CanvasConfigDialog *configDialog = nullptr;

	obs_output_t *virtualCamOutput = nullptr;
	obs_output_t *recordOutput = nullptr;
	obs_output_t *replayOutput = nullptr;
	obs_output_t *streamOutput = nullptr;

	obs_service_t *stream_service = nullptr;
	QString stream_key;
	QString stream_server;
	uint32_t canvas_width;
	uint32_t canvas_height;

	QColor GetSelectionColor() const;
	QColor GetCropColor() const;
	QColor GetHoverColor() const;

	gs_texture_t *overflow = nullptr;
	gs_vertbuffer_t *rectFill = nullptr;
	gs_vertbuffer_t *circleFill = nullptr;

	gs_vertbuffer_t *box = nullptr;

	OBSSourceAutoRelease spacerLabel[4];
	int spacerPx[4] = {0};

	bool startReplayBuffer = false;

	inline bool IsFixedScaling() const { return fixedScaling; }

	OBSEventFilter *BuildEventFilter();

	bool HandleMousePressEvent(QMouseEvent *event);
	bool HandleMouseReleaseEvent(QMouseEvent *event);
	bool HandleMouseMoveEvent(QMouseEvent *event);
	bool HandleMouseLeaveEvent(QMouseEvent *event);
	bool HandleMouseWheelEvent(QWheelEvent *event);
	bool HandleKeyPressEvent(QKeyEvent *event);
	bool HandleKeyReleaseEvent(QKeyEvent *event);

	void UpdateCursor(uint32_t &flags);
	void ProcessClick(const vec2 &pos);
	void DoCtrlSelect(const vec2 &pos);
	void DoSelect(const vec2 &pos);
	OBSSceneItem GetItemAtPos(const vec2 &pos, bool selectBelow);

	QMenu *CreateAddSourcePopupMenu();
	void LoadSourceTypeMenu(QMenu *menu, const char *type);

	obs_scene_item *GetSelectedItem();

	bool SelectedAtPos(obs_scene_t *scene, const vec2 &pos);
	void DrawOverflow(float scale);
	void DrawBackdrop(float cx, float cy);
	void DrawSpacingHelpers(obs_scene_t *scene, float x, float y, float cx,
				float cy, float scale, float sourceX,
				float sourceY);
	void DrawSpacingLine(vec3 &start, vec3 &end, vec3 &viewport,
			     float pixelRatio);
	void SetLabelText(int sourceIndex, int px);
	void RenderSpacingHelper(int sourceIndex, vec3 &start, vec3 &end,
				 vec3 &viewport, float pixelRatio);
	bool GetSourceRelativeXY(int mouseX, int mouseY, int &relX, int &relY);

	void RotateItem(const vec2 &pos);
	void CropItem(const vec2 &pos);
	void StretchItem(const vec2 &pos);
	void SnapStretchingToScreen(vec3 &tl, vec3 &br);
	vec3 GetSnapOffset(const vec3 &tl, const vec3 &br);
	void MoveItems(const vec2 &pos);
	void SnapItemMovement(vec2 &offset);
	void BoxItems(const vec2 &startPos, const vec2 &pos);
	void GetStretchHandleData(const vec2 &pos, bool ignoreGroup);
	void ClampAspect(vec3 &tl, vec3 &br, vec2 &size, const vec2 &baseSize);
	vec3 CalculateStretchPos(const vec3 &tl, const vec3 &br);

	bool DrawSelectionBox(float x1, float y1, float x2, float y2,
			      gs_vertbuffer_t *rectFill);

	vec2 GetMouseEventPos(QMouseEvent *event);
	float GetDevicePixelRatio();

	void setSource(obs_weak_source_t *source);
	void AddSourceToScene(obs_source_t *source);

	bool StartVideo();

	void StartVirtualCam();
	void StopVirtualCam();
	void StartRecord();
	void StopRecord();
	void StartReplayBuffer();
	void StopReplayBuffer();
	void StartStream();
	void StopStream();
	void DestroyVideo();

	void SwitchScene(const QString &scene_name);

	static bool DrawSelectedOverflow(obs_scene_t *scene,
					 obs_sceneitem_t *item, void *param);
	static bool FindSelected(obs_scene_t *scene, obs_sceneitem_t *item,
				 void *param);
	static void DrawPreview(void *data, uint32_t cx, uint32_t cy);
	static bool DrawSelectedItem(obs_scene_t *scene, obs_sceneitem_t *item,
				     void *param);
	static bool add_sources_of_type_to_menu(void *param,
						obs_source_t *source);

	static void virtual_cam_output_start(void *p, calldata_t *calldata);
	static void virtual_cam_output_stop(void *p, calldata_t *calldata);
	static void record_output_start(void *p, calldata_t *calldata);
	static void record_output_stop(void *p, calldata_t *calldata);
	static void record_output_stopping(void *p, calldata_t *calldata);
	static void replay_output_start(void *p, calldata_t *calldata);
	static void replay_output_stop(void *p, calldata_t *calldata);
	static void stream_output_start(void *p, calldata_t *calldata);
	static void stream_output_stop(void *p, calldata_t *calldata);
	static void source_rename(void *p, calldata_t *calldata);
	static void source_remove(void *p, calldata_t *calldata);
	static void source_save(void *p, calldata_t *calldata);

private slots:
	void AddSourceFromAction();
	void VirtualCamButtonClicked();
	void ReplayButtonClicked();
	void RecordButtonClicked();
	void StreamButtonClicked();
	void ConfigButtonClicked();
	void OnVirtualCamStart();
	void OnVirtualCamStop();
	void OnRecordStart();
	void OnRecordStop();
	void OnStreamStart();
	void OnStreamStop();
	void OnReplayBufferStart();
	void OnReplayBufferStop();

public:
	CanvasDock(obs_data_t *settings, QWidget *parent = nullptr);
	~CanvasDock();

	void ClearScenes();
	void LoadScenes();
	void FinishLoading();
	void setAction(QAction *action);

	obs_data_t *SaveSettings();
};
