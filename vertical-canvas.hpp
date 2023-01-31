#pragma once

#include <mutex>
#include <memory>
#include <obs-frontend-api.h>
#include <QDockWidget>
#include <qlistwidget.h>
#include <qpushbutton.h>
#include <QVBoxLayout>
#include <QLabel>
#include <QMovie>
#include <QStackedWidget>
#include <QTimer>

#include <graphics/vec2.h>
#include <graphics/matrix4.h>

#include "config-dialog.hpp"
#include "scenes-dock.hpp"
#include "obs.hpp"
#include "qt-display.hpp"
#include "sources-dock.hpp"

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

class CanvasScenesDock;
class CanvasSourcesDock;

class CanvasDock : public QDockWidget {
	Q_OBJECT
	friend class CanvasScenesDock;
	friend class CanvasSourcesDock;
	friend class SourceTree;
	friend class SourceTreeItem;
	friend class SourceTreeModel;
	friend class OBSBasicSettings;

private:
	QPointer<QAction> action;
	QVBoxLayout *mainLayout;
	OBSQTDisplay *preview;
	OBSWeakSource source;
	std::vector<OBSSource> transitions;
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
	QIcon recordActiveIcon = QIcon(":/aitum/media/recording.svg");
	QIcon recordInactiveIcon = QIcon(":/aitum/media/record.svg");
	QPushButton *replayButton;
	QLabel *statusLabel;
	QTimer replayStatusResetTimer;
	QPushButton *streamButton;
	QIcon streamActiveIcon = QIcon(":/aitum/media/streaming.svg");
	QIcon streamInactiveIcon = QIcon(":/aitum/media/stream.svg");

	QIcon replayActiveIcon = QIcon(":/aitum/media/backtrack_on.svg");
	QIcon replayInactiveIcon = QIcon(":/aitum/media/backtrack_off.svg");
	QComboBox *scenesCombo = nullptr;
	QCheckBox *linkedButton = nullptr;
	CanvasScenesDock *scenesDock = nullptr;
	QAction *scenesDockAction = nullptr;
	CanvasSourcesDock *sourcesDock = nullptr;
	QAction *sourcesDockAction = nullptr;
	OBSBasicSettings *configDialog = nullptr;

	obs_hotkey_pair_id stream_hotkey;
	obs_hotkey_pair_id record_hotkey;
	obs_hotkey_pair_id virtual_cam_hotkey;

	obs_output_t *virtualCamOutput = nullptr;
	obs_output_t *recordOutput = nullptr;
	obs_output_t *replayOutput = nullptr;
	obs_output_t *streamOutput = nullptr;

	obs_service_t *stream_service = nullptr;

	uint32_t canvas_width;
	uint32_t canvas_height;
	bool hideScenes;
	uint32_t videoBitrate;
	uint32_t audioBitrate;
	bool startReplay;
	uint32_t replayDuration;
	std::string replayPath;

	std::string stream_key;
	std::string stream_server;

	std::string recordPath;

	QString currentSceneName;
	bool first_time = false;

	QColor GetSelectionColor() const;
	QColor GetCropColor() const;
	QColor GetHoverColor() const;

	gs_texture_t *overflow = nullptr;
	gs_vertbuffer_t *rectFill = nullptr;
	gs_vertbuffer_t *circleFill = nullptr;

	gs_vertbuffer_t *box = nullptr;

	OBSSourceAutoRelease spacerLabel[4];
	int spacerPx[4] = {0};

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
	void AddSceneItemMenuItems(QMenu *popup, OBSSceneItem sceneItem);
	void LoadSourceTypeMenu(QMenu *menu, const char *type);
	QIcon GetIconFromType(enum obs_icon_type icon_type) const;
	QIcon GetGroupIcon() const;
	QIcon GetSceneIcon() const;

	obs_scene_item *GetSelectedItem(obs_scene_t* scene = nullptr);

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

	enum class CenterType {
		Scene,
		Vertical,
		Horizontal,
	};
	void CenterSelectedItems(CenterType centerType);

	void AddSourceToScene(obs_source_t *source);

	bool StartVideo();

	void DestroyVideo();

	void CreateScenesRow();
	void AddScene(QString duplicate = "", bool ask_name = true);
	void RemoveScene(const QString &sceneName);
	void SetLinkedScene(obs_source_t *scene, const QString &linkedScene);
	bool HasScene(QString scene) const;
	void CheckReplayBuffer(bool start = false);
	void SendVendorEvent(const char * e);
	QListWidget *GetGlobalScenesList();

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
	static void replay_saved(void *p, calldata_t *calldata);
	static void stream_output_start(void *p, calldata_t *calldata);
	static void stream_output_stop(void *p, calldata_t *calldata);
	static void source_rename(void *p, calldata_t *calldata);
	static void source_remove(void *p, calldata_t *calldata);
	static void source_save(void *p, calldata_t *calldata);
	static bool start_virtual_cam_hotkey(void *data, obs_hotkey_pair_id id,
					     obs_hotkey_t *hotkey,
					     bool pressed);
	static bool stop_virtual_cam_hotkey(void *data, obs_hotkey_pair_id id,
					    obs_hotkey_t *hotkey, bool pressed);
	static bool start_recording_hotkey(void *data, obs_hotkey_pair_id id,
					   obs_hotkey_t *hotkey, bool pressed);
	static bool stop_recording_hotkey(void *data, obs_hotkey_pair_id id,
					  obs_hotkey_t *hotkey, bool pressed);
	static bool start_streaming_hotkey(void *data, obs_hotkey_pair_id id,
					   obs_hotkey_t *hotkey, bool pressed);
	static bool stop_streaming_hotkey(void *data, obs_hotkey_pair_id id,
					  obs_hotkey_t *hotkey, bool pressed);

	static void SceneItemAdded(void *data, calldata_t *params);
	static void SceneReordered(void *data, calldata_t *params);
	static void SceneRefreshed(void *data, calldata_t *params);

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
	void OnRecordStop(int code, QString last_error);
	void OnReplaySaved();
	void OnStreamStart();
	void OnStreamStop(int code, QString last_error);
	void OnReplayBufferStart();
	void OnReplayBufferStop();
	void SwitchScene(const QString &scene_name);
	void StartVirtualCam();
	void StopVirtualCam();
	void StartRecord();
	void StopRecord();
	void StartReplayBuffer();
	void StopReplayBuffer();
	void StartStream();
	void StopStream();
	void AddSceneItem(OBSSceneItem item);
	void RefreshSources(OBSScene scene);
	void ReorderSources(OBSScene scene);

public:
	CanvasDock(obs_data_t *settings, QWidget *parent = nullptr);
	~CanvasDock();

	void ClearScenes();
	void LoadScenes();
	void FinishLoading();
	void MainSceneChanged();
	void setAction(QAction *action);
	CanvasScenesDock *GetScenesDock();
	inline uint32_t GetCanvasWidth() const { return canvas_width; }
	inline uint32_t GetCanvasHeight() const { return canvas_height; }

	obs_data_t *SaveSettings();

	obs_scene_t* GetCurrentScene();
	std::vector<QString> GetScenes();
	bool StreamingActive();
	bool RecordingActive();
	bool BacktrackActive();
	bool VirtualCameraActive();

	void MainStreamStart();
	void MainStreamStop();
	void MainRecordStart();
	void MainRecordStop();
	void MainVirtualCamStart();
	void MainVirtualCamStop();
};

class LockedCheckBox : public QCheckBox {
	Q_OBJECT

public:
	LockedCheckBox();
	explicit LockedCheckBox(QWidget *parent);
};

class VisibilityCheckBox : public QCheckBox {
	Q_OBJECT

public:
	VisibilityCheckBox();
	explicit VisibilityCheckBox(QWidget *parent);
};
