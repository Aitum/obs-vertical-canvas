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
#include "projector.hpp"

#define ITEM_LEFT (1 << 0)
#define ITEM_RIGHT (1 << 1)
#define ITEM_TOP (1 << 2)
#define ITEM_BOTTOM (1 << 3)
#define ITEM_ROT (1 << 4)

#define VIRTUAL_CAMERA_VERTICAL 0
#define VIRTUAL_CAMERA_MAIN 1
#define VIRTUAL_CAMERA_BOTH 2

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
	bool eventFilter(QObject *obj, QEvent *event) { return filter(obj, event); }

public:
	EventFilterFunc filter;
};

class CanvasScenesDock;
class CanvasSourcesDock;
class CanvasTransitionsDock;
class OBSProjector;

class StreamServer {
public:
	obs_output_t *output = nullptr;
	obs_service_t *service = nullptr;
	std::string name;
	std::string stream_key;
	std::string stream_server;
	bool enabled = true;
};

class CanvasDock : public QFrame {
	Q_OBJECT
	friend class CanvasScenesDock;
	friend class CanvasSourcesDock;
	friend class CanvasTransitionsDock;
	friend class SourceTree;
	friend class SourceTreeItem;
	friend class SourceTreeModel;
	friend class OBSBasicSettings;
	friend class OBSProjector;

private:
	QPointer<QAction> action;
	QString newer_version_available;
	QVBoxLayout *mainLayout;
	OBSQTDisplay *preview;
	bool preview_disabled = false;
	QFrame *previewDisabledWidget;
	QPushButton *configButton;
	OBSWeakSource source;
	obs_source_t *transitionAudioWrapper;
	std::vector<OBSSource> transitions;
	std::vector<OBSProjector *> projectors;
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

	float previewScale = 1.0f;

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
	obs_view_t *multiCanvasView = nullptr;
	video_t *multiCanvasVideo = nullptr;
	obs_source_t *multiCanvasSource = nullptr;
	gs_texrender_t *texrender = nullptr;
	gs_stagesurf_t *stagesurface = nullptr;
	QPushButton *virtualCamButton;
	QPushButton *recordButton;
	QIcon recordActiveIcon = QIcon(":/aitum/media/recording.svg");
	QIcon recordInactiveIcon = QIcon(":/aitum/media/record.svg");
	QPushButton *replayButton;
	QLabel *statusLabel;
	QTimer replayStatusResetTimer;
	QTimer recordDurationTimer;
	QPushButton *streamButton;
	QPushButton *streamButtonMulti;
	QIcon streamActiveIcon = QIcon(":/aitum/media/streaming.svg");
	QIcon streamInactiveIcon = QIcon(":/aitum/media/stream.svg");

	QIcon replayActiveIcon = QIcon(":/aitum/media/backtrack_on.svg");
	QIcon replayInactiveIcon = QIcon(":/aitum/media/backtrack_off.svg");

	QIcon virtualCamActiveIcon = QIcon(":/aitum/media/virtual_cam_on.svg");
	QIcon virtualCamInactiveIcon = QIcon(":/aitum/media/virtual_cam_off.svg");
	QComboBox *scenesCombo = nullptr;
	QCheckBox *linkedButton = nullptr;
	CanvasScenesDock *scenesDock = nullptr;
	QAction *scenesDockAction = nullptr;
	CanvasSourcesDock *sourcesDock = nullptr;
	QAction *sourcesDockAction = nullptr;
	CanvasTransitionsDock *transitionsDock = nullptr;
	QAction *transitionsDockAction = nullptr;
	OBSBasicSettings *configDialog = nullptr;

	obs_hotkey_pair_id stream_hotkey = OBS_INVALID_HOTKEY_PAIR_ID;
	obs_hotkey_pair_id record_hotkey = OBS_INVALID_HOTKEY_PAIR_ID;
	obs_hotkey_pair_id virtual_cam_hotkey = OBS_INVALID_HOTKEY_PAIR_ID;

	obs_output_t *virtualCamOutput = nullptr;
	obs_output_t *recordOutput = nullptr;
	obs_output_t *replayOutput = nullptr;

	uint32_t canvas_width;
	uint32_t canvas_height;
	bool restart_video = false;
	bool hideScenes;
	uint32_t streamingVideoBitrate;
	uint32_t recordVideoBitrate;
	uint32_t audioBitrate;
	bool streamingMatchMain;
	bool recordingMatchMain;
	bool startReplay;
	bool replayAlwaysOn;
	uint32_t replayDuration;
	std::string replayPath;
	std::string replayFilename;

	std::vector<StreamServer> streamOutputs;

	bool stream_delay_enabled;
	uint32_t stream_delay_duration;
	bool stream_delay_preserve;

	bool stream_advanced_settings;
	int stream_audio_track;
	std::string stream_encoder;
	obs_data_t *stream_encoder_settings;

	std::string recordPath;

	bool record_advanced_settings;
	std::string filename_formatting;
	std::string file_format;
	long long record_audio_tracks;
	std::string record_encoder;
	obs_data_t *record_encoder_settings;
	bool virtual_cam_warned;
	uint32_t virtual_cam_mode = 0;

	QString currentSceneName;
	bool first_time = false;
	bool multi_rtmp = false;

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
	QMenu *CreateVisibilityTransitionMenu(bool visible, obs_sceneitem_t *sceneItem);
	QIcon GetIconFromType(enum obs_icon_type icon_type) const;
	QIcon GetGroupIcon() const;
	QIcon GetSceneIcon() const;

	obs_scene_item *GetSelectedItem(obs_scene_t *scene = nullptr);

	bool SelectedAtPos(obs_scene_t *scene, const vec2 &pos);
	void DrawOverflow(float scale);
	void DrawBackdrop(float cx, float cy);
	void DrawSpacingHelpers(obs_scene_t *scene, float x, float y, float cx, float cy, float scale, float sourceX,
				float sourceY);
	void DrawSpacingLine(vec3 &start, vec3 &end, vec3 &viewport, float pixelRatio);
	void SetLabelText(int sourceIndex, int px);
	void RenderSpacingHelper(int sourceIndex, vec3 &start, vec3 &end, vec3 &viewport, float pixelRatio);
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

	bool DrawSelectionBox(float x1, float y1, float x2, float y2, gs_vertbuffer_t *rectFill);

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
	void HandleRecordError(int code, QString last_error);

	void CreateScenesRow();
	void AddScene(QString duplicate = "", bool ask_name = true);
	void RemoveScene(const QString &sceneName);
	void SetLinkedScene(obs_source_t *scene, const QString &linkedScene);
	bool HasScene(QString scene) const;
	void CheckReplayBuffer(bool start = false);
	void SendVendorEvent(const char *e);
	QListWidget *GetGlobalScenesList();
	void ResizeScenes();
	void ResizeScene(QString scene_name);
	void DeleteProjector(OBSProjector *projector);
	OBSProjector *OpenProjector(int monitor);
	void AddProjectorMenuMonitors(QMenu *parent, QObject *target, const char *slot);

	void TryRemux(QString path);
	void CreateStreamOutput(std::vector<StreamServer>::iterator it);

	void StreamButtonMultiMenu(QMenu *menu);

	enum class MoveDir { Up, Down, Left, Right };
	void Nudge(int dist, MoveDir dir);

	static bool DrawSelectedOverflow(obs_scene_t *scene, obs_sceneitem_t *item, void *param);
	static bool FindSelected(obs_scene_t *scene, obs_sceneitem_t *item, void *param);
	static void DrawPreview(void *data, uint32_t cx, uint32_t cy);
	static bool DrawSelectedItem(obs_scene_t *scene, obs_sceneitem_t *item, void *param);
	static bool add_sources_of_type_to_menu(void *param, obs_source_t *source);

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
	static bool start_virtual_cam_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed);
	static bool stop_virtual_cam_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed);
	static bool start_recording_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed);
	static bool stop_recording_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed);
	static bool start_streaming_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed);
	static bool stop_streaming_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed);

	static void SceneItemAdded(void *data, calldata_t *params);
	static void SceneReordered(void *data, calldata_t *params);
	static void SceneRefreshed(void *data, calldata_t *params);

	static void transition_override_stop(void *data, calldata_t *);

	static void get_transitions(void *data, struct obs_frontend_source_list *sources);

private slots:
	void AddSourceFromAction();
	void VirtualCamButtonClicked();
	void ReplayButtonClicked(QString filename = "");
	void RecordButtonClicked();
	void StreamButtonClicked();
	void ConfigButtonClicked();
	void OnVirtualCamStart();
	void OnVirtualCamStop();
	void OnRecordStart();
	void OnRecordStop(int code, QString last_error);
	void OnReplaySaved();
	void OnStreamStart();
	void OnStreamStop(int code, QString last_error, QString stream_server, QString stream_key);
	void OnReplayBufferStart();
	void OnReplayBufferStop(int code, QString last_error);
	void SwitchScene(const QString &scene_name, bool transition = true);
	obs_source_t *GetTransition(const char *transition_name);
	bool SwapTransition(obs_source_t *transition);
	void StartVirtualCam();
	void StopVirtualCam();
	void SetRecordAudioEncoders(obs_output_t *output);
	obs_encoder_t *GetRecordVideoEncoder();
	obs_encoder_t *GetStreamVideoEncoder();
	obs_encoder_t *GetStreamAudioEncoder();
	void ShowNoReplayOutputError();
	void StartRecord();
	void StopRecord();
	void StartReplayBuffer();
	void StopReplayBuffer();
	void StartStream();
	void StopStream();
	void AddSceneItem(OBSSceneItem item);
	void RefreshSources(OBSScene scene);
	void ReorderSources(OBSScene scene);
	void DestroyVideo();
	void MainSceneChanged();
	void MainStreamStart();
	void MainStreamStop();
	void MainRecordStart();
	void MainRecordStop();
	void MainReplayBufferStart();
	void MainReplayBufferStop();
	void MainVirtualCamStart();
	void MainVirtualCamStop();
	void ProfileChanged();
	void OpenPreviewProjector();
	void OpenSourceProjector();
	void SwitchBackToSelectedTransition();

	void NewerVersionAvailable(QString version);
	void updateStreamKey(const QString &newStreamKey, int index);
	void updateStreamServer(const QString &newStreamServer, int index);

public:
	CanvasDock(obs_data_t *settings, QWidget *parent = nullptr);
	~CanvasDock();

	void ClearScenes();
	void StopOutputs();
	void LoadScenes();
	void FinishLoading();
	void setAction(QAction *action);
	CanvasScenesDock *GetScenesDock();
	inline uint32_t GetCanvasWidth() const { return canvas_width; }
	inline uint32_t GetCanvasHeight() const { return canvas_height; }
	inline video_t* GetVideo() const { return video; }

	obs_data_t *SaveSettings();

	obs_scene_t *GetCurrentScene();
	std::vector<QString> GetScenes();
	bool StreamingActive();
	bool RecordingActive();
	bool BacktrackActive();
	bool VirtualCameraActive();
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
