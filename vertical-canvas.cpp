#include "vertical-canvas.hpp"

#include <list>

#include "version.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QDesktopServices>

#include <QGuiApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QToolBar>
#include <QWidgetAction>

#include "audio-wrapper-source.h"
#include "config-dialog.hpp"
#include "display-helpers.hpp"
#include "media-io/video-frame.h"
#include "multi-canvas-source.h"
#include "name-dialog.hpp"
#include "obs-websocket-api.h"
#include "scenes-dock.hpp"
#include "sources-dock.hpp"
#include "transitions-dock.hpp"
#include "util/config-file.h"
#include "util/dstr.h"
#include "util/platform.h"
#include "util/util.hpp"
extern "C" {
#include "file-updater.h"
}

#ifndef _WIN32
#include <dlfcn.h>
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Aitum");
OBS_MODULE_USE_DEFAULT_LOCALE("vertical-canvas", "en-US")

#define HANDLE_RADIUS 4.0f
#define HANDLE_SEL_RADIUS (HANDLE_RADIUS * 1.5f)
#define HELPER_ROT_BREAKPONT 45.0f

#define SPACER_LABEL_MARGIN 6.0f

#define CANVAS_NAME "Aitum Vertical"

inline std::list<CanvasDock *> canvas_docks;

void clear_canvas_docks()
{
	for (const auto &it : canvas_docks) {
		it->ClearScenes();
		it->StopOutputs();
		it->close();
		it->deleteLater();
	}
	canvas_docks.clear();
}

static void ensure_directory(char *path)
{
#ifdef _WIN32
	char *backslash = strrchr(path, '\\');
	if (backslash)
		*backslash = '/';
#endif

	char *slash = strrchr(path, '/');
	if (slash) {
		*slash = 0;
		os_mkdirs(path);
		*slash = '/';
	}

#ifdef _WIN32
	if (backslash)
		*backslash = '\\';
#endif
}

static void save_canvas()
{
	if (canvas_docks.empty())
		return;
	char *path = obs_module_config_path("config.json");
	if (!path)
		return;
	ensure_directory(path);
	obs_data_t *config = obs_data_create();
	const auto canvas = obs_data_array_create();
	for (const auto &it : canvas_docks) {
		obs_data_t *s = it->SaveSettings();
		obs_data_array_push_back(canvas, s);
		obs_data_release(s);
	}
	obs_data_set_array(config, "canvas", canvas);
	obs_data_array_release(canvas);
	if (obs_data_save_json_safe(config, path, "tmp", "bak")) {
		blog(LOG_INFO, "[Vertical Canvas] Saved settings");
	} else {
		blog(LOG_ERROR, "[Vertical Canvas] Failed saving settings");
	}
	obs_data_release(config);
	bfree(path);
}

void transition_start(void *, calldata_t *)
{
	for (const auto &it : canvas_docks) {
		QMetaObject::invokeMethod(it, "MainSceneChanged", Qt::QueuedConnection);
	}
}

void frontend_event(obs_frontend_event event, void *private_data)
{
	UNUSED_PARAMETER(private_data);
	if (event == OBS_FRONTEND_EVENT_EXIT || event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
		save_canvas();
		clear_canvas_docks();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP) {
		for (const auto &it : canvas_docks) {
			it->ClearScenes();
		}
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		struct obs_frontend_source_list transitions = {};
		obs_frontend_get_transitions(&transitions);
		for (size_t i = 0; i < transitions.sources.num; i++) {
			auto sh = obs_source_get_signal_handler(transitions.sources.array[i]);
			signal_handler_connect(sh, "transition_start", transition_start, nullptr);
		}
		obs_frontend_source_list_free(&transitions);
		for (const auto &it : canvas_docks) {
			it->LoadScenes();
		}
	} else if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		struct obs_frontend_source_list transitions = {};
		obs_frontend_get_transitions(&transitions);
		for (size_t i = 0; i < transitions.sources.num; i++) {
			auto sh = obs_source_get_signal_handler(transitions.sources.array[i]);
			signal_handler_connect(sh, "transition_start", transition_start, nullptr);
		}
		obs_frontend_source_list_free(&transitions);
		for (const auto &it : canvas_docks) {
			it->LoadScenes();
			it->FinishLoading();
		}
	} else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainSceneChanged", Qt::QueuedConnection);
		}
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainStreamStart", Qt::QueuedConnection);
		}
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPING || event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainStreamStop", Qt::QueuedConnection);
			QTimer::singleShot(200, it,
					   [it] { QMetaObject::invokeMethod(it, "MainStreamStop", Qt::QueuedConnection); });
		}
	} else if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainRecordStart", Qt::QueuedConnection);
		}
	} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPING || event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainRecordStop", Qt::QueuedConnection);
			QTimer::singleShot(200, it,
					   [it] { QMetaObject::invokeMethod(it, "MainRecordStop", Qt::QueuedConnection); });
		}

	} else if (event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED) {
		for (const auto &it : canvas_docks) {
			QTimer::singleShot(250, it,
					   [it] { QMetaObject::invokeMethod(it, "MainReplayBufferStart", Qt::QueuedConnection); });
		}
	} else if (event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING || event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainReplayBufferStop", Qt::QueuedConnection);
			QTimer::singleShot(200, it,
					   [it] { QMetaObject::invokeMethod(it, "MainReplayBufferStop", Qt::QueuedConnection); });
		}

	} else if (event == OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainVirtualCamStart", Qt::QueuedConnection);
		}

	} else if (event == OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainVirtualCamStop", Qt::QueuedConnection);
			QTimer::singleShot(200, it,
					   [it] { QMetaObject::invokeMethod(it, "MainVirtualCamStop", Qt::QueuedConnection); });
		}
	} else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "ProfileChanged", Qt::QueuedConnection);
		}
	}
}

static void get_video(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const auto width = calldata_int(cd, "width");
	const auto height = calldata_int(cd, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		calldata_set_ptr(cd, "video", it->GetVideo());
		return;
	}
}

static void get_stream_settings(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const auto width = calldata_int(cd, "width");
	const auto height = calldata_int(cd, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		it->DisableStreamSettings();
		calldata_set_ptr(cd, "outputs", it->SaveStreamOutputs());
		return;
	}
}

static void set_stream_settings(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const auto width = calldata_int(cd, "width");
	const auto height = calldata_int(cd, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		obs_data_array_t *outputs = (obs_data_array_t *)calldata_ptr(cd, "outputs");
		if (outputs) {
			it->DisableStreamSettings();
			it->LoadStreamOutputs(outputs);
			it->UpdateMulti();
		}
		return;
	}
}

static void start_stream_output(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const auto width = calldata_int(cd, "width");
	const auto height = calldata_int(cd, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		auto name = calldata_string(cd, "name");
		it->StartStreamOutput(name);
		return;
	}
}

static void stop_stream_output(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const auto width = calldata_int(cd, "width");
	const auto height = calldata_int(cd, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		auto name = calldata_string(cd, "name");
		it->StopStreamOutput(name);
		return;
	}
}

static void get_stream_output(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const auto width = calldata_int(cd, "width");
	const auto height = calldata_int(cd, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		auto name = calldata_string(cd, "name");
		calldata_set_ptr(cd, "output", it->GetStreamOutput(name));
		return;
	}
}

static void add_chapter(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const auto width = calldata_int(cd, "width");
	const auto height = calldata_int(cd, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		auto output = it->GetRecordOutput();
		if (!output)
			continue;

		proc_handler_t *ph = obs_output_get_proc_handler(output);
		calldata cd2;
		calldata_init(&cd2);
		calldata_set_string(&cd2, "chapter_name", calldata_string(cd, "chapter_name"));
		proc_handler_call(ph, "add_chapter", &cd2);
		calldata_free(&cd2);
		obs_output_release(output);
		return;
	}
}

obs_websocket_vendor vendor = nullptr;

void vendor_request_version(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	UNUSED_PARAMETER(request_data);
	obs_data_set_string(response_data, "version", PROJECT_VERSION);
	obs_data_set_bool(response_data, "success", true);
}

void vendor_request_switch_scene(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	const char *scene_name = obs_data_get_string(request_data, "scene");
	if (!scene_name || !strlen(scene_name)) {
		obs_data_set_string(response_data, "error", "'scene' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	for (const auto &it : canvas_docks) {
		QMetaObject::invokeMethod(it, "SwitchScene", Q_ARG(QString, QString::fromUtf8(scene_name)));
	}

	obs_data_set_bool(response_data, "success", true);
}

void vendor_request_current_scene(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		auto scene = obs_scene_get_source(it->GetCurrentScene());
		if (scene) {
			obs_data_set_string(response_data, "scene", obs_source_get_name(scene));
		} else {
			obs_data_set_string(response_data, "scene", "");
		}
		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_get_scenes(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	auto sa = obs_data_array_create();
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		auto scenes = it->GetScenes();
		for (auto &scene : scenes) {
			auto s = obs_data_create();
			obs_data_set_string(s, "name", scene.toUtf8().constData());
			obs_data_array_push_back(sa, s);
			obs_data_release(s);
		}
	}
	obs_data_set_array(response_data, "scenes", sa);
	obs_data_array_release(sa);
	obs_data_set_bool(response_data, "success", true);
}

void vendor_request_status(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		obs_data_set_bool(response_data, "streaming", it->StreamingActive());
		obs_data_set_bool(response_data, "recording", it->RecordingActive());
		obs_data_set_bool(response_data, "backtrack", it->BacktrackActive());
		obs_data_set_bool(response_data, "virtual_camera", it->VirtualCameraActive());
		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_invoke(obs_data_t *request_data, obs_data_t *response_data, void *p)
{
	const char *method = static_cast<char *>(p);
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		QMetaObject::invokeMethod(it, method);
		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_save_replay(obs_data_t *request_data, obs_data_t *response_data, void *p)
{
	UNUSED_PARAMETER(p);
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;
		QMetaObject::invokeMethod(it, "ReplayButtonClicked",
					  Q_ARG(QString, QString::fromUtf8(obs_data_get_string(request_data, "filename"))));
		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_update_stream_key(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	// Parse request_data to get the new stream_key
	const char *new_stream_key = obs_data_get_string(request_data, "stream_key");
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");

	if (!new_stream_key || !strlen(new_stream_key)) {
		obs_data_set_string(response_data, "error", "'stream_key' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}

	// Loop through each CanvasDock to find the right one
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;

		// Update stream_key using the UpdateStreamKey method of CanvasDock
		QMetaObject::invokeMethod(it, "updateStreamKey", Q_ARG(QString, QString::fromUtf8(new_stream_key)),
					  Q_ARG(int, (int)obs_data_get_int(request_data, "index")));

		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_update_stream_server(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	// Parse request_data to get the new stream_server
	const char *new_stream_server = obs_data_get_string(request_data, "stream_server");
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");

	if (!new_stream_server || !strlen(new_stream_server)) {
		obs_data_set_string(response_data, "error", "'stream_server' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}

	// Loop through each CanvasDock to find the right one
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;

		// Update stream_server using the UpdateStreamServer method of CanvasDock
		QMetaObject::invokeMethod(it, "updateStreamServer", Q_ARG(QString, QString::fromUtf8(new_stream_server)),
					  Q_ARG(int, (int)obs_data_get_int(request_data, "index")));

		obs_data_set_bool(response_data, "success", true);
		return;
	}

	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_add_chapter(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;

		auto output = it->GetRecordOutput();
		if (!output)
			continue;

		proc_handler_t *ph = obs_output_get_proc_handler(output);
		calldata cd;
		calldata_init(&cd);
		calldata_set_string(&cd, "chapter_name", obs_data_get_string(request_data, "chapter_name"));
		bool result = proc_handler_call(ph, "add_chapter", &cd);
		calldata_free(&cd);
		obs_output_release(output);
		obs_data_set_bool(response_data, "success", result);
		return;
	}

	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_pause_recording(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;

		auto output = it->GetRecordOutput();
		if (!output || !obs_output_active(output) || obs_output_paused(output))
			continue;
		obs_output_pause(output, true);
		obs_output_release(output);
		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_unpause_recording(obs_data_t *request_data, obs_data_t *response_data, void *)
{
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) || (height && it->GetCanvasHeight() != height))
			continue;

		auto output = it->GetRecordOutput();
		if (!output || !obs_output_active(output) || !obs_output_paused(output))
			continue;
		obs_output_pause(output, false);
		obs_output_release(output);
		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

update_info_t *version_update_info = nullptr;

bool version_info_downloaded(void *param, struct file_download_data *file)
{
	UNUSED_PARAMETER(param);
	if (!file || !file->buffer.num)
		return true;
	for (const auto &it : canvas_docks) {
		QMetaObject::invokeMethod(it, "ApiInfo", Q_ARG(QString, QString::fromUtf8((const char *)file->buffer.array)));
	}
	if (version_update_info) {
		update_info_destroy(version_update_info);
		version_update_info = nullptr;
	}
	return true;
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Vertical Canvas] loaded version %s", PROJECT_VERSION);
	obs_frontend_add_event_callback(frontend_event, nullptr);

	obs_register_source(&audio_wrapper_source);
	obs_register_source(&multi_canvas_source);

	auto ph = obs_get_proc_handler();
	proc_handler_add(ph, "void aitum_vertical_get_video(in int width, in int height, out ptr video)", get_video, nullptr);
	proc_handler_add(ph, "void aitum_vertical_get_stream_settings(in int width, in int height, out ptr outputs)",
			 get_stream_settings, nullptr);
	proc_handler_add(ph, "void aitum_vertical_set_stream_settings(in int width, in int height, in ptr outputs)",
			 set_stream_settings, nullptr);
	proc_handler_add(ph, "void aitum_vertical_get_stream_output(in int width, in int height, in string name, out ptr output)",
			 get_stream_output, nullptr);
	proc_handler_add(ph, "void aitum_vertical_start_stream_output(in int width, in int height, in string name)",
			 start_stream_output, nullptr);
	proc_handler_add(ph, "void aitum_vertical_stop_stream_output(in int width, in int height, in string name)",
			 stop_stream_output, nullptr);
	proc_handler_add(ph, "void aitum_vertical_add_chapter(in int width, in int height, in string chapter_name)", add_chapter,
			 nullptr);

	return true;
}

void obs_module_post_load(void)
{
	const auto path = obs_module_config_path("config.json");
	obs_data_t *config = obs_data_create_from_json_file_safe(path, "bak");
	bfree(path);
	if (!config) {
		config = obs_data_create();
		blog(LOG_WARNING, "[Vertical Canvas] No configuration file loaded");
	} else {
		blog(LOG_INFO, "[Vertical Canvas] Loaded configuration file");
	}

	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto canvas = obs_data_get_array(config, "canvas");
	obs_data_release(config);
	if (!canvas) {
		canvas = obs_data_array_create();
		blog(LOG_WARNING, "[Vertical Canvas] no canvas found in configuration");
	}
	const auto count = obs_data_array_count(canvas);
	if (!count) {
		const auto canvasDock = new CanvasDock(nullptr, main_window);
		const QString title = QString::fromUtf8(obs_module_text("Vertical"));
		const auto name = "VerticalCanvasDock";
		obs_frontend_add_dock_by_id(name, title.toUtf8().constData(), canvasDock);
		canvas_docks.push_back(canvasDock);
		obs_data_array_release(canvas);
		blog(LOG_INFO, "[Vertical Canvas] New Canvas created");
		return;
	}
	for (size_t i = 0; i < count; i++) {
		const auto item = obs_data_array_item(canvas, i);
		const auto canvasDock = new CanvasDock(item, main_window);
		const QString title = QString::fromUtf8(obs_module_text("Vertical"));
		const auto name = "VerticalCanvasDock";
		obs_frontend_add_dock_by_id(name, title.toUtf8().constData(), canvasDock);
		obs_data_release(item);
		canvas_docks.push_back(canvasDock);
	}
	obs_data_array_release(canvas);

	if (!vendor)
		vendor = obs_websocket_register_vendor("aitum-vertical-canvas");
	if (!vendor)
		return;
	obs_websocket_vendor_register_request(vendor, "version", vendor_request_version, nullptr);
	obs_websocket_vendor_register_request(vendor, "switch_scene", vendor_request_switch_scene, nullptr);
	obs_websocket_vendor_register_request(vendor, "current_scene", vendor_request_current_scene, nullptr);
	obs_websocket_vendor_register_request(vendor, "get_scenes", vendor_request_get_scenes, nullptr);
	obs_websocket_vendor_register_request(vendor, "status", vendor_request_status, nullptr);
	obs_websocket_vendor_register_request(vendor, "start_streaming", vendor_request_invoke, (void *)"StartStream");
	obs_websocket_vendor_register_request(vendor, "stop_streaming", vendor_request_invoke, (void *)"StopStream");
	obs_websocket_vendor_register_request(vendor, "toggle_streaming", vendor_request_invoke, (void *)"StreamButtonClicked");
	obs_websocket_vendor_register_request(vendor, "start_recording", vendor_request_invoke, (void *)"StartRecord");
	obs_websocket_vendor_register_request(vendor, "stop_recording", vendor_request_invoke, (void *)"StopRecord");
	obs_websocket_vendor_register_request(vendor, "toggle_recording", vendor_request_invoke, (void *)"RecordButtonClicked");
	obs_websocket_vendor_register_request(vendor, "start_backtrack", vendor_request_invoke, (void *)"StartReplayBuffer");
	obs_websocket_vendor_register_request(vendor, "stop_backtrack", vendor_request_invoke, (void *)"StopReplayBuffer");
	obs_websocket_vendor_register_request(vendor, "save_backtrack", vendor_request_save_replay, nullptr);
	obs_websocket_vendor_register_request(vendor, "start_virtual_camera", vendor_request_invoke, (void *)"StartVirtualCam");
	obs_websocket_vendor_register_request(vendor, "stop_virtual_camera", vendor_request_invoke, (void *)"StopVirtualCam");
	obs_websocket_vendor_register_request(vendor, "update_stream_key", vendor_request_update_stream_key, nullptr);
	obs_websocket_vendor_register_request(vendor, "update_stream_server", vendor_request_update_stream_server, nullptr);
	obs_websocket_vendor_register_request(vendor, "add_chapter", vendor_request_add_chapter, nullptr);
	obs_websocket_vendor_register_request(vendor, "pause_recording", vendor_request_pause_recording, nullptr);
	obs_websocket_vendor_register_request(vendor, "unpause_recording", vendor_request_unpause_recording, nullptr);

	version_update_info = update_info_create_single("[Vertical Canvas]", "OBS", "https://api.aitum.tv/plugin/vertical",
							version_info_downloaded, nullptr);
}

void obs_module_unload(void)
{
	if (vendor && obs_get_module("obs-websocket")) {
		obs_websocket_vendor_unregister_request(vendor, "version");
		obs_websocket_vendor_unregister_request(vendor, "switch_scene");
		obs_websocket_vendor_unregister_request(vendor, "current_scene");
		obs_websocket_vendor_unregister_request(vendor, "get_scenes");
		obs_websocket_vendor_unregister_request(vendor, "status");
		obs_websocket_vendor_unregister_request(vendor, "start_streaming");
		obs_websocket_vendor_unregister_request(vendor, "stop_streaming");
		obs_websocket_vendor_unregister_request(vendor, "toggle_streaming");
		obs_websocket_vendor_unregister_request(vendor, "start_recording");
		obs_websocket_vendor_unregister_request(vendor, "stop_recording");
		obs_websocket_vendor_unregister_request(vendor, "toggle_recording");
		obs_websocket_vendor_unregister_request(vendor, "start_backtrack");
		obs_websocket_vendor_unregister_request(vendor, "stop_backtrack");
		obs_websocket_vendor_unregister_request(vendor, "save_backtrack");
		obs_websocket_vendor_unregister_request(vendor, "start_virtual_camera");
		obs_websocket_vendor_unregister_request(vendor, "stop_virtual_camera");
		obs_websocket_vendor_unregister_request(vendor, "update_stream_key");
		obs_websocket_vendor_unregister_request(vendor, "update_stream_server");
	}
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	if (version_update_info) {
		update_info_destroy(version_update_info);
		version_update_info = nullptr;
	}
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("VerticalCanvas");
}

CanvasScenesDock *CanvasDock::GetScenesDock()
{
	return scenesDock;
}

void CanvasDock::AddScene(QString duplicate, bool ask_name)
{
	std::string name = duplicate.isEmpty() ? obs_module_text("VerticalScene") : duplicate.toUtf8().constData();
	obs_source_t *s = obs_canvas_get_source_by_name(canvas, name.c_str());
	int i = 0;
	while (s) {
		obs_source_release(s);
		i++;
		name = obs_module_text("VerticalScene");
		name += " ";
		name += std::to_string(i);
		s = obs_canvas_get_source_by_name(canvas, name.c_str());
	}
	do {
		obs_source_release(s);
		if (ask_name && !NameDialog::AskForName(this, QString::fromUtf8(obs_module_text("SceneName")), name)) {
			break;
		}
		s = obs_canvas_get_source_by_name(canvas, name.c_str());
		if (s)
			continue;

		obs_source_t *new_scene = nullptr;
		if (!duplicate.isEmpty()) {
			auto origScene = obs_canvas_get_scene_by_name(canvas, duplicate.toUtf8().constData());
			if (origScene) {
				new_scene = obs_scene_get_source(obs_scene_duplicate(origScene, name.c_str(), OBS_SCENE_DUP_REFS));
				obs_scene_release(origScene);
			}
		}
		if (!new_scene) {
			obs_scene_t *canvas_scene = obs_canvas_scene_create(canvas, name.c_str());
			new_scene = obs_scene_get_source(canvas_scene);
		}
		auto sh = obs_source_get_signal_handler(new_scene);
		signal_handler_connect(sh, "rename", source_rename, this);
		auto sn = QString::fromUtf8(obs_source_get_name(new_scene));
		if (scenesCombo)
			scenesCombo->addItem(sn);
		if (scenesDock)
			scenesDock->sceneList->addItem(sn);

		SwitchScene(sn);
		obs_source_release(new_scene);
	} while (ask_name && s);
}

void CanvasDock::RemoveScene(const QString &sceneName)
{
	auto s = obs_canvas_get_source_by_name(canvas, sceneName.toUtf8().constData());
	if (!s)
		return;
	if (!obs_source_is_scene(s)) {
		obs_source_release(s);
		return;
	}

	QMessageBox mb(QMessageBox::Question, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmRemove.Title")),
		       QString::fromUtf8(obs_frontend_get_locale_string("ConfirmRemove.Text"))
			       .arg(QString::fromUtf8(obs_source_get_name(s))),
		       QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No));
	mb.setDefaultButton(QMessageBox::NoButton);
	if (mb.exec() == QMessageBox::Yes) {
		obs_source_remove(s);
	}

	obs_source_release(s);
}

void CanvasDock::SetLinkedScene(obs_source_t *scene_, const QString &linkedScene)
{
	auto ss = obs_source_get_settings(scene_);
	auto c = obs_data_get_array(ss, "canvas");

	auto count = obs_data_array_count(c);
	obs_data_t *found = nullptr;
	for (size_t i = 0; i < count; i++) {
		auto item = obs_data_array_item(c, i);
		if (!item)
			continue;
		if (obs_data_get_int(item, "width") == canvas_width && obs_data_get_int(item, "height") == canvas_height) {
			found = item;
			if (linkedScene.isEmpty()) {
				obs_data_array_erase(c, i);
			}
			break;
		}
		obs_data_release(item);
	}
	if (!linkedScene.isEmpty()) {
		if (!found) {
			if (!c) {
				c = obs_data_array_create();
				obs_data_set_array(ss, "canvas", c);
			}
			found = obs_data_create();
			obs_data_set_int(found, "width", canvas_width);
			obs_data_set_int(found, "height", canvas_height);
			obs_data_array_push_back(c, found);
		}
		obs_data_set_string(found, "scene", linkedScene.toUtf8().constData());
	}
	obs_data_release(ss);
	obs_data_release(found);
	obs_data_array_release(c);
}

bool CanvasDock::HasScene(QString sceneName) const
{
	if (scenesCombo) {
		for (int i = 0; i < scenesCombo->count(); i++) {
			if (sceneName == scenesCombo->itemText(i)) {
				return true;
			}
		}
	}
	if (scenesDock) {
		for (int i = 0; i < scenesDock->sceneList->count(); i++) {
			if (sceneName == scenesDock->sceneList->item(i)->text()) {
				return true;
			}
		}
	}
	return false;
}

void CanvasDock::CheckReplayBuffer(bool start)
{
	if (replayAlwaysOn) {
		StartReplayBuffer();
		return;
	}
	if (start && !startReplay)
		return;
	bool active = obs_frontend_streaming_active() || obs_frontend_recording_active() || obs_frontend_replay_buffer_active() ||
		      (recordOutput && obs_output_active(recordOutput));
	for (auto it = streamOutputs.begin(); !active && it != streamOutputs.end(); ++it) {
		active = it->enabled && it->output && !it->stopping && obs_output_active(it->output);
	}

	if (start && active) {
		StartReplayBuffer();
	} else if (!active) {
		if (!start)
			StopReplayBuffer();
		if (canvas && obs_canvas_has_video(canvas))
			DestroyVideo();
	}
}

void CanvasDock::CreateScenesRow()
{
	const auto sceneRow = new QHBoxLayout(this);
	scenesCombo = new QComboBox;
	connect(scenesCombo, &QComboBox::currentTextChanged, [this]() { SwitchScene(scenesCombo->currentText()); });
	sceneRow->addWidget(scenesCombo, 1);

	linkedButton = new LockedCheckBox;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	connect(linkedButton, &QCheckBox::checkStateChanged, [this] {
#else
	connect(linkedButton, &QCheckBox::stateChanged, [this] {
#endif
		auto current_scene = obs_frontend_get_current_scene();
		if (!current_scene)
			return;
		SetLinkedScene(current_scene, linkedButton->isChecked() ? scenesCombo->currentText() : "");
		obs_source_release(current_scene);
	});

	sceneRow->addWidget(linkedButton);

	auto addButton = new QPushButton;
	addButton->setProperty("themeID", "addIconSmall");
	addButton->setProperty("class", "icon-plus");
	addButton->setToolTip(QString::fromUtf8(obs_module_text("AddVerticalScene")));
	connect(addButton, &QPushButton::clicked, [this] { AddScene(); });
	sceneRow->addWidget(addButton);
	auto removeButton = new QPushButton;
	removeButton->setProperty("themeID", "removeIconSmall");
	removeButton->setProperty("class", "icon-minus");
	removeButton->setToolTip(QString::fromUtf8(obs_module_text("RemoveVerticalScene")));
	connect(removeButton, &QPushButton::clicked, [this] { RemoveScene(scenesCombo->currentText()); });
	sceneRow->addWidget(removeButton);
	mainLayout->insertLayout(0, sceneRow);
}

CanvasDock::CanvasDock(obs_data_t *settings, QWidget *parent)
	: QFrame(parent),
	  action(nullptr),
	  mainLayout(new QVBoxLayout(this)),
	  preview(new OBSQTDisplay(this)),
	  eventFilter(BuildEventFilter())
{
	if (!settings) {
		settings = obs_data_create();
		obs_data_set_bool(settings, "backtrack", true);
		first_time = true;
	}
	partnerBlockTime = (time_t)obs_data_get_int(settings, "partner_block");
	canvas_width = (uint32_t)obs_data_get_int(settings, "width");
	canvas_height = (uint32_t)obs_data_get_int(settings, "height");
	if (!canvas_width || !canvas_height) {
		canvas_width = 1080;
		canvas_height = 1920;
	}
	streamingVideoBitrate = (uint32_t)obs_data_get_int(settings, "streaming_video_bitrate");
	if (!streamingVideoBitrate)
		streamingVideoBitrate = (uint32_t)obs_data_get_int(settings, "video_bitrate");
	streamingMatchMain = obs_data_get_bool(settings, "streaming_match_main");
	recordVideoBitrate = (uint32_t)obs_data_get_int(settings, "record_video_bitrate");
	if (!recordVideoBitrate)
		recordVideoBitrate = (uint32_t)obs_data_get_int(settings, "video_bitrate");
	max_size_mb = (uint32_t)obs_data_get_int(settings, "max_size_mb");
	max_time_sec = (uint32_t)obs_data_get_int(settings, "max_time_sec");
	recordingMatchMain = obs_data_get_bool(settings, "recording_match_main");

	audioBitrate = (uint32_t)obs_data_get_int(settings, "audio_bitrate");
	startReplay = obs_data_get_bool(settings, "backtrack");
	replayAlwaysOn = false;
	replayDuration = (uint32_t)obs_data_get_int(settings, "backtrack_seconds");
	replayPath = obs_data_get_string(settings, "backtrack_path");

	virtual_cam_mode = (uint32_t)obs_data_get_int(settings, "virtual_camera_mode");

	auto so = obs_data_get_array(settings, "stream_outputs");
	multi_rtmp = LoadStreamOutputs(so);
	obs_data_array_release(so);
	if (streamOutputs.empty() && strlen(obs_data_get_string(settings, "stream_server"))) {
		StreamServer ss;
		ss.stream_server = obs_data_get_string(settings, "stream_server");
		ss.stream_key = obs_data_get_string(settings, "stream_key");
		bool whip = strstr(ss.stream_server.c_str(), "whip") != nullptr;
		ss.service = obs_service_create(whip ? "whip_custom" : "rtmp_custom", "vertical_canvas_stream_service_0", nullptr,
						nullptr);
		streamOutputs.push_back(ss);
	}

	stream_delay_enabled = obs_data_get_bool(settings, "stream_delay_enabled");
	stream_delay_duration = (uint32_t)obs_data_get_int(settings, "stream_delay_duration");
	stream_delay_preserve = obs_data_get_bool(settings, "stream_delay_preserve");

	stream_advanced_settings = obs_data_get_bool(settings, "stream_advanced_settings");
	stream_audio_track = (int)obs_data_get_int(settings, "stream_audio_track");
	stream_encoder = obs_data_get_string(settings, "stream_encoder");
	stream_encoder_settings = obs_data_get_obj(settings, "stream_encoder_settings");
	if (!stream_encoder_settings)
		stream_encoder_settings = obs_data_create();

	recordPath = obs_data_get_string(settings, "record_path");
	record_advanced_settings = obs_data_get_bool(settings, "record_advanced_settings");
	filename_formatting = obs_data_get_string(settings, "filename_formatting");
	file_format = obs_data_get_string(settings, "file_format");
	record_audio_tracks = obs_data_get_int(settings, "record_audio_tracks");
	if (!record_audio_tracks)
		record_audio_tracks = 1;
	record_encoder = obs_data_get_string(settings, "record_encoder");
	record_encoder_settings = obs_data_get_obj(settings, "record_encoder_settings");
	if (!record_encoder_settings)
		record_encoder_settings = obs_data_create();

	preview_disabled = obs_data_get_bool(settings, "preview_disabled");

	virtual_cam_warned = obs_data_get_bool(settings, "virtual_cam_warned");

	if (!record_advanced_settings && (replayAlwaysOn || startReplay)) {
		const auto profile_config = obs_frontend_get_profile_config();
		if (!config_get_bool(profile_config, "AdvOut", "RecRB") ||
		    !config_get_bool(profile_config, "SimpleOutput", "RecRB")) {
			config_set_bool(profile_config, "AdvOut", "RecRB", true);
			config_set_bool(profile_config, "SimpleOutput", "RecRB", true);
			config_save(profile_config);
		}
	}

	size_t idx = 0;
	const char *id;
	while (obs_enum_transition_types(idx++, &id)) {
		if (obs_is_source_configurable(id))
			continue;
		const char *name = obs_source_get_display_name(id);

		OBSSourceAutoRelease tr = obs_source_create_private(id, name, NULL);
		transitions.emplace_back(tr);

		//signals "transition_stop" and "transition_video_stop"
		//        TransitionFullyStopped TransitionStopped
	}

	obs_data_array_t *transition_array = obs_data_get_array(settings, "transitions");
	if (transition_array) {
		size_t c = obs_data_array_count(transition_array);
		for (size_t i = 0; i < c; i++) {
			obs_data_t *td = obs_data_array_item(transition_array, i);
			if (!td)
				continue;
			OBSSourceAutoRelease transition = obs_load_private_source(td);
			if (transition)
				transitions.emplace_back(transition);

			obs_data_release(td);
		}
		obs_data_array_release(transition_array);
	}

	auto transition = GetTransition(obs_data_get_string(settings, "transition"));
	if (!transition)
		transition = GetTransition(obs_source_get_display_name("fade_transition"));

	SwapTransition(transition);

	setObjectName(QStringLiteral("contextContainer"));
	setContentsMargins(0, 0, 0, 0);
	setLayout(mainLayout);

	const QString title = QString::fromUtf8(obs_module_text("Vertical"));

	const QString replayName = title + " " + QString::fromUtf8(obs_module_text("Backtrack"));
	auto hotkeyData = obs_data_get_obj(settings, "backtrack_hotkeys");
	replayOutput = obs_output_create("replay_buffer", replayName.toUtf8().constData(), nullptr, hotkeyData);
	obs_data_release(hotkeyData);
	auto rpsh = obs_output_get_signal_handler(replayOutput);
	signal_handler_connect(rpsh, "saved", replay_saved, this);

	if (obs_data_get_bool(settings, "scenes_row")) {
		CreateScenesRow();
	}
	scenesDock = new CanvasScenesDock(this, parent);
	scenesDock->SetGridMode(obs_data_get_bool(settings, "grid_mode"));

	const auto scenesName = "VerticalCanvasDockScenes";
	const auto scenesTitle = title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.Scenes"));
	obs_frontend_add_dock_by_id(scenesName, scenesTitle.toUtf8().constData(), scenesDock);
	sourcesDock = new CanvasSourcesDock(this, parent);
	const auto sourcesName = "VerticalCanvasDockSources";
	const auto sourcesTitle = title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.Sources"));
	obs_frontend_add_dock_by_id(sourcesName, sourcesTitle.toUtf8().constData(), sourcesDock);
	transitionsDock = new CanvasTransitionsDock(this, parent);
	const auto transitionsName = "VerticalCanvasDockTransitions";
	const auto transitionsTitle = title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.SceneTransitions"));
	obs_frontend_add_dock_by_id(transitionsName, transitionsTitle.toUtf8().constData(), transitionsDock);
	preview->setObjectName(QStringLiteral("preview"));
	preview->setMinimumSize(QSize(24, 24));
	QSizePolicy sizePolicy1(QSizePolicy::Expanding, QSizePolicy::Expanding);
	sizePolicy1.setHorizontalStretch(0);
	sizePolicy1.setVerticalStretch(0);
	sizePolicy1.setHeightForWidth(preview->sizePolicy().hasHeightForWidth());
	preview->setSizePolicy(sizePolicy1);

	preview->setMouseTracking(true);
	preview->setFocusPolicy(Qt::StrongFocus);
	preview->installEventFilter(eventFilter.get());

	auto addDrawCallback = [this]() {
		obs_display_add_draw_callback(preview->GetDisplay(), DrawPreview, this);
	};
	preview->show();
	connect(preview, &OBSQTDisplay::DisplayCreated, addDrawCallback);
	obs_display_set_enabled(preview->GetDisplay(), !preview_disabled);

	auto addNudge = [this](const QKeySequence &seq, MoveDir direction, int distance) {
		QAction *nudge = new QAction(preview);
		nudge->setShortcut(seq);
		nudge->setShortcutContext(Qt::WidgetShortcut);
		preview->addAction(nudge);
		connect(nudge, &QAction::triggered, [this, distance, direction]() { Nudge(distance, direction); });
	};

	addNudge(Qt::Key_Up, MoveDir::Up, 1);
	addNudge(Qt::Key_Down, MoveDir::Down, 1);
	addNudge(Qt::Key_Left, MoveDir::Left, 1);
	addNudge(Qt::Key_Right, MoveDir::Right, 1);
	addNudge(Qt::SHIFT | Qt::Key_Up, MoveDir::Up, 10);
	addNudge(Qt::SHIFT | Qt::Key_Down, MoveDir::Down, 10);
	addNudge(Qt::SHIFT | Qt::Key_Left, MoveDir::Left, 10);
	addNudge(Qt::SHIFT | Qt::Key_Right, MoveDir::Right, 10);

	QAction *deleteAction = new QAction(preview);
	connect(deleteAction, &QAction::triggered, [this]() {
		obs_sceneitem_t *sceneItem = GetSelectedItem();
		if (!sceneItem)
			return;
		QMessageBox mb(QMessageBox::Question, QString::fromUtf8(obs_frontend_get_locale_string("ConfirmRemove.Title")),
			       QString::fromUtf8(obs_frontend_get_locale_string("ConfirmRemove.Text"))
				       .arg(QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(sceneItem)))),
			       QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No));
		mb.setDefaultButton(QMessageBox::NoButton);
		if (mb.exec() == QMessageBox::Yes) {
			obs_sceneitem_remove(sceneItem);
		}
	});
#ifdef __APPLE__
	deleteAction->setShortcut({Qt::Key_Backspace});
#else
	deleteAction->setShortcut({Qt::Key_Delete});
#endif
	deleteAction->setShortcutContext(Qt::WidgetShortcut);
	preview->addAction(deleteAction);

	mainLayout->addWidget(preview, 1);
	preview->setVisible(!preview_disabled);

	previewDisabledWidget = new QFrame;
	auto l = new QVBoxLayout;

	auto enablePreviewButton =
		new QPushButton(QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.PreviewConextMenu.Enable")));
	connect(enablePreviewButton, &QPushButton::clicked, [this] {
		preview_disabled = false;
		obs_display_set_enabled(preview->GetDisplay(), true);
		preview->setVisible(true);
		previewDisabledWidget->setVisible(false);
	});
	l->addWidget(enablePreviewButton);

	previewDisabledWidget->setLayout(l);

	mainLayout->addWidget(previewDisabledWidget, 1);
	previewDisabledWidget->setVisible(preview_disabled);

	auto buttonRow = new QHBoxLayout(this);
	buttonRow->setContentsMargins(0, 0, 0, 0);

	auto streamButtonGroup = new QWidget();
	auto streamButtonGroupLayout = new QHBoxLayout();
	streamButtonGroupLayout->setContentsMargins(0, 0, 0, 0);

	streamButtonGroupLayout->setSpacing(0);
	streamButtonGroup->setLayout(streamButtonGroupLayout);

	streamButton = new QPushButton;
	streamButton->setMinimumHeight(30);
	streamButton->setObjectName(QStringLiteral("canvasStream"));
	streamButton->setIcon(streamInactiveIcon);
	streamButton->setCheckable(true);
	streamButton->setChecked(false);
	streamButton->setToolTip(QString::fromUtf8(obs_module_text("StreamVertical")));
	streamButton->setStyleSheet(
		QString::fromUtf8("QPushButton:checked{background: rgb(0,210,153);}") +
		QString::fromUtf8(multi_rtmp ? "QPushButton{border-top-right-radius: 0; border-bottom-right-radius: 0;}" : ""));
	connect(streamButton, SIGNAL(clicked()), this, SLOT(StreamButtonClicked()));
	streamButtonGroup->layout()->addWidget(streamButton);

	// Little up arrow in the case of there being multiple enabled outputs
	streamButtonMulti = new QPushButton;
	streamButtonMulti->setMinimumHeight(30);
	streamButtonMulti->setObjectName(QStringLiteral("canvasStreamMulti"));
	streamButtonMulti->setToolTip(QString::fromUtf8(obs_module_text("StreamVerticalMulti")));
	streamButtonMulti->setChecked(false);
	auto multi_menu = new QMenu(this);
	connect(multi_menu, &QMenu::aboutToShow, [this, multi_menu] { StreamButtonMultiMenu(multi_menu); });
	streamButtonMulti->setMenu(multi_menu);
	streamButtonMulti->setStyleSheet(
		QString::fromUtf8("QPushButton{width: 16px; border-top-left-radius: 0; border-bottom-left-radius: 0;}"));
	streamButtonMulti->setVisible(multi_rtmp);
	streamButtonGroup->layout()->addWidget(streamButtonMulti);

	buttonRow->addWidget(streamButtonGroup);

	recordButton = new QPushButton;
	recordButton->setMinimumHeight(30);
	recordButton->setObjectName(QStringLiteral("canvasRecord"));
	recordButton->setIcon(recordInactiveIcon);
	recordButton->setCheckable(true);
	recordButton->setChecked(false);
	recordButton->setStyleSheet(QString::fromUtf8("QPushButton:checked{background: rgb(255,0,0);}"));
	recordButton->setToolTip(QString::fromUtf8(obs_module_text("RecordVertical")));
	connect(recordButton, SIGNAL(clicked()), this, SLOT(RecordButtonClicked()));
	buttonRow->addWidget(recordButton);

	auto replayButtonGroupLayout = new QHBoxLayout();
	replayButtonGroupLayout->setContentsMargins(0, 0, 0, 0);
	replayButtonGroupLayout->setSpacing(0);

	replayButton = new QPushButton;
	replayButton->setMinimumHeight(30);
	replayButton->setObjectName(QStringLiteral("canvasReplay"));
	replayButton->setIcon(replayInactiveIcon);
	replayButton->setContentsMargins(0, 0, 0, 0);
	replayButton->setToolTip(QString::fromUtf8(obs_module_text("BacktrackClipVertical")));
	replayButton->setCheckable(true);
	replayButton->setStyleSheet(QString::fromUtf8(
		"QPushButton:checked{background: rgb(26,87,255);} QPushButton{width: 32px; padding-left: 0px; padding-right: 0px; border-top-left-radius: 0; border-bottom-left-radius: 0;}"));
	connect(replayButton, SIGNAL(clicked()), this, SLOT(ReplayButtonClicked()));

	replayEnableButton = new QPushButton;
	replayEnableButton->setMinimumHeight(30);
	replayEnableButton->setObjectName(QStringLiteral("canvasBacktrackEnable"));
	replayEnableButton->setToolTip(QString::fromUtf8(obs_module_text("BacktrackOn")));
	replayEnableButton->setCheckable(true);
	replayEnableButton->setChecked(false);
	replayEnableButton->setStyleSheet(QString::fromUtf8(
		"QPushButton:checked{background: rgb(26,87,255);} QPushButton{ border-top-right-radius: 0; border-bottom-right-radius: 0; width: 32px; padding-left: 0px; padding-right: 0px;}"));
	replayEnable = new QCheckBox;
	auto testl = new QHBoxLayout;
	replayEnableButton->setLayout(testl);
	testl->addWidget(replayEnable);

	connect(replayEnableButton, &QPushButton::clicked, [this] {
		replayAlwaysOn = !replayAlwaysOn;
		replayEnableButton->setChecked(replayAlwaysOn);
		replayEnable->setChecked(replayAlwaysOn);
		CheckReplayBuffer();
	});

	backtrack_hotkey = obs_hotkey_pair_register_frontend(
		"VerticalCanvasDockStartBacktrack",
		(title + " " + QString::fromUtf8(obs_module_text("BacktrackOn"))).toUtf8().constData(),
		"VerticalCanvasDockStopBacktrack",
		(title + " " + QString::fromUtf8(obs_module_text("BacktrackOff"))).toUtf8().constData(),
		[](void *param, obs_hotkey_pair_id, obs_hotkey_t *, bool pressed) {
			auto cd = (CanvasDock *)param;
			if (!cd->replayAlwaysOn && pressed) {
				cd->replayAlwaysOn = true;
				cd->replayEnableButton->setChecked(cd->replayAlwaysOn);
				cd->replayEnable->setChecked(cd->replayAlwaysOn);
				cd->CheckReplayBuffer();
				return true;
			}
			return false;
		},
		[](void *param, obs_hotkey_pair_id, obs_hotkey_t *, bool pressed) {
			auto cd = (CanvasDock *)param;
			if (cd->replayAlwaysOn && pressed) {
				cd->replayAlwaysOn = false;
				cd->replayEnableButton->setChecked(cd->replayAlwaysOn);
				cd->replayEnable->setChecked(cd->replayAlwaysOn);
				cd->CheckReplayBuffer();
				return true;
			}
			return false;
		},
		this, this);

	obs_data_array_t *start_hotkey = obs_data_get_array(settings, "start_backtrack_hotkey");
	obs_data_array_t *stop_hotkey = obs_data_get_array(settings, "stop_backtrack_hotkey");
	obs_hotkey_pair_load(backtrack_hotkey, start_hotkey, stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	connect(replayEnable, &QCheckBox::checkStateChanged, [this] {
#else
	connect(replayEnable, &QCheckBox::stateChanged, [this] {
#endif
		if (replayEnable->isChecked() != replayEnableButton->isChecked()) {
			replayEnableButton->setChecked(replayEnable->isChecked());
			if (replayEnable->isChecked() == replayAlwaysOn)
				return;
			replayAlwaysOn = replayEnable->isChecked();
			CheckReplayBuffer();
		}
	});

	replayButtonGroupLayout->addWidget(replayEnableButton);
	replayButtonGroupLayout->addWidget(replayButton);

	buttonRow->addLayout(replayButtonGroupLayout);

	virtualCamButton = new QPushButton;
	virtualCamButton->setMinimumHeight(30);
	virtualCamButton->setObjectName(QStringLiteral("canvasVirtualCam"));
	virtualCamButton->setIcon(virtualCamInactiveIcon);
	virtualCamButton->setCheckable(true);
	virtualCamButton->setChecked(false);
	virtualCamButton->setStyleSheet(QString::fromUtf8("QPushButton:checked{background: rgb(192,128,0);}"));
	virtualCamButton->setToolTip(QString::fromUtf8(obs_module_text("VirtualCameraVertical")));
	connect(virtualCamButton, SIGNAL(clicked()), this, SLOT(VirtualCamButtonClicked()));
	buttonRow->addWidget(virtualCamButton);

	statusLabel = new QLabel;
	statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	buttonRow->addWidget(statusLabel, 1);

	recordDurationTimer.setInterval(1000);
	recordDurationTimer.setSingleShot(false);
	connect(&recordDurationTimer, &QTimer::timeout, [this] {
		if (obs_output_active(recordOutput)) {
			int totalFrames = obs_output_get_total_frames(recordOutput);
			video_t *output_video = obs_output_video(recordOutput);
			uint64_t frameTimeNs = video_output_get_frame_time(output_video);
			auto t = QTime::fromMSecsSinceStartOfDay((int)util_mul_div64(totalFrames, frameTimeNs, 1000000ULL));
			recordButton->setText(t.toString(t.hour() ? "hh:mm:ss" : "mm:ss"));
		} else if (!recordButton->text().isEmpty()) {
			recordButton->setText("");
		}
		QString streamButtonText;
		for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
			if (!obs_output_active(it->output))
				continue;

			int totalFrames = obs_output_get_total_frames(it->output);
			video_t *output_video = obs_output_video(it->output);
			uint64_t frameTimeNs = video_output_get_frame_time(output_video);
			auto t = QTime::fromMSecsSinceStartOfDay((int)util_mul_div64(totalFrames, frameTimeNs, 1000000ULL));
			streamButtonText = t.toString(t.hour() ? "hh:mm:ss" : "mm:ss");
			break;
		}
		if (streamButton->text() != streamButtonText) {
			streamButton->setText(streamButtonText);
		}
	});
	recordDurationTimer.start();

	replayStatusResetTimer.setInterval(4000);
	replayStatusResetTimer.setSingleShot(true);
	connect(&replayStatusResetTimer, &QTimer::timeout, [this] { statusLabel->setText(""); });

	configButton = new QPushButton(this);
	configButton->setMinimumHeight(30);
	configButton->setProperty("themeID", "configIconSmall");
	configButton->setProperty("class", "icon-gear");
	configButton->setFlat(true);
	configButton->setAutoDefault(false);
	configButton->setToolTip(QString::fromUtf8(obs_module_text("VerticalSettings")));
	connect(configButton, SIGNAL(clicked()), this, SLOT(ConfigButtonClicked()));
	buttonRow->addWidget(configButton);

	auto aitumButtonGroupLayout = new QHBoxLayout();
	aitumButtonGroupLayout->setContentsMargins(0, 0, 0, 0);
	aitumButtonGroupLayout->setSpacing(0);

	auto contributeButton = new QPushButton;
	contributeButton->setMinimumHeight(30);
	QPixmap pixmap(32, 32);
	pixmap.fill(Qt::transparent);

	QPainter painter(&pixmap);
	QFont font = painter.font();
	font.setPixelSize(32);
	painter.setFont(font);
	painter.drawText(pixmap.rect(), Qt::AlignCenter, "❤️");
	contributeButton->setIcon(QIcon(pixmap));
	contributeButton->setToolTip(QString::fromUtf8(obs_module_text("VerticalDonate")));
	contributeButton->setStyleSheet(
		QString::fromUtf8("QPushButton{ border-top-right-radius: 0; border-bottom-right-radius: 0;}"));
	QPushButton::connect(contributeButton, &QPushButton::clicked,
			     [] { QDesktopServices::openUrl(QUrl("https://aitum.tv/contribute")); });

	aitumButtonGroupLayout->addWidget(contributeButton);

	auto aitumButton = new QPushButton;
	aitumButton->setMinimumHeight(30);
	aitumButton->setIcon(QIcon(":/aitum/media/aitum.png"));
	aitumButton->setToolTip(QString::fromUtf8("https://aitum.tv"));
	aitumButton->setStyleSheet(QString::fromUtf8("QPushButton{border-top-left-radius: 0; border-bottom-left-radius: 0;}"));
	connect(aitumButton, &QPushButton::clicked, [] { QDesktopServices::openUrl(QUrl("https://aitum.tv")); });
	aitumButtonGroupLayout->addWidget(aitumButton);

	buttonRow->addLayout(aitumButtonGroupLayout);

	setStyleSheet(QString::fromUtf8("QPushButton{padding-left: 4px; padding-right: 4px;}"));

	mainLayout->addLayout(buttonRow);

	obs_enter_graphics();

	gs_render_start(true);
	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(0.0f, 1.0f);
	gs_vertex2f(1.0f, 0.0f);
	gs_vertex2f(1.0f, 1.0f);
	box = gs_render_save();

	obs_leave_graphics();

	currentSceneName = QString::fromUtf8(obs_data_get_string(settings, "current_scene"));

	auto sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_rename", source_rename, this);
	signal_handler_connect(sh, "source_remove", source_remove, this);
	signal_handler_connect(sh, "source_destroy", source_remove, this);
	//signal_handler_connect(sh, "source_create", source_create, this);
	//signal_handler_connect(sh, "source_load", source_load, this);
	signal_handler_connect(sh, "source_save", source_save, this);

	virtual_cam_hotkey = obs_hotkey_pair_register_frontend(
		"VerticalCanvasDockStartVirtualCam",
		(title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.StartVirtualCam"))).toUtf8().constData(),
		"VerticalCanvasDockStopVirtualCam",
		(title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.StopVirtualCam"))).toUtf8().constData(),
		start_virtual_cam_hotkey, stop_virtual_cam_hotkey, this, this);

	start_hotkey = obs_data_get_array(settings, "start_virtual_cam_hotkey");
	stop_hotkey = obs_data_get_array(settings, "stop_virtual_cam_hotkey");
	obs_hotkey_pair_load(virtual_cam_hotkey, start_hotkey, stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);

	record_hotkey = obs_hotkey_pair_register_frontend(
		"VerticalCanvasDockStartRecording",
		(title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.StartRecording"))).toUtf8().constData(),
		"VerticalCanvasDockStopRecording",
		(title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.StopRecording"))).toUtf8().constData(),
		start_recording_hotkey, stop_recording_hotkey, this, this);

	start_hotkey = obs_data_get_array(settings, "start_record_hotkey");
	stop_hotkey = obs_data_get_array(settings, "stop_record_hotkey");
	obs_hotkey_pair_load(record_hotkey, start_hotkey, stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);

	stream_hotkey = obs_hotkey_pair_register_frontend(
		"VerticalCanvasDockStartStreaming",
		(title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.StartStreaming"))).toUtf8().constData(),
		"VerticalCanvasDockStopStreaming",
		(title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.StopStreaming"))).toUtf8().constData(),
		start_streaming_hotkey, stop_streaming_hotkey, this, this);

	start_hotkey = obs_data_get_array(settings, "start_stream_hotkey");
	stop_hotkey = obs_data_get_array(settings, "stop_stream_hotkey");
	obs_hotkey_pair_load(stream_hotkey, start_hotkey, stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);

	pause_hotkey = obs_hotkey_pair_register_frontend(
		"VerticalCanvasDockPause",
		(title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.PauseRecording"))).toUtf8().constData(),
		"VerticalCanvasDockUnpause",
		(title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.UnpauseRecording")))
			.toUtf8()
			.constData(),
		pause_recording_hotkey, unpause_recording_hotkey, this, this);

	start_hotkey = obs_data_get_array(settings, "pause_hotkey");
	stop_hotkey = obs_data_get_array(settings, "unpause_hotkey");
	obs_hotkey_pair_load(pause_hotkey, start_hotkey, stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);

	chapter_hotkey = obs_hotkey_register_frontend(
		"VerticalCanvasDockChapter",
		(title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.AddChapterMarker")))
			.toUtf8()
			.constData(),
		recording_chapter_hotkey, this);

	start_hotkey = obs_data_get_array(settings, "chapter_hotkey");
	obs_hotkey_load(chapter_hotkey, start_hotkey);
	obs_data_array_release(start_hotkey);

	split_hotkey = obs_hotkey_register_frontend(
		"VerticalCanvasDockSplit",
		(title + " " + QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.SplitFile"))).toUtf8().constData(),
		recording_split_hotkey, this);

	start_hotkey = obs_data_get_array(settings, "split_hotkey");
	obs_hotkey_load(split_hotkey, start_hotkey);
	obs_data_array_release(start_hotkey);

	if (first_time) {
		obs_data_release(settings);
	}
	hide();

	transitionAudioWrapper =
		obs_source_create_private("vertical_audio_wrapper_source", "vertical_audio_wrapper_source", nullptr);
	auto aw = (struct audio_wrapper_info *)obs_obj_get_data(transitionAudioWrapper);
	if (aw) {
		aw->param = this;
		aw->target = [](void *param) {
			CanvasDock *dock = reinterpret_cast<CanvasDock *>(param);
			return obs_weak_source_get_source(dock->source);
		};
	}
}

CanvasDock::~CanvasDock()
{
	for (auto projector : projectors) {
		delete projector;
	}
	canvas_docks.remove(this);
	obs_hotkey_pair_unregister(backtrack_hotkey);
	obs_hotkey_pair_unregister(virtual_cam_hotkey);
	obs_hotkey_pair_unregister(record_hotkey);
	obs_hotkey_pair_unregister(stream_hotkey);
	obs_hotkey_pair_unregister(pause_hotkey);
	obs_hotkey_unregister(chapter_hotkey);
	obs_hotkey_unregister(split_hotkey);
	obs_display_remove_draw_callback(preview->GetDisplay(), DrawPreview, this);
	for (uint32_t i = MAX_CHANNELS - 1; i > 0; i--) {
		auto s = obs_get_output_source(i);
		if (s == transitionAudioWrapper) {
			obs_source_release(s);
			obs_set_output_source(i, nullptr);
			break;
		}
		obs_source_release(s);
	}
	obs_source_release(transitionAudioWrapper);
	transitionAudioWrapper = nullptr;
	sourcesDock = nullptr;
	scenesDock = nullptr;
	transitionsDock = nullptr;
	auto sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_rename", source_rename, this);
	signal_handler_disconnect(sh, "source_remove", source_remove, this);
	signal_handler_disconnect(sh, "source_destroy", source_remove, this);
	//signal_handler_disconnect(sh, "source_create", source_create, this);
	//signal_handler_disconnect(sh, "source_load", source_load, this);
	signal_handler_disconnect(sh, "source_save", source_save, this);

	if (obs_output_active(recordOutput))
		obs_output_stop(recordOutput);
	obs_output_release(recordOutput);
	recordOutput = nullptr;

	if (replayOutput) {
		auto rpsh = obs_output_get_signal_handler(replayOutput);
		signal_handler_disconnect(rpsh, "saved", replay_saved, this);
	}

	if (obs_output_active(replayOutput))
		obs_output_stop(replayOutput);
	obs_output_release(replayOutput);
	replayOutput = nullptr;

	if (obs_output_active(virtualCamOutput))
		obs_output_stop(virtualCamOutput);
	obs_output_release(virtualCamOutput);
	virtualCamOutput = nullptr;

	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		if (obs_output_active(it->output))
			obs_output_stop(it->output);
		obs_service_release(obs_output_get_service(it->output));
		obs_output_release(it->output);
		obs_data_release(it->settings);
	}
	streamOutputs.clear();

	obs_data_release(stream_encoder_settings);
	obs_data_release(record_encoder_settings);

	if (multiCanvasSource) {
		multi_canvas_source_remove_canvas(obs_obj_get_data(multiCanvasSource), canvas);
		obs_source_release(multiCanvasSource);
		multiCanvasSource = nullptr;
	}

	if (multiCanvasVideo) {
		multiCanvasVideo = nullptr;
		obs_canvas_set_channel(multiCanvas, 0, nullptr);
	}
	if (multiCanvas) {
		obs_canvas_remove(multiCanvas);
		obs_canvas_release(multiCanvas);
		multiCanvas = nullptr;
	}

	auto ph = obs_get_proc_handler();
	calldata_t cd = {0};
	calldata_set_string(&cd, "canvas_name", CANVAS_NAME);
	proc_handler_call(ph, "downstream_keyer_remove_canvas", &cd);
	calldata_free(&cd);

	DestroyVideo();

	if (canvas) {
		obs_canvas_remove(canvas);
		obs_canvas_release(canvas);
		canvas = nullptr;
	}

	obs_enter_graphics();

	if (overflow)
		gs_texture_destroy(overflow);
	if (rectFill)
		gs_vertexbuffer_destroy(rectFill);
	if (circleFill)
		gs_vertexbuffer_destroy(circleFill);

	gs_vertexbuffer_destroy(box);
	obs_leave_graphics();

	obs_source_t *oldTransition = obs_weak_source_get_source(source);
	if (oldTransition && obs_source_get_type(oldTransition) == OBS_SOURCE_TYPE_TRANSITION) {
		obs_weak_source_release(source);
		source = nullptr;
		signal_handler_t *handler = obs_source_get_signal_handler(oldTransition);
		signal_handler_disconnect(handler, "transition_stop", transition_override_stop, this);
		obs_source_dec_showing(oldTransition);
		obs_source_dec_active(oldTransition);
	}
	obs_source_release(oldTransition);

	transitions.clear();
}

void CanvasDock::setAction(QAction *a)
{
	action = a;
}

static bool SceneItemHasVideo(obs_sceneitem_t *item)
{
	const obs_source_t *source = obs_sceneitem_get_source(item);
	const uint32_t flags = obs_source_get_output_flags(source);
	return (flags & OBS_SOURCE_VIDEO) != 0;
}

config_t *get_user_config(void)
{
	return obs_frontend_get_user_config();
}

void CanvasDock::DrawOverflow(float scale)
{
	if (locked)
		return;

	auto config = get_user_config();
	if (!config)
		return;

	bool hidden = config_get_bool(config, "BasicWindow", "OverflowHidden");

	if (hidden)
		return;

	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "DrawOverflow");

	if (!overflow) {
		const auto file = obs_module_file("images/overflow.png");
		overflow = gs_texture_create_from_file(file);
		bfree(file);
	}

	if (scene) {
		gs_matrix_push();
		gs_matrix_scale3f(scale, scale, 1.0f);
		obs_scene_enum_items(scene, DrawSelectedOverflow, this);
		gs_matrix_pop();
	}

	gs_load_vertexbuffer(nullptr);

	GS_DEBUG_MARKER_END();
}

static bool CloseFloat(float a, float b, float epsilon = 0.01f)
{
	return std::abs(a - b) <= epsilon;
}

bool CanvasDock::DrawSelectedOverflow(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	if (obs_sceneitem_locked(item))
		return true;

	if (!SceneItemHasVideo(item))
		return true;

	auto config = get_user_config();
	if (!config)
		return true;

	bool select = config_get_bool(config, "BasicWindow", "OverflowSelectionHidden");

	if (!select && !obs_sceneitem_visible(item))
		return true;

	if (obs_sceneitem_is_group(item)) {
		matrix4 mat;
		obs_sceneitem_get_draw_transform(item, &mat);

		gs_matrix_push();
		gs_matrix_mul(&mat);
		obs_sceneitem_group_enum_items(item, DrawSelectedOverflow, param);
		gs_matrix_pop();
	}

	bool always = config_get_bool(config, "BasicWindow", "OverflowAlwaysVisible");

	if (!always && !obs_sceneitem_selected(item))
		return true;

	CanvasDock *prev = reinterpret_cast<CanvasDock *>(param);

	matrix4 boxTransform;
	matrix4 invBoxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);
	matrix4_inv(&invBoxTransform, &boxTransform);

	vec3 bounds[] = {
		{{{0.f, 0.f, 0.f}}},
		{{{1.f, 0.f, 0.f}}},
		{{{0.f, 1.f, 0.f}}},
		{{{1.f, 1.f, 0.f}}},
	};

	bool visible = std::all_of(std::begin(bounds), std::end(bounds), [&](const vec3 &b) {
		vec3 pos;
		vec3_transform(&pos, &b, &boxTransform);
		vec3_transform(&pos, &pos, &invBoxTransform);
		return CloseFloat(pos.x, b.x) && CloseFloat(pos.y, b.y);
	});

	if (!visible)
		return true;

	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "DrawSelectedOverflow");

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_REPEAT);
	gs_eparam_t *image = gs_effect_get_param_by_name(solid, "image");
	gs_eparam_t *scale = gs_effect_get_param_by_name(solid, "scale");

	vec2 s;
	vec2_set(&s, boxTransform.x.x / 96, boxTransform.y.y / 96);

	gs_effect_set_vec2(scale, &s);
	gs_effect_set_texture(image, prev->overflow);

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);

	while (gs_effect_loop(solid, "Draw")) {
		gs_draw_sprite(prev->overflow, 0, 1, 1);
	}

	gs_matrix_pop();

	GS_DEBUG_MARKER_END();

	UNUSED_PARAMETER(scene);
	return true;
}

void CanvasDock::DrawBackdrop(float cx, float cy)
{
	if (!box)
		return;

	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "DrawBackdrop");

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

	vec4 colorVal;
	vec4_set(&colorVal, 0.0f, 0.0f, 0.0f, 1.0f);
	gs_effect_set_vec4(color, &colorVal);

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);
	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_scale3f(float(cx), float(cy), 1.0f);

	gs_load_vertexbuffer(box);
	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_pop();
	gs_technique_end_pass(tech);
	gs_technique_end(tech);

	gs_load_vertexbuffer(nullptr);

	GS_DEBUG_MARKER_END();
}

void CanvasDock::DrawPreview(void *data, uint32_t cx, uint32_t cy)
{
	CanvasDock *window = static_cast<CanvasDock *>(data);

	uint32_t sourceCX = window->canvas_width;
	if (sourceCX <= 0)
		sourceCX = 1;
	uint32_t sourceCY = window->canvas_height;
	if (sourceCY <= 0)
		sourceCY = 1;

	int x, y;
	float scale;

	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);
	if (window->previewScale != scale)
		window->previewScale = scale;
	auto newCX = scale * float(sourceCX);
	auto newCY = scale * float(sourceCY);

	/* auto extraCx = (window->zoom - 1.0f) * newCX;
	auto extraCy = (window->zoom - 1.0f) * newCY;
	int newCx = newCX * window->zoom;
	int newCy = newCY * window->zoom;
	x -= extraCx * window->scrollX;
	y -= extraCy * window->scrollY;*/
	gs_viewport_push();
	gs_projection_push();

	gs_ortho(float(-x), newCX + float(x), float(-y), newCY + float(y), -100.0f, 100.0f);
	gs_reset_viewport();

	window->DrawOverflow(scale);

	window->DrawBackdrop(newCX, newCY);

	const bool previous = gs_set_linear_srgb(true);

	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	gs_set_viewport(x, y, (int)newCX, (int)newCY);
	if (window->canvas)
		obs_canvas_render(window->canvas);

	gs_set_linear_srgb(previous);

	gs_ortho(float(-x), newCX + float(x), float(-y), newCY + float(y), -100.0f, 100.0f);
	gs_reset_viewport();

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	if (window->scene && !window->locked) {
		gs_matrix_push();
		gs_matrix_scale3f(scale, scale, 1.0f);
		obs_scene_enum_items(window->scene, DrawSelectedItem, data);
		gs_matrix_pop();
	}

	if (window->selectionBox) {
		if (!window->rectFill) {
			gs_render_start(true);

			gs_vertex2f(0.0f, 0.0f);
			gs_vertex2f(1.0f, 0.0f);
			gs_vertex2f(0.0f, 1.0f);
			gs_vertex2f(1.0f, 1.0f);

			window->rectFill = gs_render_save();
		}

		window->DrawSelectionBox(window->startPos.x * scale, window->startPos.y * scale, window->mousePos.x * scale,
					 window->mousePos.y * scale, window->rectFill);
	}
	gs_load_vertexbuffer(nullptr);

	gs_technique_end_pass(tech);
	gs_technique_end(tech);

	if (window->drawSpacingHelpers)
		window->DrawSpacingHelpers(window->scene, (float)x, (float)y, newCX, newCY, scale, float(sourceCX),
					   float(sourceCY));

	gs_projection_pop();
	gs_viewport_pop();
}

struct SceneFindData {
	const vec2 &pos;
	OBSSceneItem item;
	bool selectBelow;

	obs_sceneitem_t *group = nullptr;

	SceneFindData(const SceneFindData &) = delete;
	SceneFindData(SceneFindData &&) = delete;
	SceneFindData &operator=(const SceneFindData &) = delete;
	SceneFindData &operator=(SceneFindData &&) = delete;

	inline SceneFindData(const vec2 &pos_, bool selectBelow_) : pos(pos_), selectBelow(selectBelow_) {}
};

struct SceneFindBoxData {
	const vec2 &startPos;
	const vec2 &pos;
	std::vector<obs_sceneitem_t *> sceneItems;

	SceneFindBoxData(const SceneFindData &) = delete;
	SceneFindBoxData(SceneFindData &&) = delete;
	SceneFindBoxData &operator=(const SceneFindData &) = delete;
	SceneFindBoxData &operator=(SceneFindData &&) = delete;

	inline SceneFindBoxData(const vec2 &startPos_, const vec2 &pos_) : startPos(startPos_), pos(pos_) {}
};

bool CanvasDock::FindSelected(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	SceneFindBoxData *data = reinterpret_cast<SceneFindBoxData *>(param);

	if (obs_sceneitem_selected(item))
		data->sceneItems.push_back(item);

	UNUSED_PARAMETER(scene);
	return true;
}

static vec2 GetItemSize(obs_sceneitem_t *item)
{
	obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(item);
	vec2 size;

	if (boundsType != OBS_BOUNDS_NONE) {
		obs_sceneitem_get_bounds(item, &size);
	} else {
		obs_source_t *source = obs_sceneitem_get_source(item);
		obs_sceneitem_crop crop;
		vec2 scale;

		obs_sceneitem_get_scale(item, &scale);
		obs_sceneitem_get_crop(item, &crop);
		size.x = float(obs_source_get_width(source) - crop.left - crop.right) * scale.x;
		size.y = float(obs_source_get_height(source) - crop.top - crop.bottom) * scale.y;
	}

	return size;
}

static vec3 GetTransformedPos(float x, float y, const matrix4 &mat)
{
	vec3 result;
	vec3_set(&result, x, y, 0.0f);
	vec3_transform(&result, &result, &mat);
	return result;
}

static void DrawLine(float x1, float y1, float x2, float y2, float thickness, vec2 scale)
{
	float ySide = (y1 == y2) ? (y1 < 0.5f ? 1.0f : -1.0f) : 0.0f;
	float xSide = (x1 == x2) ? (x1 < 0.5f ? 1.0f : -1.0f) : 0.0f;

	gs_render_start(true);

	gs_vertex2f(x1, y1);
	gs_vertex2f(x1 + (xSide * (thickness / scale.x)), y1 + (ySide * (thickness / scale.y)));
	gs_vertex2f(x2 + (xSide * (thickness / scale.x)), y2 + (ySide * (thickness / scale.y)));
	gs_vertex2f(x2, y2);
	gs_vertex2f(x1, y1);

	gs_vertbuffer_t *line = gs_render_save();

	gs_load_vertexbuffer(line);
	gs_draw(GS_TRISTRIP, 0, 0);
	gs_vertexbuffer_destroy(line);
}

void CanvasDock::DrawSpacingLine(vec3 &start, vec3 &end, vec3 &viewport, float pixelRatio)
{
	matrix4 transform;
	matrix4_identity(&transform);
	transform.x.x = viewport.x;
	transform.y.y = viewport.y;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

	QColor selColor = GetSelectionColor();
	vec4 color;
	vec4_set(&color, selColor.redF(), selColor.greenF(), selColor.blueF(), 1.0f);

	gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &color);

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	gs_matrix_push();
	gs_matrix_mul(&transform);

	vec2 scale;
	vec2_set(&scale, viewport.x, viewport.y);

	DrawLine(start.x, start.y, end.x, end.y, pixelRatio * (HANDLE_RADIUS / 2), scale);

	gs_matrix_pop();

	gs_load_vertexbuffer(nullptr);

	gs_technique_end_pass(tech);
	gs_technique_end(tech);
}

void CanvasDock::SetLabelText(int sourceIndex, int px)
{

	if (px == spacerPx[sourceIndex])
		return;

	std::string text = std::to_string(px) + " px";

	obs_source_t *s = spacerLabel[sourceIndex];

	OBSDataAutoRelease settings = obs_source_get_settings(s);
	obs_data_set_string(settings, "text", text.c_str());
	obs_source_update(s, settings);

	spacerPx[sourceIndex] = px;
}

static void DrawLabel(OBSSource source, vec3 &pos, vec3 &viewport)
{
	if (!source)
		return;

	vec3_mul(&pos, &pos, &viewport);

	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_translate(&pos);
	obs_source_video_render(source);
	gs_matrix_pop();
}

void CanvasDock::RenderSpacingHelper(int sourceIndex, vec3 &start, vec3 &end, vec3 &viewport, float pixelRatio)
{
	bool horizontal = (sourceIndex == 2 || sourceIndex == 3);

	// If outside of preview, don't render
	if (!((horizontal && (end.x >= start.x)) || (!horizontal && (end.y >= start.y))))
		return;

	float length = vec3_dist(&start, &end);

	float px;

	if (horizontal) {
		px = length * (float)canvas_width;
	} else {
		px = length * (float)canvas_height;
	}

	if (px <= 0.0f)
		return;

	obs_source_t *s = spacerLabel[sourceIndex];
	vec3 labelSize, labelPos;
	vec3_set(&labelSize, (float)obs_source_get_width(s), (float)obs_source_get_height(s), 1.0f);

	vec3_div(&labelSize, &labelSize, &viewport);

	vec3 labelMargin;
	vec3_set(&labelMargin, SPACER_LABEL_MARGIN * pixelRatio, SPACER_LABEL_MARGIN * pixelRatio, 1.0f);
	vec3_div(&labelMargin, &labelMargin, &viewport);

	vec3_set(&labelPos, end.x, end.y, end.z);
	if (horizontal) {
		labelPos.x -= (end.x - start.x) / 2;
		labelPos.x -= labelSize.x / 2;
		labelPos.y -= labelMargin.y + (labelSize.y / 2) + (HANDLE_RADIUS / viewport.y);
	} else {
		labelPos.y -= (end.y - start.y) / 2;
		labelPos.y -= labelSize.y / 2;
		labelPos.x += labelMargin.x;
	}

	DrawSpacingLine(start, end, viewport, pixelRatio);
	SetLabelText(sourceIndex, (int)px);
	DrawLabel(s, labelPos, viewport);
}

static obs_source_t *CreateLabel(float pixelRatio, int i)
{
	OBSDataAutoRelease settings = obs_data_create();
	OBSDataAutoRelease font = obs_data_create();

#if defined(_WIN32)
	obs_data_set_string(font, "face", "Arial");
#elif defined(__APPLE__)
	obs_data_set_string(font, "face", "Helvetica");
#else
	obs_data_set_string(font, "face", "Monospace");
#endif
	obs_data_set_int(font, "flags", 1); // Bold text
	obs_data_set_int(font, "size", (int)(16.0f * pixelRatio));

	obs_data_set_obj(settings, "font", font);
	obs_data_set_bool(settings, "outline", true);

#ifdef _WIN32
	obs_data_set_int(settings, "outline_color", 0x000000);
	obs_data_set_int(settings, "outline_size", 3);
	const char *text_source_id = "text_gdiplus";
#else
	const char *text_source_id = "text_ft2_source";
#endif

	struct dstr name;
	dstr_init(&name);
	dstr_printf(&name, "Aitum Vertical Preview spacing label %d", i);
	OBSSource txtSource = obs_source_create_private(text_source_id, name.array, settings);
	dstr_free(&name);
	return txtSource;
}

obs_scene_item *CanvasDock::GetSelectedItem(obs_scene_t *s)
{
	vec2 pos;
	SceneFindBoxData sfbd(pos, pos);

	if (!s)
		s = this->scene;
	obs_scene_enum_items(s, FindSelected, &sfbd);

	if (sfbd.sceneItems.size() != 1)
		return nullptr;

	return sfbd.sceneItems.at(0);
}

void CanvasDock::DrawSpacingHelpers(obs_scene_t *s, float x, float y, float cx, float cy, float scale, float sourceX, float sourceY)
{
	UNUSED_PARAMETER(x);
	UNUSED_PARAMETER(y);
	if (locked)
		return;

	OBSSceneItem item = GetSelectedItem();
	if (!item)
		return;

	if (obs_sceneitem_locked(item))
		return;

	vec2 itemSize = GetItemSize(item);
	if (itemSize.x == 0.0f || itemSize.y == 0.0f)
		return;

	obs_sceneitem_t *parentGroup = obs_sceneitem_get_group(s, item);

	if (parentGroup && obs_sceneitem_locked(parentGroup))
		return;

	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	vec3 size;
	vec3_set(&size, sourceX, sourceY, 1.0f);

	// Init box transform side locations
	vec3 left, right, top, bottom;

	vec3_set(&left, 0.0f, 0.5f, 1.0f);
	vec3_set(&right, 1.0f, 0.5f, 1.0f);
	vec3_set(&top, 0.5f, 0.0f, 1.0f);
	vec3_set(&bottom, 0.5f, 1.0f, 1.0f);

	// Decide which side to use with box transform, based on rotation
	// Seems hacky, probably a better way to do it
	float rot = obs_sceneitem_get_rot(item);

	if (parentGroup) {

		//Correct the scene item rotation angle
		rot += obs_sceneitem_get_rot(parentGroup);

		vec2 group_scale;
		obs_sceneitem_get_scale(parentGroup, &group_scale);

		vec2 group_pos;
		obs_sceneitem_get_pos(parentGroup, &group_pos);

		// Correct the scene item box transform
		// Based on scale, rotation angle, position of parent's group
		matrix4_scale3f(&boxTransform, &boxTransform, group_scale.x, group_scale.y, 1.0f);
		matrix4_rotate_aa4f(&boxTransform, &boxTransform, 0.0f, 0.0f, 1.0f, RAD(obs_sceneitem_get_rot(parentGroup)));
		matrix4_translate3f(&boxTransform, &boxTransform, group_pos.x, group_pos.y, 0.0f);
	}

	if (rot >= HELPER_ROT_BREAKPONT) {
		for (float i = HELPER_ROT_BREAKPONT; i <= 360.0f; i += 90.0f) {
			if (rot < i)
				break;

			vec3 l = left;
			vec3 r = right;
			vec3 t = top;
			vec3 b = bottom;

			vec3_copy(&top, &l);
			vec3_copy(&right, &t);
			vec3_copy(&bottom, &r);
			vec3_copy(&left, &b);
		}
	} else if (rot <= -HELPER_ROT_BREAKPONT) {
		for (float i = -HELPER_ROT_BREAKPONT; i >= -360.0f; i -= 90.0f) {
			if (rot > i)
				break;

			vec3 l = left;
			vec3 r = right;
			vec3 t = top;
			vec3 b = bottom;

			vec3_copy(&top, &r);
			vec3_copy(&right, &b);
			vec3_copy(&bottom, &l);
			vec3_copy(&left, &t);
		}
	}
	vec2 item_scale;
	obs_sceneitem_get_scale(item, &item_scale);

	// Switch top/bottom or right/left if scale is negative
	if (item_scale.x < 0.0f) {
		vec3 l = left;
		vec3 r = right;

		vec3_copy(&left, &r);
		vec3_copy(&right, &l);
	}

	if (item_scale.y < 0.0f) {
		vec3 t = top;
		vec3 b = bottom;

		vec3_copy(&top, &b);
		vec3_copy(&bottom, &t);
	}

	// Get sides of box transform
	left = GetTransformedPos(left.x, left.y, boxTransform);
	right = GetTransformedPos(right.x, right.y, boxTransform);
	top = GetTransformedPos(top.x, top.y, boxTransform);
	bottom = GetTransformedPos(bottom.x, bottom.y, boxTransform);

	bottom.y = size.y - bottom.y;
	right.x = size.x - right.x;

	// Init viewport
	vec3 viewport;
	vec3_set(&viewport, cx, cy, 1.0f);

	vec3_div(&left, &left, &viewport);
	vec3_div(&right, &right, &viewport);
	vec3_div(&top, &top, &viewport);
	vec3_div(&bottom, &bottom, &viewport);

	vec3_mulf(&left, &left, scale);
	vec3_mulf(&right, &right, scale);
	vec3_mulf(&top, &top, scale);
	vec3_mulf(&bottom, &bottom, scale);

	// Draw spacer lines and labels
	vec3 start, end;

	float pixelRatio = 1.0f; //main->GetDevicePixelRatio();
	if (!spacerLabel[3]) {
		QMetaObject::invokeMethod(this, [this, pixelRatio]() {
			for (int i = 0; i < 4; i++) {
				if (!spacerLabel[i])
					spacerLabel[i] = CreateLabel(pixelRatio, i);
			}
		});
		return;
	}

	vec3_set(&start, top.x, 0.0f, 1.0f);
	vec3_set(&end, top.x, top.y, 1.0f);
	RenderSpacingHelper(0, start, end, viewport, pixelRatio);

	vec3_set(&start, bottom.x, 1.0f - bottom.y, 1.0f);
	vec3_set(&end, bottom.x, 1.0f, 1.0f);
	RenderSpacingHelper(1, start, end, viewport, pixelRatio);

	vec3_set(&start, 0.0f, left.y, 1.0f);
	vec3_set(&end, left.x, left.y, 1.0f);
	RenderSpacingHelper(2, start, end, viewport, pixelRatio);

	vec3_set(&start, 1.0f - right.x, right.y, 1.0f);
	vec3_set(&end, 1.0f, right.y, 1.0f);
	RenderSpacingHelper(3, start, end, viewport, pixelRatio);
}

static inline bool crop_enabled(const obs_sceneitem_crop *crop)
{
	return crop->left > 0 || crop->top > 0 || crop->right > 0 || crop->bottom > 0;
}

static void DrawSquareAtPos(float x, float y, float pixelRatio)
{
	struct vec3 pos;
	vec3_set(&pos, x, y, 0.0f);

	struct matrix4 matrix;
	gs_matrix_get(&matrix);
	vec3_transform(&pos, &pos, &matrix);

	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_translate(&pos);

	gs_matrix_translate3f(-HANDLE_RADIUS * pixelRatio, -HANDLE_RADIUS * pixelRatio, 0.0f);
	gs_matrix_scale3f(HANDLE_RADIUS * pixelRatio * 2, HANDLE_RADIUS * pixelRatio * 2, 1.0f);
	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_pop();
}

static void DrawRotationHandle(gs_vertbuffer_t *circle, float rot, float pixelRatio)
{
	struct vec3 pos;
	vec3_set(&pos, 0.5f, 0.0f, 0.0f);

	struct matrix4 matrix;
	gs_matrix_get(&matrix);
	vec3_transform(&pos, &pos, &matrix);

	gs_render_start(true);

	gs_vertex2f(0.5f - 0.34f / HANDLE_RADIUS, 0.5f);
	gs_vertex2f(0.5f - 0.34f / HANDLE_RADIUS, -2.0f);
	gs_vertex2f(0.5f + 0.34f / HANDLE_RADIUS, -2.0f);
	gs_vertex2f(0.5f + 0.34f / HANDLE_RADIUS, 0.5f);
	gs_vertex2f(0.5f - 0.34f / HANDLE_RADIUS, 0.5f);

	gs_vertbuffer_t *line = gs_render_save();

	gs_load_vertexbuffer(line);

	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_translate(&pos);

	gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, RAD(rot));
	gs_matrix_translate3f(-HANDLE_RADIUS * 1.5f * pixelRatio, -HANDLE_RADIUS * 1.5f * pixelRatio, 0.0f);
	gs_matrix_scale3f(HANDLE_RADIUS * 3 * pixelRatio, HANDLE_RADIUS * 3 * pixelRatio, 1.0f);

	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_translate3f(0.0f, -HANDLE_RADIUS * 2 / 3, 0.0f);

	gs_load_vertexbuffer(circle);
	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_pop();
	gs_vertexbuffer_destroy(line);
}

static void DrawStripedLine(float x1, float y1, float x2, float y2, float thickness, vec2 scale)
{
	float ySide = (y1 == y2) ? (y1 < 0.5f ? 1.0f : -1.0f) : 0.0f;
	float xSide = (x1 == x2) ? (x1 < 0.5f ? 1.0f : -1.0f) : 0.0f;

	float dist = sqrtf(powf((x1 - x2) * scale.x, 2.0f) + powf((y1 - y2) * scale.y, 2.0f));
	if (dist > 1000000.0f) {
		// too many stripes to draw, draw it as a line as fallback
		DrawLine(x1, y1, x2, y2, thickness, scale);
		return;
	}
	float offX = (x2 - x1) / dist;
	float offY = (y2 - y1) / dist;

	for (int i = 0, l = (int)ceil(dist / 15.0); i < l; i++) {
		gs_render_start(true);

		float xx1 = x1 + (float)i * 15.0f * offX;
		float yy1 = y1 + (float)i * 15.0f * offY;

		float dx;
		float dy;

		if (x1 < x2) {
			dx = std::min(xx1 + 7.5f * offX, x2);
		} else {
			dx = std::max(xx1 + 7.5f * offX, x2);
		}

		if (y1 < y2) {
			dy = std::min(yy1 + 7.5f * offY, y2);
		} else {
			dy = std::max(yy1 + 7.5f * offY, y2);
		}

		gs_vertex2f(xx1, yy1);
		gs_vertex2f(xx1 + (xSide * (thickness / scale.x)), yy1 + (ySide * (thickness / scale.y)));
		gs_vertex2f(dx, dy);
		gs_vertex2f(dx + (xSide * (thickness / scale.x)), dy + (ySide * (thickness / scale.y)));

		gs_vertbuffer_t *line = gs_render_save();

		gs_load_vertexbuffer(line);
		gs_draw(GS_TRISTRIP, 0, 0);
		gs_vertexbuffer_destroy(line);
	}
}

static void DrawRect(float thickness, vec2 scale)
{
	if (scale.x <= 0.0f || scale.y <= 0.0f || thickness <= 0.0f) {
		return;
	}
	gs_render_start(true);

	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(0.0f + (thickness / scale.x), 0.0f);
	gs_vertex2f(0.0f, 1.0f);
	gs_vertex2f(0.0f + (thickness / scale.x), 1.0f);
	gs_vertex2f(0.0f, 1.0f - (thickness / scale.y));
	gs_vertex2f(1.0f, 1.0f);
	gs_vertex2f(1.0f, 1.0f - (thickness / scale.y));
	gs_vertex2f(1.0f - (thickness / scale.x), 1.0f);
	gs_vertex2f(1.0f, 0.0f);
	gs_vertex2f(1.0f - (thickness / scale.x), 0.0f);
	gs_vertex2f(1.0f, 0.0f + (thickness / scale.y));
	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(0.0f, 0.0f + (thickness / scale.y));

	gs_vertbuffer_t *rect = gs_render_save();

	gs_load_vertexbuffer(rect);
	gs_draw(GS_TRISTRIP, 0, 0);
	gs_vertexbuffer_destroy(rect);
}

bool CanvasDock::DrawSelectedItem(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	if (obs_sceneitem_locked(item))
		return true;

	if (!SceneItemHasVideo(item))
		return true;

	CanvasDock *window = static_cast<CanvasDock *>(param);

	if (obs_sceneitem_is_group(item)) {
		matrix4 mat;
		obs_sceneitem_get_draw_transform(item, &mat);

		window->groupRot = obs_sceneitem_get_rot(item);

		gs_matrix_push();
		gs_matrix_mul(&mat);
		obs_sceneitem_group_enum_items(item, DrawSelectedItem, param);
		gs_matrix_pop();

		window->groupRot = 0.0f;
	}

	float pixelRatio = window->GetDevicePixelRatio();

	bool hovered = false;
	{
		std::lock_guard<std::mutex> lock(window->selectMutex);
		for (size_t i = 0; i < window->hoveredPreviewItems.size(); i++) {
			if (window->hoveredPreviewItems[i] == item) {
				hovered = true;
				break;
			}
		}
	}

	bool selected = obs_sceneitem_selected(item);

	if (!selected && !hovered)
		return true;

	matrix4 boxTransform;
	matrix4 invBoxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);
	matrix4_inv(&invBoxTransform, &boxTransform);

	vec3 bounds[] = {
		{{{0.f, 0.f, 0.f}}},
		{{{1.f, 0.f, 0.f}}},
		{{{0.f, 1.f, 0.f}}},
		{{{1.f, 1.f, 0.f}}},
	};

	//main->GetCameraIcon();

	QColor selColor = window->GetSelectionColor();
	QColor cropColor = window->GetCropColor();
	QColor hoverColor = window->GetHoverColor();

	vec4 red;
	vec4 green;
	vec4 blue;

	vec4_set(&red, selColor.redF(), selColor.greenF(), selColor.blueF(), 1.0f);
	vec4_set(&green, cropColor.redF(), cropColor.greenF(), cropColor.blueF(), 1.0f);
	vec4_set(&blue, hoverColor.redF(), hoverColor.greenF(), hoverColor.blueF(), 1.0f);

	bool visible = std::all_of(std::begin(bounds), std::end(bounds), [&](const vec3 &b) {
		vec3 pos;
		vec3_transform(&pos, &b, &boxTransform);
		vec3_transform(&pos, &pos, &invBoxTransform);
		return CloseFloat(pos.x, b.x) && CloseFloat(pos.y, b.y);
	});

	if (!visible)
		return true;

	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "DrawSelectedItem");

	matrix4 curTransform;
	vec2 boxScale;
	gs_matrix_get(&curTransform);
	obs_sceneitem_get_box_scale(item, &boxScale);
	boxScale.x *= curTransform.x.x;
	boxScale.y *= curTransform.y.y;

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);

	gs_effect_t *eff = gs_get_effect();
	gs_eparam_t *colParam = gs_effect_get_param_by_name(eff, "color");

	gs_effect_set_vec4(colParam, &red);

	if (obs_sceneitem_get_bounds_type(item) == OBS_BOUNDS_NONE && crop_enabled(&crop)) {
#define DRAW_SIDE(side, x1, y1, x2, y2)                                                   \
	if (hovered && !selected) {                                                       \
		gs_effect_set_vec4(colParam, &blue);                                      \
		DrawLine(x1, y1, x2, y2, HANDLE_RADIUS *pixelRatio / 2, boxScale);        \
	} else if (crop.side > 0) {                                                       \
		gs_effect_set_vec4(colParam, &green);                                     \
		DrawStripedLine(x1, y1, x2, y2, HANDLE_RADIUS *pixelRatio / 2, boxScale); \
	} else {                                                                          \
		DrawLine(x1, y1, x2, y2, HANDLE_RADIUS *pixelRatio / 2, boxScale);        \
	}                                                                                 \
	gs_effect_set_vec4(colParam, &red);

		DRAW_SIDE(left, 0.0f, 0.0f, 0.0f, 1.0f);
		DRAW_SIDE(top, 0.0f, 0.0f, 1.0f, 0.0f);
		DRAW_SIDE(right, 1.0f, 0.0f, 1.0f, 1.0f);
		DRAW_SIDE(bottom, 0.0f, 1.0f, 1.0f, 1.0f);
#undef DRAW_SIDE
	} else {
		if (!selected) {
			gs_effect_set_vec4(colParam, &blue);
			DrawRect(HANDLE_RADIUS * pixelRatio / 2, boxScale);
		} else {
			DrawRect(HANDLE_RADIUS * pixelRatio / 2, boxScale);
		}
	}

	gs_load_vertexbuffer(window->box);
	gs_effect_set_vec4(colParam, &red);

	if (selected) {
		DrawSquareAtPos(0.0f, 0.0f, pixelRatio);
		DrawSquareAtPos(0.0f, 1.0f, pixelRatio);
		DrawSquareAtPos(1.0f, 0.0f, pixelRatio);
		DrawSquareAtPos(1.0f, 1.0f, pixelRatio);
		DrawSquareAtPos(0.5f, 0.0f, pixelRatio);
		DrawSquareAtPos(0.0f, 0.5f, pixelRatio);
		DrawSquareAtPos(0.5f, 1.0f, pixelRatio);
		DrawSquareAtPos(1.0f, 0.5f, pixelRatio);

		if (!window->circleFill) {
			gs_render_start(true);

			float angle = 180;
			for (int i = 0, l = 40; i < l; i++) {
				gs_vertex2f(sin(RAD(angle)) / 2 + 0.5f, cos(RAD(angle)) / 2 + 0.5f);
				angle += 360.0f / (float)l;
				gs_vertex2f(sin(RAD(angle)) / 2 + 0.5f, cos(RAD(angle)) / 2 + 0.5f);
				gs_vertex2f(0.5f, 1.0f);
			}

			window->circleFill = gs_render_save();
		}

		DrawRotationHandle(window->circleFill, obs_sceneitem_get_rot(item) + window->groupRot, pixelRatio);
	}

	gs_matrix_pop();

	GS_DEBUG_MARKER_END();

	UNUSED_PARAMETER(scene);
	return true;
}

static inline QColor color_from_int(long long val)
{
	return QColor(val & 0xff, (val >> 8) & 0xff, (val >> 16) & 0xff, (val >> 24) & 0xff);
}

QColor CanvasDock::GetSelectionColor() const
{
	auto config = get_user_config();
	if (config && config_get_bool(config, "Accessibility", "OverrideColors")) {
		return color_from_int(config_get_int(config, "Accessibility", "SelectRed"));
	}
	return QColor::fromRgb(255, 0, 0);
}

QColor CanvasDock::GetCropColor() const
{
	auto config = get_user_config();
	if (config && config_get_bool(config, "Accessibility", "OverrideColors")) {
		return color_from_int(config_get_int(config, "Accessibility", "SelectGreen"));
	}
	return QColor::fromRgb(0, 255, 0);
}

QColor CanvasDock::GetHoverColor() const
{
	auto config = get_user_config();
	if (config && config_get_bool(config, "Accessibility", "OverrideColors")) {
		return color_from_int(config_get_int(config, "Accessibility", "SelectBlue"));
	}
	return QColor::fromRgb(0, 127, 255);
}

OBSEventFilter *CanvasDock::BuildEventFilter()
{
	return new OBSEventFilter([this](QObject *obj, QEvent *event) {
		UNUSED_PARAMETER(obj);

		if (!scene)
			return false;
		switch (event->type()) {
		case QEvent::MouseButtonPress:
			return this->HandleMousePressEvent(static_cast<QMouseEvent *>(event));
		case QEvent::MouseButtonRelease:
			return this->HandleMouseReleaseEvent(static_cast<QMouseEvent *>(event));
		//case QEvent::MouseButtonDblClick:			return this->HandleMouseClickEvent(				static_cast<QMouseEvent *>(event));
		case QEvent::MouseMove:
			return this->HandleMouseMoveEvent(static_cast<QMouseEvent *>(event));
		//case QEvent::Enter:
		case QEvent::Leave:
			return this->HandleMouseLeaveEvent(static_cast<QMouseEvent *>(event));
		case QEvent::Wheel:
			return this->HandleMouseWheelEvent(static_cast<QWheelEvent *>(event));
		//case QEvent::FocusIn:
		//case QEvent::FocusOut:
		case QEvent::KeyPress:
			return this->HandleKeyPressEvent(static_cast<QKeyEvent *>(event));
		case QEvent::KeyRelease:
			return this->HandleKeyReleaseEvent(static_cast<QKeyEvent *>(event));
		default:
			return false;
		}
	});
}

bool CanvasDock::GetSourceRelativeXY(int mouseX, int mouseY, int &relX, int &relY)
{
	float pixelRatio = (float)devicePixelRatioF();

	int mouseXscaled = (int)roundf((float)mouseX * pixelRatio);
	int mouseYscaled = (int)roundf((float)mouseY * pixelRatio);

	QSize size = preview->size() * preview->devicePixelRatioF();

	obs_source_t *s = obs_weak_source_get_source(source);
	uint32_t sourceCX = s ? obs_source_get_width(s) : 1;
	if (sourceCX <= 0)
		sourceCX = 1;
	uint32_t sourceCY = s ? obs_source_get_height(s) : 1;
	if (sourceCY <= 0)
		sourceCY = 1;

	obs_source_release(s);

	int x, y;
	float scale;

	GetScaleAndCenterPos(sourceCX, sourceCY, size.width(), size.height(), x, y, scale);

	auto newCX = scale * float(sourceCX);
	auto newCY = scale * float(sourceCY);

	auto extraCx = /*(zoom - 1.0f) **/ newCX;
	auto extraCy = /*(zoom - 1.0f) **/ newCY;

	//scale *= zoom;
	float scrollX = 0.5f;
	float scrollY = 0.5f;

	if (x > 0) {
		relX = int(((float)mouseXscaled - (float)x + extraCx * scrollX) / scale);
		relY = int(((float)mouseYscaled + extraCy * scrollY) / scale);
	} else {
		relX = int(((float)mouseXscaled + extraCx * scrollX) / scale);
		relY = int(((float)mouseYscaled - (float)y + extraCy * scrollY) / scale);
	}

	// Confirm mouse is inside the source
	if (relX < 0 || relX > int(sourceCX))
		return false;
	if (relY < 0 || relY > int(sourceCY))
		return false;

	return true;
}

bool CanvasDock::HandleMousePressEvent(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	QPointF pos = event->position();
#else
	QPointF pos = event->localPos();
#endif

	if (scrollMode && IsFixedScaling() && event->button() == Qt::LeftButton) {
		setCursor(Qt::ClosedHandCursor);
		scrollingFrom.x = (float)pos.x();
		scrollingFrom.y = (float)pos.y();
		return true;
	}

	if (event->button() == Qt::RightButton) {
		scrollMode = false;
		setCursor(Qt::ArrowCursor);
	}

	if (locked)
		return false;

	//float pixelRatio = 1.0f;
	//float x = pos.x() - main->previewX / pixelRatio;
	//float y = pos.y() - main->previewY / pixelRatio;
	Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
	bool altDown = (modifiers & Qt::AltModifier);
	bool shiftDown = (modifiers & Qt::ShiftModifier);
	bool ctrlDown = (modifiers & Qt::ControlModifier);

	if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)
		return false;

	if (event->button() == Qt::LeftButton)
		mouseDown = true;

	{
		std::lock_guard<std::mutex> lock(selectMutex);
		selectedItems.clear();
	}

	if (altDown)
		cropping = true;

	if (altDown || shiftDown || ctrlDown) {
		vec2 s;
		SceneFindBoxData sfbd(s, s);

		obs_scene_enum_items(scene, FindSelected, &sfbd);

		std::lock_guard<std::mutex> lock(selectMutex);
		selectedItems = sfbd.sceneItems;
	}
	startPos = GetMouseEventPos(event);

	//vec2_set(&startPos, mouseEvent.x, mouseEvent.y);
	//GetStretchHandleData(startPos, false);

	//vec2_divf(&startPos, &startPos, main->previewScale / pixelRatio);
	startPos.x = std::round(startPos.x);
	startPos.y = std::round(startPos.y);

	mouseOverItems = SelectedAtPos(scene, startPos);
	vec2_zero(&lastMoveOffset);

	mousePos = startPos;

	return true;
}

static void GetItemBox(obs_sceneitem_t *item, vec3 &tl, vec3 &br)
{
	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	vec3_set(&tl, M_INFINITE, M_INFINITE, 0.0f);
	vec3_set(&br, -M_INFINITE, -M_INFINITE, 0.0f);

	auto GetMinPos = [&](float x, float y) {
		vec3 pos;
		vec3_set(&pos, x, y, 0.0f);
		vec3_transform(&pos, &pos, &boxTransform);
		vec3_min(&tl, &tl, &pos);
		vec3_max(&br, &br, &pos);
	};

	GetMinPos(0.0f, 0.0f);
	GetMinPos(1.0f, 0.0f);
	GetMinPos(0.0f, 1.0f);
	GetMinPos(1.0f, 1.0f);
}

static vec3 GetItemTL(obs_sceneitem_t *item)
{
	vec3 tl, br;
	GetItemBox(item, tl, br);
	return tl;
}

static void SetItemTL(obs_sceneitem_t *item, const vec3 &tl)
{
	vec3 newTL;
	vec2 pos;

	obs_sceneitem_get_pos(item, &pos);
	newTL = GetItemTL(item);
	pos.x += tl.x - newTL.x;
	pos.y += tl.y - newTL.y;
	obs_sceneitem_set_pos(item, &pos);
}

static bool RotateSelectedSources(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, RotateSelectedSources, param);
	if (!obs_sceneitem_selected(item))
		return true;
	if (obs_sceneitem_locked(item))
		return true;

	float rot = *reinterpret_cast<float *>(param);

	vec3 tl = GetItemTL(item);

	rot += obs_sceneitem_get_rot(item);
	if (rot >= 360.0f)
		rot -= 360.0f;
	else if (rot <= -360.0f)
		rot += 360.0f;
	obs_sceneitem_set_rot(item, rot);

	obs_sceneitem_force_update_transform(item);

	SetItemTL(item, tl);

	UNUSED_PARAMETER(scene);
	return true;
};

static bool MultiplySelectedItemScale(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	vec2 &mul = *reinterpret_cast<vec2 *>(param);

	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, MultiplySelectedItemScale, param);
	if (!obs_sceneitem_selected(item))
		return true;
	if (obs_sceneitem_locked(item))
		return true;

	vec3 tl = GetItemTL(item);

	vec2 scale;
	obs_sceneitem_get_scale(item, &scale);
	vec2_mul(&scale, &scale, &mul);
	obs_sceneitem_set_scale(item, &scale);

	obs_sceneitem_force_update_transform(item);

	SetItemTL(item, tl);

	UNUSED_PARAMETER(scene);
	return true;
}

static bool CenterAlignSelectedItems(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	obs_bounds_type boundsType = *reinterpret_cast<obs_bounds_type *>(param);

	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, CenterAlignSelectedItems, param);
	if (!obs_sceneitem_selected(item))
		return true;
	if (obs_sceneitem_locked(item))
		return true;

	obs_source_t *scene_source = obs_scene_get_source(scene);

	obs_transform_info itemInfo;
	vec2_set(&itemInfo.pos, 0.0f, 0.0f);
	vec2_set(&itemInfo.scale, 1.0f, 1.0f);
	itemInfo.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	itemInfo.rot = 0.0f;

	vec2_set(&itemInfo.bounds, float(obs_source_get_base_width(scene_source)), float(obs_source_get_base_height(scene_source)));
	itemInfo.bounds_type = boundsType;
	itemInfo.bounds_alignment = OBS_ALIGN_CENTER;
	itemInfo.crop_to_bounds = obs_sceneitem_get_bounds_crop(item);
	obs_sceneitem_set_info2(item, &itemInfo);

	UNUSED_PARAMETER(scene);
	return true;
}

static bool GetSelectedItemsWithSize(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	auto items = static_cast<std::vector<obs_sceneitem_t *> *>(param);

	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, GetSelectedItemsWithSize, param);
	if (!obs_sceneitem_selected(item))
		return true;
	if (obs_sceneitem_locked(item))
		return true;

	vec2 scale;
	obs_sceneitem_get_scale(item, &scale);

	obs_source_t *source = obs_sceneitem_get_source(item);
	const float width = float(obs_source_get_width(source)) * scale.x;
	const float height = float(obs_source_get_height(source)) * scale.y;

	if (width == 0.0f || height == 0.0f)
		return true;

	items->push_back(item);

	UNUSED_PARAMETER(scene);
	return true;
}

void CanvasDock::CenterSelectedItems(CenterType centerType)
{
	std::vector<obs_sceneitem_t *> items;
	obs_scene_enum_items(scene, GetSelectedItemsWithSize, &items);
	if (!items.size())
		return;

	// Get center x, y coordinates of items
	vec3 center;

	float top = M_INFINITE;
	float left = M_INFINITE;
	float right = 0.0f;
	float bottom = 0.0f;

	for (auto &item : items) {
		vec3 tl, br;

		GetItemBox(item, tl, br);

		left = (std::min)(tl.x, left);
		top = (std::min)(tl.y, top);
		right = (std::max)(br.x, right);
		bottom = (std::max)(br.y, bottom);
	}

	center.x = (right + left) / 2.0f;
	center.y = (top + bottom) / 2.0f;
	center.z = 0.0f;

	// Get coordinates of screen center
	vec3 screenCenter;
	vec3_set(&screenCenter, float(canvas_width), float(canvas_height), 0.0f);

	vec3_mulf(&screenCenter, &screenCenter, 0.5f);

	// Calculate difference between screen center and item center
	vec3 offset;
	vec3_sub(&offset, &screenCenter, &center);

	// Shift items by offset
	for (auto &item : items) {
		vec3 tl, br;

		GetItemBox(item, tl, br);

		vec3_add(&tl, &tl, &offset);

		vec3 itemTL = GetItemTL(item);

		if (centerType == CenterType::Vertical)
			tl.x = itemTL.x;
		else if (centerType == CenterType::Horizontal)
			tl.y = itemTL.y;

		SetItemTL(item, tl);
	}
}

void CanvasDock::AddSceneItemMenuItems(QMenu *popup, OBSSceneItem sceneItem)
{

	popup->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Rename")), [this, sceneItem] {
		obs_source_t *item_source = obs_source_get_ref(obs_sceneitem_get_source(sceneItem));
		if (!item_source)
			return;
		obs_canvas_t *canvas = (obs_source_get_output_flags(item_source) & OBS_SOURCE_REQUIRES_CANVAS)
					       ? obs_source_get_canvas(item_source)
					       : nullptr;
		std::string name = obs_source_get_name(item_source);
		obs_source_t *s = nullptr;
		do {
			obs_source_release(s);
			if (!NameDialog::AskForName(this, QString::fromUtf8(obs_module_text("SourceName")), name)) {
				break;
			}
			s = canvas ? obs_canvas_get_source_by_name(canvas, name.c_str()) : obs_get_source_by_name(name.c_str());
			if (s)
				continue;
			obs_source_set_name(item_source, name.c_str());
		} while (s);
		obs_canvas_release(canvas);
		obs_source_release(item_source);
	});
	popup->addAction(
		//removeButton->icon(),
		QString::fromUtf8(obs_frontend_get_locale_string("Remove")), this, [sceneItem] {
			QMessageBox mb(QMessageBox::Question,
				       QString::fromUtf8(obs_frontend_get_locale_string("ConfirmRemove.Title")),
				       QString::fromUtf8(obs_frontend_get_locale_string("ConfirmRemove.Text"))
					       .arg(QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(sceneItem)))),
				       QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No));
			mb.setDefaultButton(QMessageBox::NoButton);
			if (mb.exec() == QMessageBox::Yes) {
				obs_sceneitem_remove(sceneItem);
			}
		});

	popup->addSeparator();
	auto orderMenu = popup->addMenu(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Order")));
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Order.MoveUp")), this,
			     [sceneItem] { obs_sceneitem_set_order(sceneItem, OBS_ORDER_MOVE_UP); });
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Order.MoveDown")), this,
			     [sceneItem] { obs_sceneitem_set_order(sceneItem, OBS_ORDER_MOVE_DOWN); });
	orderMenu->addSeparator();
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Order.MoveToTop")), this,
			     [sceneItem] { obs_sceneitem_set_order(sceneItem, OBS_ORDER_MOVE_TOP); });
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Order.MoveToBottom")), this,
			     [sceneItem] { obs_sceneitem_set_order(sceneItem, OBS_ORDER_MOVE_BOTTOM); });

	auto transformMenu = popup->addMenu(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform")));
	transformMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.EditTransform")),
				 [this, sceneItem] {
					 const auto mainDialog = static_cast<QMainWindow *>(obs_frontend_get_main_window());
					 auto transformDialog = mainDialog->findChild<QDialog *>("OBSBasicTransform");
					 if (!transformDialog) {
						 // make sure there is an item selected on the main canvas before starting the transform dialog
						 const auto currentScene = obs_frontend_preview_program_mode_active()
										   ? obs_frontend_get_current_preview_scene()
										   : obs_frontend_get_current_scene();
						 auto selected = GetSelectedItem(obs_scene_from_source(currentScene));
						 if (!selected) {
							 obs_scene_enum_items(
								 obs_scene_from_source(currentScene),
								 [](obs_scene_t *, obs_sceneitem_t *item, void *) {
									 obs_sceneitem_select(item, true);
									 return false;
								 },
								 nullptr);
						 }
						 obs_source_release(currentScene);
						 QMetaObject::invokeMethod(mainDialog, "on_actionEditTransform_triggered");
						 transformDialog = mainDialog->findChild<QDialog *>("OBSBasicTransform");
					 }
					 if (!transformDialog)
						 return;
					 QMetaObject::invokeMethod(transformDialog, "SetItemQt",
								   Q_ARG(OBSSceneItem, OBSSceneItem(sceneItem)));
				 });
	transformMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.ResetTransform")),
				 this, [sceneItem] {
					 obs_sceneitem_set_alignment(sceneItem, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
					 obs_sceneitem_set_bounds_type(sceneItem, OBS_BOUNDS_NONE);
					 vec2 scale;
					 scale.x = 1.0f;
					 scale.y = 1.0f;
					 obs_sceneitem_set_scale(sceneItem, &scale);
					 vec2 pos;
					 pos.x = 0.0f;
					 pos.y = 0.0f;
					 obs_sceneitem_set_pos(sceneItem, &pos);
					 obs_sceneitem_crop crop = {0, 0, 0, 0};
					 obs_sceneitem_set_crop(sceneItem, &crop);
					 obs_sceneitem_set_rot(sceneItem, 0.0f);
				 });
	transformMenu->addSeparator();
	transformMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.Rotate90CW")),
				 this, [this] {
					 float rotation = 90.0f;
					 obs_scene_enum_items(scene, RotateSelectedSources, &rotation);
				 });
	transformMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.Rotate90CCW")),
				 this, [this] {
					 float rotation = -90.0f;
					 obs_scene_enum_items(scene, RotateSelectedSources, &rotation);
				 });
	transformMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.Rotate180")), this,
				 [this] {
					 float rotation = 180.0f;
					 obs_scene_enum_items(scene, RotateSelectedSources, &rotation);
				 });
	transformMenu->addSeparator();
	transformMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.FlipHorizontal")),
				 this, [this] {
					 vec2 scale;
					 vec2_set(&scale, -1.0f, 1.0f);
					 obs_scene_enum_items(scene, MultiplySelectedItemScale, &scale);
				 });
	transformMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.FlipVertical")),
				 this, [this] {
					 vec2 scale;
					 vec2_set(&scale, 1.0f, -1.0f);
					 obs_scene_enum_items(scene, MultiplySelectedItemScale, &scale);
				 });
	transformMenu->addSeparator();
	transformMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.FitToScreen")),
				 this, [this] {
					 obs_bounds_type boundsType = OBS_BOUNDS_SCALE_INNER;
					 obs_scene_enum_items(scene, CenterAlignSelectedItems, &boundsType);
				 });
	transformMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.StretchToScreen")),
				 this, [this] {
					 obs_bounds_type boundsType = OBS_BOUNDS_STRETCH;
					 obs_scene_enum_items(scene, CenterAlignSelectedItems, &boundsType);
				 });
	transformMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.CenterToScreen")),
				 this, [this] { CenterSelectedItems(CenterType::Scene); });
	transformMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.VerticalCenter")),
				 this, [this] { CenterSelectedItems(CenterType::Vertical); });
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.Transform.HorizontalCenter")), this,
		[this] { CenterSelectedItems(CenterType::Horizontal); });

	popup->addSeparator();

	obs_scale_type scaleFilter = obs_sceneitem_get_scale_filter(sceneItem);
	auto scaleMenu = popup->addMenu(QString::fromUtf8(obs_frontend_get_locale_string("ScaleFiltering")));
	auto a = scaleMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Disable")), this,
				      [sceneItem] { obs_sceneitem_set_scale_filter(sceneItem, OBS_SCALE_DISABLE); });
	a->setCheckable(true);
	a->setChecked(scaleFilter == OBS_SCALE_DISABLE);
	a = scaleMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("ScaleFiltering.Point")), this,
				 [sceneItem] { obs_sceneitem_set_scale_filter(sceneItem, OBS_SCALE_POINT); });
	a->setCheckable(true);
	a->setChecked(scaleFilter == OBS_SCALE_POINT);
	a = scaleMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("ScaleFiltering.Bilinear")), this,
				 [sceneItem] { obs_sceneitem_set_scale_filter(sceneItem, OBS_SCALE_BILINEAR); });
	a->setCheckable(true);
	a->setChecked(scaleFilter == OBS_SCALE_BILINEAR);
	a = scaleMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("ScaleFiltering.Bicubic")), this,
				 [sceneItem] { obs_sceneitem_set_scale_filter(sceneItem, OBS_SCALE_BICUBIC); });
	a->setCheckable(true);
	a->setChecked(scaleFilter == OBS_SCALE_BICUBIC);
	a = scaleMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("ScaleFiltering.Lanczos")), this,
				 [sceneItem] { obs_sceneitem_set_scale_filter(sceneItem, OBS_SCALE_LANCZOS); });
	a->setCheckable(true);
	a->setChecked(scaleFilter == OBS_SCALE_LANCZOS);
	a = scaleMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("ScaleFiltering.Area")), this,
				 [sceneItem] { obs_sceneitem_set_scale_filter(sceneItem, OBS_SCALE_AREA); });
	a->setCheckable(true);
	a->setChecked(scaleFilter == OBS_SCALE_AREA);

	auto blendingMode = obs_sceneitem_get_blending_mode(sceneItem);
	auto blendingMenu = popup->addMenu(QString::fromUtf8(obs_frontend_get_locale_string("BlendingMode")));
	a = blendingMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("BlendingMode.Normal")), this,
				    [sceneItem] { obs_sceneitem_set_blending_mode(sceneItem, OBS_BLEND_NORMAL); });
	a->setCheckable(true);
	a->setChecked(blendingMode == OBS_BLEND_NORMAL);

	a = blendingMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("BlendingMode.Additive")), this,
				    [sceneItem] { obs_sceneitem_set_blending_mode(sceneItem, OBS_BLEND_ADDITIVE); });
	a->setCheckable(true);
	a->setChecked(blendingMode == OBS_BLEND_ADDITIVE);

	a = blendingMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("BlendingMode.Subtract")), this,
				    [sceneItem] { obs_sceneitem_set_blending_mode(sceneItem, OBS_BLEND_SUBTRACT); });
	a->setCheckable(true);
	a->setChecked(blendingMode == OBS_BLEND_SUBTRACT);

	a = blendingMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("BlendingMode.Screen")), this,
				    [sceneItem] { obs_sceneitem_set_blending_mode(sceneItem, OBS_BLEND_SCREEN); });
	a->setCheckable(true);
	a->setChecked(blendingMode == OBS_BLEND_SCREEN);

	a = blendingMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("BlendingMode.Multiply")), this,
				    [sceneItem] { obs_sceneitem_set_blending_mode(sceneItem, OBS_BLEND_MULTIPLY); });
	a->setCheckable(true);
	a->setChecked(blendingMode == OBS_BLEND_MULTIPLY);

	a = blendingMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("BlendingMode.Lighten")), this,
				    [sceneItem] { obs_sceneitem_set_blending_mode(sceneItem, OBS_BLEND_LIGHTEN); });
	a->setCheckable(true);
	a->setChecked(blendingMode == OBS_BLEND_LIGHTEN);

	a = blendingMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("BlendingMode.Darken")), this,
				    [sceneItem] { obs_sceneitem_set_blending_mode(sceneItem, OBS_BLEND_DARKEN); });
	a->setCheckable(true);
	a->setChecked(blendingMode == OBS_BLEND_DARKEN);

	popup->addSeparator();
	popup->addMenu(CreateVisibilityTransitionMenu(true, sceneItem));
	popup->addMenu(CreateVisibilityTransitionMenu(false, sceneItem));

	popup->addSeparator();

	auto projectorMenu = popup->addMenu(QString::fromUtf8(obs_frontend_get_locale_string("SourceProjector")));
	AddProjectorMenuMonitors(projectorMenu, this, SLOT(OpenSourceProjector()));
	popup->addAction(QString::fromUtf8(obs_frontend_get_locale_string("SourceWindow")), [sceneItem] {
		obs_source_t *s = obs_source_get_ref(obs_sceneitem_get_source(sceneItem));
		if (!s)
			return;
		obs_frontend_open_projector("Source", -1, nullptr, obs_source_get_name(s));
		obs_source_release(s);
	});

	obs_source_t *s = obs_sceneitem_get_source(sceneItem);
	popup->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Screenshot.Source")), this,
			 [s] { obs_frontend_take_source_screenshot(s); });
	popup->addSeparator();
	popup->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Filters")), this,
			 [s] { obs_frontend_open_source_filters(s); });
	a = popup->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Properties")), this,
			     [s] { obs_frontend_open_source_properties(s); });
	a->setEnabled(obs_source_configurable(s));
}

bool CanvasDock::HandleMouseReleaseEvent(QMouseEvent *event)
{
	if (scrollMode)
		setCursor(Qt::OpenHandCursor);

	if (!mouseDown && event->button() == Qt::RightButton) {
		QMenu popup(this);
		QAction *a =
			popup.addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.Main.Preview.Disable")), [this] {
				preview_disabled = !preview_disabled;
				obs_display_set_enabled(preview->GetDisplay(), !preview_disabled);
				preview->setVisible(!preview_disabled);
				previewDisabledWidget->setVisible(preview_disabled);
			});
		auto projectorMenu = popup.addMenu(QString::fromUtf8(obs_frontend_get_locale_string("PreviewProjector")));
		AddProjectorMenuMonitors(projectorMenu, this, SLOT(OpenPreviewProjector()));

		a = popup.addAction(QString::fromUtf8(obs_frontend_get_locale_string("PreviewWindow")),
				    [this] { OpenProjector(-1); });

		a = popup.addAction(QString::fromUtf8(obs_frontend_get_locale_string("Basic.MainMenu.Edit.LockPreview")), this,
				    [this] { locked = !locked; });
		a->setCheckable(true);
		a->setChecked(locked);

		popup.addAction(GetIconFromType(OBS_ICON_TYPE_IMAGE),
				QString::fromUtf8(obs_frontend_get_locale_string("Screenshot")), this, [this] {
					auto s = obs_weak_source_get_source(source);
					obs_frontend_take_source_screenshot(s);
					obs_source_release(s);
				});

		popup.addMenu(CreateAddSourcePopupMenu());

		popup.addSeparator();

		OBSSceneItem sceneItem = GetSelectedItem();
		if (sceneItem) {
			AddSceneItemMenuItems(&popup, sceneItem);
		}
		popup.exec(QCursor::pos());
		return true;
	}

	if (locked)
		return false;

	if (!mouseDown)
		return false;

	const vec2 pos = GetMouseEventPos(event);

	if (!mouseMoved)
		ProcessClick(pos);

	if (selectionBox) {
		Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();

		bool altDown = modifiers & Qt::AltModifier;
		bool shiftDown = modifiers & Qt::ShiftModifier;
		bool ctrlDown = modifiers & Qt::ControlModifier;

		std::lock_guard<std::mutex> lock(selectMutex);
		if (altDown || ctrlDown || shiftDown) {
			for (size_t i = 0; i < selectedItems.size(); i++) {
				obs_sceneitem_select(selectedItems[i], true);
			}
		}

		for (size_t i = 0; i < hoveredPreviewItems.size(); i++) {
			bool select = true;
			obs_sceneitem_t *item = hoveredPreviewItems[i];

			if (altDown) {
				select = false;
			} else if (ctrlDown) {
				select = !obs_sceneitem_selected(item);
			}

			obs_sceneitem_select(hoveredPreviewItems[i], select);
		}
	}

	if (stretchGroup) {
		obs_sceneitem_defer_group_resize_end(stretchGroup);
	}

	stretchItem = nullptr;
	stretchGroup = nullptr;
	mouseDown = false;
	mouseMoved = false;
	cropping = false;
	selectionBox = false;
	unsetCursor();

	OBSSceneItem item = GetItemAtPos(pos, true);

	std::lock_guard<std::mutex> lock(selectMutex);
	hoveredPreviewItems.clear();
	hoveredPreviewItems.push_back(item);
	selectedItems.clear();

	return true;
}

float CanvasDock::GetDevicePixelRatio()
{
	return 1.0f;
}

bool CanvasDock::HandleMouseLeaveEvent(QMouseEvent *event)
{
	UNUSED_PARAMETER(event);
	std::lock_guard<std::mutex> lock(selectMutex);
	if (!selectionBox)
		hoveredPreviewItems.clear();
	return true;
}

bool CanvasDock::HandleMouseMoveEvent(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	QPointF qtPos = event->position();
#else
	QPointF qtPos = event->localPos();
#endif

	float pixelRatio = GetDevicePixelRatio();

	if (scrollMode && event->buttons() == Qt::LeftButton) {
		scrollingOffset.x += pixelRatio * ((float)qtPos.x() - scrollingFrom.x);
		scrollingOffset.y += pixelRatio * ((float)qtPos.y() - scrollingFrom.y);
		scrollingFrom.x = (float)qtPos.x();
		scrollingFrom.y = (float)qtPos.y();
		//emit DisplayResized();
		return true;
	}

	if (locked)
		return true;

	bool updateCursor = false;

	if (mouseDown) {
		vec2 pos = GetMouseEventPos(event);

		if (!mouseMoved && !mouseOverItems && stretchHandle == ItemHandle::None) {
			ProcessClick(startPos);
			mouseOverItems = SelectedAtPos(scene, startPos);
		}

		pos.x = std::round(pos.x);
		pos.y = std::round(pos.y);

		if (stretchHandle != ItemHandle::None) {
			if (obs_sceneitem_locked(stretchItem))
				return true;

			selectionBox = false;

			obs_sceneitem_t *group = obs_sceneitem_get_group(scene, stretchItem);
			if (group) {
				vec3 group_pos;
				vec3_set(&group_pos, pos.x, pos.y, 0.0f);
				vec3_transform(&group_pos, &group_pos, &invGroupTransform);
				pos.x = group_pos.x;
				pos.y = group_pos.y;
			}

			if (stretchHandle == ItemHandle::Rot) {
				RotateItem(pos);
				setCursor(Qt::ClosedHandCursor);
			} else if (cropping)
				CropItem(pos);
			else
				StretchItem(pos);

		} else if (mouseOverItems) {
			if (cursor().shape() != Qt::SizeAllCursor)
				setCursor(Qt::SizeAllCursor);
			selectionBox = false;
			MoveItems(pos);
		} else {
			selectionBox = true;
			if (!mouseMoved)
				DoSelect(startPos);
			BoxItems(startPos, pos);
		}

		mouseMoved = true;
		mousePos = pos;
	} else {
		vec2 pos = GetMouseEventPos(event);
		OBSSceneItem item = GetItemAtPos(pos, true);

		std::lock_guard<std::mutex> lock(selectMutex);
		hoveredPreviewItems.clear();
		hoveredPreviewItems.push_back(item);

		if (!mouseMoved && hoveredPreviewItems.size() > 0) {
			mousePos = pos;
			//float scale = GetDevicePixelRatio();
			//float x = qtPos.x(); // - main->previewX / scale;
			//float y = qtPos.y(); // - main->previewY / scale;
			vec2_set(&startPos, pos.x, pos.y);
			updateCursor = true;
		}
	}

	if (updateCursor) {
		GetStretchHandleData(startPos, true);
		uint32_t stretchFlags = (uint32_t)stretchHandle;
		UpdateCursor(stretchFlags);
	}
	return true;
}
bool CanvasDock::HandleMouseWheelEvent(QWheelEvent *event)
{
	UNUSED_PARAMETER(event);
	return true;
}

bool CanvasDock::HandleKeyPressEvent(QKeyEvent *event)
{
	UNUSED_PARAMETER(event);
	return true;
}

bool CanvasDock::HandleKeyReleaseEvent(QKeyEvent *event)
{
	UNUSED_PARAMETER(event);
	return true;
}

static bool CheckItemSelected(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	SceneFindData *data = reinterpret_cast<SceneFindData *>(param);
	matrix4 transform;
	vec3 transformedPos;
	vec3 pos3;

	if (!SceneItemHasVideo(item))
		return true;
	if (obs_sceneitem_is_group(item)) {
		data->group = item;
		obs_sceneitem_group_enum_items(item, CheckItemSelected, param);
		data->group = nullptr;

		if (data->item) {
			return false;
		}
	}

	vec3_set(&pos3, data->pos.x, data->pos.y, 0.0f);

	obs_sceneitem_get_box_transform(item, &transform);

	if (data->group) {
		matrix4 parent_transform;
		obs_sceneitem_get_draw_transform(data->group, &parent_transform);
		matrix4_mul(&transform, &transform, &parent_transform);
	}

	matrix4_inv(&transform, &transform);
	vec3_transform(&transformedPos, &pos3, &transform);

	if (transformedPos.x >= 0.0f && transformedPos.x <= 1.0f && transformedPos.y >= 0.0f && transformedPos.y <= 1.0f) {
		if (obs_sceneitem_selected(item)) {
			data->item = item;
			return false;
		}
	}

	UNUSED_PARAMETER(scene);
	return true;
}

bool CanvasDock::SelectedAtPos(obs_scene_t *s, const vec2 &pos)
{
	if (!s)
		return false;

	SceneFindData sfd(pos, false);
	obs_scene_enum_items(s, CheckItemSelected, &sfd);
	return !!sfd.item;
}

static bool select_one(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	obs_sceneitem_t *selectedItem = reinterpret_cast<obs_sceneitem_t *>(param);
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, select_one, param);

	obs_sceneitem_select(item, (selectedItem == item));

	UNUSED_PARAMETER(scene);
	return true;
}

void CanvasDock::DoSelect(const vec2 &pos)
{
	OBSSceneItem item = GetItemAtPos(pos, true);
	obs_scene_enum_items(scene, select_one, (obs_sceneitem_t *)item);
}

void CanvasDock::DoCtrlSelect(const vec2 &pos)
{
	OBSSceneItem item = GetItemAtPos(pos, false);
	if (!item)
		return;

	bool selected = obs_sceneitem_selected(item);
	obs_sceneitem_select(item, !selected);
}

void CanvasDock::ProcessClick(const vec2 &pos)
{
	Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();

	if (modifiers & Qt::ControlModifier)
		DoCtrlSelect(pos);
	else
		DoSelect(pos);
}

static bool FindItemAtPos(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	SceneFindData *data = reinterpret_cast<SceneFindData *>(param);
	matrix4 transform;
	matrix4 invTransform;
	vec3 transformedPos;
	vec3 pos3;
	vec3 pos3_;

	if (!SceneItemHasVideo(item))
		return true;
	if (obs_sceneitem_locked(item))
		return true;

	vec3_set(&pos3, data->pos.x, data->pos.y, 0.0f);

	obs_sceneitem_get_box_transform(item, &transform);

	matrix4_inv(&invTransform, &transform);
	vec3_transform(&transformedPos, &pos3, &invTransform);
	vec3_transform(&pos3_, &transformedPos, &transform);

	if (CloseFloat(pos3.x, pos3_.x) && CloseFloat(pos3.y, pos3_.y) && transformedPos.x >= 0.0f && transformedPos.x <= 1.0f &&
	    transformedPos.y >= 0.0f && transformedPos.y <= 1.0f) {
		if (data->selectBelow && obs_sceneitem_selected(item)) {
			if (data->item)
				return false;
			else
				data->selectBelow = false;
		}

		data->item = item;
	}

	UNUSED_PARAMETER(scene);
	return true;
}

OBSSceneItem CanvasDock::GetItemAtPos(const vec2 &pos, bool selectBelow)
{
	if (!scene)
		return OBSSceneItem();

	SceneFindData sfd(pos, selectBelow);
	obs_scene_enum_items(scene, FindItemAtPos, &sfd);
	return sfd.item;
}

vec2 CanvasDock::GetMouseEventPos(QMouseEvent *event)
{

	auto s = obs_weak_source_get_source(source);
	uint32_t sourceCX = obs_source_get_width(s);
	if (sourceCX <= 0)
		sourceCX = 1;
	uint32_t sourceCY = obs_source_get_height(s);
	if (sourceCY <= 0)
		sourceCY = 1;
	obs_source_release(s);

	int x, y;
	float scale;

	auto size = preview->size();

	GetScaleAndCenterPos(sourceCX, sourceCY, size.width(), size.height(), x, y, scale);
	//auto newCX = scale * float(sourceCX);
	//auto newCY = scale * float(sourceCY);
	float pixelRatio = GetDevicePixelRatio();

	QPoint qtPos = event->pos();

	vec2 pos;
	vec2_set(&pos, ((float)qtPos.x() - (float)x / pixelRatio) / scale, ((float)qtPos.y() - (float)y / pixelRatio) / scale);

	return pos;
}

void CanvasDock::UpdateCursor(uint32_t &flags)
{
	if (obs_sceneitem_locked(stretchItem)) {
		unsetCursor();
		return;
	}

	if (!flags && (cursor().shape() != Qt::OpenHandCursor || !scrollMode))
		unsetCursor();
	if (cursor().shape() != Qt::ArrowCursor)
		return;

	if ((flags & ITEM_LEFT && flags & ITEM_TOP) || (flags & ITEM_RIGHT && flags & ITEM_BOTTOM))
		setCursor(Qt::SizeFDiagCursor);
	else if ((flags & ITEM_LEFT && flags & ITEM_BOTTOM) || (flags & ITEM_RIGHT && flags & ITEM_TOP))
		setCursor(Qt::SizeBDiagCursor);
	else if (flags & ITEM_LEFT || flags & ITEM_RIGHT)
		setCursor(Qt::SizeHorCursor);
	else if (flags & ITEM_TOP || flags & ITEM_BOTTOM)
		setCursor(Qt::SizeVerCursor);
	else if (flags & ITEM_ROT)
		setCursor(Qt::OpenHandCursor);
}

static void RotatePos(vec2 *pos, float rot)
{
	float cosR = cos(rot);
	float sinR = sin(rot);

	vec2 newPos;

	newPos.x = cosR * pos->x - sinR * pos->y;
	newPos.y = sinR * pos->x + cosR * pos->y;

	vec2_copy(pos, &newPos);
}

void CanvasDock::RotateItem(const vec2 &pos)
{
	Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
	bool shiftDown = (modifiers & Qt::ShiftModifier);
	bool ctrlDown = (modifiers & Qt::ControlModifier);

	vec2 pos2;
	vec2_copy(&pos2, &pos);

	float angle = atan2(pos2.y - rotatePoint.y, pos2.x - rotatePoint.x) + RAD(90);

#define ROT_SNAP(rot, thresh)                                    \
	if (abs(angle - RAD((float)rot)) < RAD((float)thresh)) { \
		angle = RAD((float)rot);                         \
	}

	if (shiftDown) {
		for (int i = 0; i <= 360 / 15; i++) {
			ROT_SNAP(i * 15 - 90, 7.5);
		}
	} else if (!ctrlDown) {
		ROT_SNAP(rotateAngle, 5)

		ROT_SNAP(-90, 5)
		ROT_SNAP(-45, 5)
		ROT_SNAP(0, 5)
		ROT_SNAP(45, 5)
		ROT_SNAP(90, 5)
		ROT_SNAP(135, 5)
		ROT_SNAP(180, 5)
		ROT_SNAP(225, 5)
		ROT_SNAP(270, 5)
		ROT_SNAP(315, 5)
	}
#undef ROT_SNAP

	vec2 pos3;
	vec2_copy(&pos3, &offsetPoint);
	RotatePos(&pos3, angle);
	pos3.x += rotatePoint.x;
	pos3.y += rotatePoint.y;

	obs_sceneitem_set_rot(stretchItem, DEG(angle));
	obs_sceneitem_set_pos(stretchItem, &pos3);
}

static float maxfunc(float x, float y)
{
	return x > y ? x : y;
}

static float minfunc(float x, float y)
{
	return x < y ? x : y;
}

void CanvasDock::CropItem(const vec2 &pos)
{
	obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(stretchItem);
	uint32_t stretchFlags = (uint32_t)stretchHandle;
	uint32_t align = obs_sceneitem_get_alignment(stretchItem);
	vec3 tl, br, pos3;

	vec3_zero(&tl);
	vec3_set(&br, stretchItemSize.x, stretchItemSize.y, 0.0f);

	vec3_set(&pos3, pos.x, pos.y, 0.0f);
	vec3_transform(&pos3, &pos3, &screenToItem);

	obs_sceneitem_crop crop = startCrop;
	vec2 scale;

	obs_sceneitem_get_scale(stretchItem, &scale);

	vec2 max_tl;
	vec2 max_br;

	vec2_set(&max_tl, float(-crop.left) * scale.x, float(-crop.top) * scale.y);
	vec2_set(&max_br, stretchItemSize.x + (float)crop.right * scale.x, stretchItemSize.y + (float)crop.bottom * scale.y);

	typedef std::function<float(float, float)> minmax_func_t;

	minmax_func_t min_x = scale.x < 0.0f ? maxfunc : minfunc;
	minmax_func_t min_y = scale.y < 0.0f ? maxfunc : minfunc;
	minmax_func_t max_x = scale.x < 0.0f ? minfunc : maxfunc;
	minmax_func_t max_y = scale.y < 0.0f ? minfunc : maxfunc;

	pos3.x = min_x(pos3.x, max_br.x);
	pos3.x = max_x(pos3.x, max_tl.x);
	pos3.y = min_y(pos3.y, max_br.y);
	pos3.y = max_y(pos3.y, max_tl.y);

	if (stretchFlags & ITEM_LEFT) {
		float maxX = stretchItemSize.x - (2.0f * scale.x);
		pos3.x = tl.x = min_x(pos3.x, maxX);

	} else if (stretchFlags & ITEM_RIGHT) {
		float minX = (2.0f * scale.x);
		pos3.x = br.x = max_x(pos3.x, minX);
	}

	if (stretchFlags & ITEM_TOP) {
		float maxY = stretchItemSize.y - (2.0f * scale.y);
		pos3.y = tl.y = min_y(pos3.y, maxY);

	} else if (stretchFlags & ITEM_BOTTOM) {
		float minY = (2.0f * scale.y);
		pos3.y = br.y = max_y(pos3.y, minY);
	}

#define ALIGN_X (ITEM_LEFT | ITEM_RIGHT)
#define ALIGN_Y (ITEM_TOP | ITEM_BOTTOM)
	vec3 newPos;
	vec3_zero(&newPos);

	uint32_t align_x = (align & ALIGN_X);
	uint32_t align_y = (align & ALIGN_Y);
	if (align_x == (stretchFlags & ALIGN_X) && align_x != 0)
		newPos.x = pos3.x;
	else if (align & ITEM_RIGHT)
		newPos.x = stretchItemSize.x;
	else if (!(align & ITEM_LEFT))
		newPos.x = stretchItemSize.x * 0.5f;

	if (align_y == (stretchFlags & ALIGN_Y) && align_y != 0)
		newPos.y = pos3.y;
	else if (align & ITEM_BOTTOM)
		newPos.y = stretchItemSize.y;
	else if (!(align & ITEM_TOP))
		newPos.y = stretchItemSize.y * 0.5f;
#undef ALIGN_X
#undef ALIGN_Y

	crop = startCrop;

	if (stretchFlags & ITEM_LEFT)
		crop.left += int(std::round(tl.x / scale.x));
	else if (stretchFlags & ITEM_RIGHT)
		crop.right += int(std::round((stretchItemSize.x - br.x) / scale.x));

	if (stretchFlags & ITEM_TOP)
		crop.top += int(std::round(tl.y / scale.y));
	else if (stretchFlags & ITEM_BOTTOM)
		crop.bottom += int(std::round((stretchItemSize.y - br.y) / scale.y));

	vec3_transform(&newPos, &newPos, &itemToScreen);
	newPos.x = std::round(newPos.x);
	newPos.y = std::round(newPos.y);

#if 0
	vec3 curPos;
	vec3_zero(&curPos);
	obs_sceneitem_get_pos(stretchItem, (vec2*)&curPos);
	blog(LOG_DEBUG, "curPos {%d, %d} - newPos {%d, %d}",
			int(curPos.x), int(curPos.y),
			int(newPos.x), int(newPos.y));
	blog(LOG_DEBUG, "crop {%d, %d, %d, %d}",
			crop.left, crop.top,
			crop.right, crop.bottom);
#endif

	obs_sceneitem_defer_update_begin(stretchItem);
	obs_sceneitem_set_crop(stretchItem, &crop);
	if (boundsType == OBS_BOUNDS_NONE)
		obs_sceneitem_set_pos(stretchItem, (vec2 *)&newPos);
	obs_sceneitem_defer_update_end(stretchItem);
}

void CanvasDock::StretchItem(const vec2 &pos)
{
	Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();
	obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(stretchItem);
	uint32_t stretchFlags = (uint32_t)stretchHandle;
	bool shiftDown = (modifiers & Qt::ShiftModifier);
	vec3 tl, br, pos3;

	vec3_zero(&tl);
	vec3_set(&br, stretchItemSize.x, stretchItemSize.y, 0.0f);

	vec3_set(&pos3, pos.x, pos.y, 0.0f);
	vec3_transform(&pos3, &pos3, &screenToItem);

	if (stretchFlags & ITEM_LEFT)
		tl.x = pos3.x;
	else if (stretchFlags & ITEM_RIGHT)
		br.x = pos3.x;

	if (stretchFlags & ITEM_TOP)
		tl.y = pos3.y;
	else if (stretchFlags & ITEM_BOTTOM)
		br.y = pos3.y;

	if (!(modifiers & Qt::ControlModifier))
		SnapStretchingToScreen(tl, br);

	obs_source_t *s = obs_sceneitem_get_source(stretchItem);

	vec2 baseSize;
	vec2_set(&baseSize, float(obs_source_get_width(s)), float(obs_source_get_height(s)));

	vec2 size;
	vec2_set(&size, br.x - tl.x, br.y - tl.y);

	if (boundsType != OBS_BOUNDS_NONE) {
		if (shiftDown)
			ClampAspect(tl, br, size, baseSize);

		if (tl.x > br.x)
			std::swap(tl.x, br.x);
		if (tl.y > br.y)
			std::swap(tl.y, br.y);

		vec2_abs(&size, &size);

		obs_sceneitem_set_bounds(stretchItem, &size);
	} else {
		obs_sceneitem_crop crop;
		obs_sceneitem_get_crop(stretchItem, &crop);

		baseSize.x -= float(crop.left + crop.right);
		baseSize.y -= float(crop.top + crop.bottom);

		if (baseSize.x > 0.0 && baseSize.y > 0.0) {
			if (!shiftDown)
				ClampAspect(tl, br, size, baseSize);

			vec2_div(&size, &size, &baseSize);
			obs_sceneitem_set_scale(stretchItem, &size);
		}
	}

	pos3 = CalculateStretchPos(tl, br);
	vec3_transform(&pos3, &pos3, &itemToScreen);

	vec2 newPos;
	vec2_set(&newPos, std::round(pos3.x), std::round(pos3.y));
	obs_sceneitem_set_pos(stretchItem, &newPos);
}

void CanvasDock::SnapStretchingToScreen(vec3 &tl, vec3 &br)
{
	uint32_t stretchFlags = (uint32_t)stretchHandle;
	vec3 newTL = GetTransformedPos(tl.x, tl.y, itemToScreen);
	vec3 newTR = GetTransformedPos(br.x, tl.y, itemToScreen);
	vec3 newBL = GetTransformedPos(tl.x, br.y, itemToScreen);
	vec3 newBR = GetTransformedPos(br.x, br.y, itemToScreen);
	vec3 boundingTL;
	vec3 boundingBR;

	vec3_copy(&boundingTL, &newTL);
	vec3_min(&boundingTL, &boundingTL, &newTR);
	vec3_min(&boundingTL, &boundingTL, &newBL);
	vec3_min(&boundingTL, &boundingTL, &newBR);

	vec3_copy(&boundingBR, &newTL);
	vec3_max(&boundingBR, &boundingBR, &newTR);
	vec3_max(&boundingBR, &boundingBR, &newBL);
	vec3_max(&boundingBR, &boundingBR, &newBR);

	vec3 offset = GetSnapOffset(boundingTL, boundingBR);
	vec3_add(&offset, &offset, &newTL);
	vec3_transform(&offset, &offset, &screenToItem);
	vec3_sub(&offset, &offset, &tl);

	if (stretchFlags & ITEM_LEFT)
		tl.x += offset.x;
	else if (stretchFlags & ITEM_RIGHT)
		br.x += offset.x;

	if (stretchFlags & ITEM_TOP)
		tl.y += offset.y;
	else if (stretchFlags & ITEM_BOTTOM)
		br.y += offset.y;
}

vec3 CanvasDock::GetSnapOffset(const vec3 &tl, const vec3 &br)
{
	auto s = obs_weak_source_get_source(source);
	vec2 screenSize;
	screenSize.x = (float)obs_source_get_base_width(s);
	screenSize.y = (float)obs_source_get_base_height(s);
	obs_source_release(s);
	vec3 clampOffset;

	vec3_zero(&clampOffset);

	auto config = get_user_config();
	if (!config)
		return clampOffset;

	const bool snap = config_get_bool(config, "BasicWindow", "SnappingEnabled");
	if (snap == false)
		return clampOffset;

	const bool screenSnap = config_get_bool(config, "BasicWindow", "ScreenSnapping");
	const bool centerSnap = config_get_bool(config, "BasicWindow", "CenterSnapping");

	const float clampDist = (float)config_get_double(config, "BasicWindow", "SnapDistance") / previewScale;
	const float centerX = br.x - (br.x - tl.x) / 2.0f;
	const float centerY = br.y - (br.y - tl.y) / 2.0f;

	// Left screen edge.
	if (screenSnap && fabsf(tl.x) < clampDist)
		clampOffset.x = -tl.x;
	// Right screen edge.
	if (screenSnap && fabsf(clampOffset.x) < EPSILON && fabsf(screenSize.x - br.x) < clampDist)
		clampOffset.x = screenSize.x - br.x;
	// Horizontal center.
	if (centerSnap && fabsf(screenSize.x - (br.x - tl.x)) > clampDist && fabsf(screenSize.x / 2.0f - centerX) < clampDist)
		clampOffset.x = screenSize.x / 2.0f - centerX;

	// Top screen edge.
	if (screenSnap && fabsf(tl.y) < clampDist)
		clampOffset.y = -tl.y;
	// Bottom screen edge.
	if (screenSnap && fabsf(clampOffset.y) < EPSILON && fabsf(screenSize.y - br.y) < clampDist)
		clampOffset.y = screenSize.y - br.y;
	// Vertical center.
	if (centerSnap && fabsf(screenSize.y - (br.y - tl.y)) > clampDist && fabsf(screenSize.y / 2.0f - centerY) < clampDist)
		clampOffset.y = screenSize.y / 2.0f - centerY;

	return clampOffset;
}

static bool move_items(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	if (obs_sceneitem_locked(item))
		return true;

	bool selected = obs_sceneitem_selected(item);
	vec2 *offset = reinterpret_cast<vec2 *>(param);

	if (obs_sceneitem_is_group(item) && !selected) {
		matrix4 transform;
		vec3 new_offset;
		vec3_set(&new_offset, offset->x, offset->y, 0.0f);

		obs_sceneitem_get_draw_transform(item, &transform);
		vec4_set(&transform.t, 0.0f, 0.0f, 0.0f, 1.0f);
		matrix4_inv(&transform, &transform);
		vec3_transform(&new_offset, &new_offset, &transform);
		obs_sceneitem_group_enum_items(item, move_items, &new_offset);
	}

	if (selected) {
		vec2 pos;
		obs_sceneitem_get_pos(item, &pos);
		vec2_add(&pos, &pos, offset);
		obs_sceneitem_set_pos(item, &pos);
	}

	UNUSED_PARAMETER(scene);
	return true;
}

void CanvasDock::MoveItems(const vec2 &pos)
{
	Qt::KeyboardModifiers modifiers = QGuiApplication::keyboardModifiers();

	vec2 offset, moveOffset;
	vec2_sub(&offset, &pos, &startPos);
	vec2_sub(&moveOffset, &offset, &lastMoveOffset);

	if (!(modifiers & Qt::ControlModifier))
		SnapItemMovement(moveOffset);

	vec2_add(&lastMoveOffset, &lastMoveOffset, &moveOffset);

	obs_scene_enum_items(scene, move_items, &moveOffset);
}

struct SelectedItemBounds {
	bool first = true;
	vec3 tl, br;
};

static bool AddItemBounds(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	SelectedItemBounds *data = reinterpret_cast<SelectedItemBounds *>(param);
	vec3 t[4];

	auto add_bounds = [data, &t]() {
		for (const vec3 &v : t) {
			if (data->first) {
				vec3_copy(&data->tl, &v);
				vec3_copy(&data->br, &v);
				data->first = false;
			} else {
				vec3_min(&data->tl, &data->tl, &v);
				vec3_max(&data->br, &data->br, &v);
			}
		}
	};

	if (obs_sceneitem_is_group(item)) {
		SelectedItemBounds sib;
		obs_sceneitem_group_enum_items(item, AddItemBounds, &sib);

		if (!sib.first) {
			matrix4 xform;
			obs_sceneitem_get_draw_transform(item, &xform);

			vec3_set(&t[0], sib.tl.x, sib.tl.y, 0.0f);
			vec3_set(&t[1], sib.tl.x, sib.br.y, 0.0f);
			vec3_set(&t[2], sib.br.x, sib.tl.y, 0.0f);
			vec3_set(&t[3], sib.br.x, sib.br.y, 0.0f);
			vec3_transform(&t[0], &t[0], &xform);
			vec3_transform(&t[1], &t[1], &xform);
			vec3_transform(&t[2], &t[2], &xform);
			vec3_transform(&t[3], &t[3], &xform);
			add_bounds();
		}
	}
	if (!obs_sceneitem_selected(item))
		return true;

	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	t[0] = GetTransformedPos(0.0f, 0.0f, boxTransform);
	t[1] = GetTransformedPos(1.0f, 0.0f, boxTransform);
	t[2] = GetTransformedPos(0.0f, 1.0f, boxTransform);
	t[3] = GetTransformedPos(1.0f, 1.0f, boxTransform);
	add_bounds();

	UNUSED_PARAMETER(scene);
	return true;
}

struct OffsetData {
	float clampDist;
	vec3 tl, br, offset;
};

static bool GetSourceSnapOffset(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	OffsetData *data = reinterpret_cast<OffsetData *>(param);

	if (obs_sceneitem_selected(item))
		return true;

	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	vec3 t[4] = {GetTransformedPos(0.0f, 0.0f, boxTransform), GetTransformedPos(1.0f, 0.0f, boxTransform),
		     GetTransformedPos(0.0f, 1.0f, boxTransform), GetTransformedPos(1.0f, 1.0f, boxTransform)};

	bool first = true;
	vec3 tl, br;
	vec3_zero(&tl);
	vec3_zero(&br);
	for (const vec3 &v : t) {
		if (first) {
			vec3_copy(&tl, &v);
			vec3_copy(&br, &v);
			first = false;
		} else {
			vec3_min(&tl, &tl, &v);
			vec3_max(&br, &br, &v);
		}
	}

	// Snap to other source edges
#define EDGE_SNAP(l, r, x, y)                                                                                              \
	do {                                                                                                               \
		double dist = fabsf(l.x - data->r.x);                                                                      \
		if (dist < data->clampDist && fabsf(data->offset.x) < EPSILON && data->tl.y < br.y && data->br.y > tl.y && \
		    (fabsf(data->offset.x) > dist || data->offset.x < EPSILON))                                            \
			data->offset.x = l.x - data->r.x;                                                                  \
	} while (false)

	EDGE_SNAP(tl, br, x, y);
	EDGE_SNAP(tl, br, y, x);
	EDGE_SNAP(br, tl, x, y);
	EDGE_SNAP(br, tl, y, x);
#undef EDGE_SNAP

	UNUSED_PARAMETER(scene);
	return true;
}

void CanvasDock::SnapItemMovement(vec2 &offset)
{
	SelectedItemBounds sib;
	obs_scene_enum_items(scene, AddItemBounds, &sib);

	sib.tl.x += offset.x;
	sib.tl.y += offset.y;
	sib.br.x += offset.x;
	sib.br.y += offset.y;

	vec3 snapOffset = GetSnapOffset(sib.tl, sib.br);

	auto config = get_user_config();
	if (!config)
		return;

	const bool snap = config_get_bool(config, "BasicWindow", "SnappingEnabled");
	const bool sourcesSnap = config_get_bool(config, "BasicWindow", "SourceSnapping");
	if (snap == false)
		return;
	if (sourcesSnap == false) {
		offset.x += snapOffset.x;
		offset.y += snapOffset.y;
		return;
	}

	const float clampDist = (float)config_get_double(config, "BasicWindow", "SnapDistance") / previewScale;

	OffsetData offsetData;
	offsetData.clampDist = clampDist;
	offsetData.tl = sib.tl;
	offsetData.br = sib.br;
	vec3_copy(&offsetData.offset, &snapOffset);

	obs_scene_enum_items(scene, GetSourceSnapOffset, &offsetData);

	if (fabsf(offsetData.offset.x) > EPSILON || fabsf(offsetData.offset.y) > EPSILON) {
		offset.x += offsetData.offset.x;
		offset.y += offsetData.offset.y;
	} else {
		offset.x += snapOffset.x;
		offset.y += snapOffset.y;
	}
}

static bool CounterClockwise(float x1, float x2, float x3, float y1, float y2, float y3)
{
	return (y3 - y1) * (x2 - x1) > (y2 - y1) * (x3 - x1);
}

static bool IntersectLine(float x1, float x2, float x3, float x4, float y1, float y2, float y3, float y4)
{
	bool a = CounterClockwise(x1, x2, x3, y1, y2, y3);
	bool b = CounterClockwise(x1, x2, x4, y1, y2, y4);
	bool c = CounterClockwise(x3, x4, x1, y3, y4, y1);
	bool d = CounterClockwise(x3, x4, x2, y3, y4, y2);

	return (a != b) && (c != d);
}

static bool IntersectBox(matrix4 transform, float x1, float x2, float y1, float y2)
{
	float x3, x4, y3, y4;

	x3 = transform.t.x;
	y3 = transform.t.y;
	x4 = x3 + transform.x.x;
	y4 = y3 + transform.x.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x4 = x3 + transform.y.x;
	y4 = y3 + transform.y.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x3 = transform.t.x + transform.x.x;
	y3 = transform.t.y + transform.x.y;
	x4 = x3 + transform.y.x;
	y4 = y3 + transform.y.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x3 = transform.t.x + transform.y.x;
	y3 = transform.t.y + transform.y.y;
	x4 = x3 + transform.x.x;
	y4 = y3 + transform.x.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	return false;
}

static bool FindItemsInBox(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	SceneFindBoxData *data = reinterpret_cast<SceneFindBoxData *>(param);
	matrix4 transform;
	matrix4 invTransform;
	vec3 transformedPos;
	vec3 pos3;
	vec3 pos3_;

	vec2 pos_min, pos_max;
	vec2_min(&pos_min, &data->startPos, &data->pos);
	vec2_max(&pos_max, &data->startPos, &data->pos);

	const float x1 = pos_min.x;
	const float x2 = pos_max.x;
	const float y1 = pos_min.y;
	const float y2 = pos_max.y;

	if (!SceneItemHasVideo(item))
		return true;
	if (obs_sceneitem_locked(item))
		return true;
	if (!obs_sceneitem_visible(item))
		return true;

	vec3_set(&pos3, data->pos.x, data->pos.y, 0.0f);

	obs_sceneitem_get_box_transform(item, &transform);

	matrix4_inv(&invTransform, &transform);
	vec3_transform(&transformedPos, &pos3, &invTransform);
	vec3_transform(&pos3_, &transformedPos, &transform);

	if (CloseFloat(pos3.x, pos3_.x) && CloseFloat(pos3.y, pos3_.y) && transformedPos.x >= 0.0f && transformedPos.x <= 1.0f &&
	    transformedPos.y >= 0.0f && transformedPos.y <= 1.0f) {

		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x > x1 && transform.t.x < x2 && transform.t.y > y1 && transform.t.y < y2) {

		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x + transform.x.x > x1 && transform.t.x + transform.x.x < x2 && transform.t.y + transform.x.y > y1 &&
	    transform.t.y + transform.x.y < y2) {

		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x + transform.y.x > x1 && transform.t.x + transform.y.x < x2 && transform.t.y + transform.y.y > y1 &&
	    transform.t.y + transform.y.y < y2) {

		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x + transform.x.x + transform.y.x > x1 && transform.t.x + transform.x.x + transform.y.x < x2 &&
	    transform.t.y + transform.x.y + transform.y.y > y1 && transform.t.y + transform.x.y + transform.y.y < y2) {

		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x + 0.5 * (transform.x.x + transform.y.x) > x1 &&
	    transform.t.x + 0.5 * (transform.x.x + transform.y.x) < x2 &&
	    transform.t.y + 0.5 * (transform.x.y + transform.y.y) > y1 &&
	    transform.t.y + 0.5 * (transform.x.y + transform.y.y) < y2) {

		data->sceneItems.push_back(item);
		return true;
	}

	if (IntersectBox(transform, x1, x2, y1, y2)) {
		data->sceneItems.push_back(item);
		return true;
	}

	UNUSED_PARAMETER(scene);
	return true;
}

void CanvasDock::BoxItems(const vec2 &start_pos, const vec2 &pos)
{
	if (!scene)
		return;

	if (cursor().shape() != Qt::CrossCursor)
		setCursor(Qt::CrossCursor);

	SceneFindBoxData sfbd(start_pos, pos);
	obs_scene_enum_items(scene, FindItemsInBox, &sfbd);

	std::lock_guard<std::mutex> lock(selectMutex);
	hoveredPreviewItems = sfbd.sceneItems;
}

struct HandleFindData {
	const vec2 &pos;
	const float radius;
	matrix4 parent_xform;

	OBSSceneItem item;
	ItemHandle handle = ItemHandle::None;
	float angle = 0.0f;
	vec2 rotatePoint;
	vec2 offsetPoint;

	float angleOffset = 0.0f;

	HandleFindData(const HandleFindData &) = delete;
	HandleFindData(HandleFindData &&) = delete;
	HandleFindData &operator=(const HandleFindData &) = delete;
	HandleFindData &operator=(HandleFindData &&) = delete;

	inline HandleFindData(const vec2 &pos_, float scale) : pos(pos_), radius(HANDLE_SEL_RADIUS / scale)
	{
		matrix4_identity(&parent_xform);
	}

	inline HandleFindData(const HandleFindData &hfd, obs_sceneitem_t *parent)
		: pos(hfd.pos),
		  radius(hfd.radius),
		  item(hfd.item),
		  handle(hfd.handle),
		  angle(hfd.angle),
		  rotatePoint(hfd.rotatePoint),
		  offsetPoint(hfd.offsetPoint)
	{
		obs_sceneitem_get_draw_transform(parent, &parent_xform);
	}
};

static bool FindHandleAtPos(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	HandleFindData &data = *reinterpret_cast<HandleFindData *>(param);

	if (!obs_sceneitem_selected(item)) {
		if (obs_sceneitem_is_group(item)) {
			HandleFindData newData(data, item);
			newData.angleOffset = obs_sceneitem_get_rot(item);

			obs_sceneitem_group_enum_items(item, FindHandleAtPos, &newData);

			data.item = newData.item;
			data.handle = newData.handle;
			data.angle = newData.angle;
			data.rotatePoint = newData.rotatePoint;
			data.offsetPoint = newData.offsetPoint;
		}

		return true;
	}

	matrix4 transform;
	vec3 pos3;
	float closestHandle = data.radius;

	vec3_set(&pos3, data.pos.x, data.pos.y, 0.0f);

	obs_sceneitem_get_box_transform(item, &transform);

	auto TestHandle = [&](float x, float y, ItemHandle handle) {
		vec3 handlePos = GetTransformedPos(x, y, transform);
		vec3_transform(&handlePos, &handlePos, &data.parent_xform);

		float dist = vec3_dist(&handlePos, &pos3);
		if (dist < data.radius) {
			if (dist < closestHandle) {
				closestHandle = dist;
				data.handle = handle;
				data.item = item;
			}
		}
	};

	TestHandle(0.0f, 0.0f, ItemHandle::TopLeft);
	TestHandle(0.5f, 0.0f, ItemHandle::TopCenter);
	TestHandle(1.0f, 0.0f, ItemHandle::TopRight);
	TestHandle(0.0f, 0.5f, ItemHandle::CenterLeft);
	TestHandle(1.0f, 0.5f, ItemHandle::CenterRight);
	TestHandle(0.0f, 1.0f, ItemHandle::BottomLeft);
	TestHandle(0.5f, 1.0f, ItemHandle::BottomCenter);
	TestHandle(1.0f, 1.0f, ItemHandle::BottomRight);

	vec2 rotHandleOffset;
	vec2_set(&rotHandleOffset, 0.0f, HANDLE_RADIUS * data.radius * 1.5f - data.radius);
	RotatePos(&rotHandleOffset, atan2(transform.x.y, transform.x.x));
	RotatePos(&rotHandleOffset, RAD(data.angleOffset));

	vec3 handlePos = GetTransformedPos(0.5f, 0.0f, transform);
	vec3_transform(&handlePos, &handlePos, &data.parent_xform);
	handlePos.x -= rotHandleOffset.x;
	handlePos.y -= rotHandleOffset.y;

	float dist = vec3_dist(&handlePos, &pos3);
	if (dist < data.radius) {
		if (dist < closestHandle) {
			closestHandle = dist;
			data.item = item;
			data.angle = obs_sceneitem_get_rot(item);
			data.handle = ItemHandle::Rot;

			vec2_set(&data.rotatePoint, transform.t.x + transform.x.x / 2 + transform.y.x / 2,
				 transform.t.y + transform.x.y / 2 + transform.y.y / 2);

			obs_sceneitem_get_pos(item, &data.offsetPoint);
			data.offsetPoint.x -= data.rotatePoint.x;
			data.offsetPoint.y -= data.rotatePoint.y;

			RotatePos(&data.offsetPoint, -RAD(obs_sceneitem_get_rot(item)));
		}
	}

	UNUSED_PARAMETER(scene);
	return true;
}

void CanvasDock::GetStretchHandleData(const vec2 &pos, bool ignoreGroup)
{
	if (!scene)
		return;

	HandleFindData hfd(pos, previewScale);
	obs_scene_enum_items(scene, FindHandleAtPos, &hfd);

	stretchItem = std::move(hfd.item);
	stretchHandle = hfd.handle;

	rotateAngle = hfd.angle;
	rotatePoint = hfd.rotatePoint;
	offsetPoint = hfd.offsetPoint;

	if (stretchHandle != ItemHandle::None) {
		matrix4 boxTransform;
		vec3 itemUL;
		float itemRot;

		stretchItemSize = GetItemSize(stretchItem);

		obs_sceneitem_get_box_transform(stretchItem, &boxTransform);
		itemRot = obs_sceneitem_get_rot(stretchItem);
		vec3_from_vec4(&itemUL, &boxTransform.t);

		/* build the item space conversion matrices */
		matrix4_identity(&itemToScreen);
		matrix4_rotate_aa4f(&itemToScreen, &itemToScreen, 0.0f, 0.0f, 1.0f, RAD(itemRot));
		matrix4_translate3f(&itemToScreen, &itemToScreen, itemUL.x, itemUL.y, 0.0f);

		matrix4_identity(&screenToItem);
		matrix4_translate3f(&screenToItem, &screenToItem, -itemUL.x, -itemUL.y, 0.0f);
		matrix4_rotate_aa4f(&screenToItem, &screenToItem, 0.0f, 0.0f, 1.0f, RAD(-itemRot));

		obs_sceneitem_get_crop(stretchItem, &startCrop);
		obs_sceneitem_get_pos(stretchItem, &startItemPos);

		obs_source_t *s = obs_sceneitem_get_source(stretchItem);
		cropSize.x = float(obs_source_get_width(s) - startCrop.left - startCrop.right);
		cropSize.y = float(obs_source_get_height(s) - startCrop.top - startCrop.bottom);

		stretchGroup = obs_sceneitem_get_group(scene, stretchItem);
		if (stretchGroup && !ignoreGroup) {
			obs_sceneitem_get_draw_transform(stretchGroup, &invGroupTransform);
			matrix4_inv(&invGroupTransform, &invGroupTransform);
			obs_sceneitem_defer_group_resize_begin(stretchGroup);
		} else {
			stretchGroup = nullptr;
		}
	}
}

void CanvasDock::ClampAspect(vec3 &tl, vec3 &br, vec2 &size, const vec2 &baseSize)
{
	float baseAspect = baseSize.x / baseSize.y;
	float aspect = size.x / size.y;
	uint32_t stretchFlags = (uint32_t)stretchHandle;

	if (stretchHandle == ItemHandle::TopLeft || stretchHandle == ItemHandle::TopRight ||
	    stretchHandle == ItemHandle::BottomLeft || stretchHandle == ItemHandle::BottomRight) {
		if (aspect < baseAspect) {
			if ((size.y >= 0.0f && size.x >= 0.0f) || (size.y <= 0.0f && size.x <= 0.0f))
				size.x = size.y * baseAspect;
			else
				size.x = size.y * baseAspect * -1.0f;
		} else {
			if ((size.y >= 0.0f && size.x >= 0.0f) || (size.y <= 0.0f && size.x <= 0.0f))
				size.y = size.x / baseAspect;
			else
				size.y = size.x / baseAspect * -1.0f;
		}

	} else if (stretchHandle == ItemHandle::TopCenter || stretchHandle == ItemHandle::BottomCenter) {
		if ((size.y >= 0.0f && size.x >= 0.0f) || (size.y <= 0.0f && size.x <= 0.0f))
			size.x = size.y * baseAspect;
		else
			size.x = size.y * baseAspect * -1.0f;

	} else if (stretchHandle == ItemHandle::CenterLeft || stretchHandle == ItemHandle::CenterRight) {
		if ((size.y >= 0.0f && size.x >= 0.0f) || (size.y <= 0.0f && size.x <= 0.0f))
			size.y = size.x / baseAspect;
		else
			size.y = size.x / baseAspect * -1.0f;
	}

	size.x = std::round(size.x);
	size.y = std::round(size.y);

	if (stretchFlags & ITEM_LEFT)
		tl.x = br.x - size.x;
	else if (stretchFlags & ITEM_RIGHT)
		br.x = tl.x + size.x;

	if (stretchFlags & ITEM_TOP)
		tl.y = br.y - size.y;
	else if (stretchFlags & ITEM_BOTTOM)
		br.y = tl.y + size.y;
}

vec3 CanvasDock::CalculateStretchPos(const vec3 &tl, const vec3 &br)
{
	uint32_t alignment = obs_sceneitem_get_alignment(stretchItem);
	vec3 pos;

	vec3_zero(&pos);

	if (alignment & OBS_ALIGN_LEFT)
		pos.x = tl.x;
	else if (alignment & OBS_ALIGN_RIGHT)
		pos.x = br.x;
	else
		pos.x = (br.x - tl.x) * 0.5f + tl.x;

	if (alignment & OBS_ALIGN_TOP)
		pos.y = tl.y;
	else if (alignment & OBS_ALIGN_BOTTOM)
		pos.y = br.y;
	else
		pos.y = (br.y - tl.y) * 0.5f + tl.y;

	return pos;
}

bool CanvasDock::DrawSelectionBox(float x1, float y1, float x2, float y2, gs_vertbuffer_t *rect_fill)
{
	float pixelRatio = GetDevicePixelRatio();

	x1 = std::round(x1);
	x2 = std::round(x2);
	y1 = std::round(y1);
	y2 = std::round(y2);

	gs_effect_t *eff = gs_get_effect();
	gs_eparam_t *colParam = gs_effect_get_param_by_name(eff, "color");

	vec4 fillColor;
	vec4_set(&fillColor, 0.7f, 0.7f, 0.7f, 0.5f);

	vec4 borderColor;
	vec4_set(&borderColor, 1.0f, 1.0f, 1.0f, 1.0f);

	vec2 scale;
	vec2_set(&scale, std::abs(x2 - x1), std::abs(y2 - y1));

	gs_matrix_push();
	gs_matrix_identity();

	gs_matrix_translate3f(x1, y1, 0.0f);
	gs_matrix_scale3f(x2 - x1, y2 - y1, 1.0f);

	gs_effect_set_vec4(colParam, &fillColor);
	gs_load_vertexbuffer(rect_fill);
	gs_draw(GS_TRISTRIP, 0, 0);

	gs_effect_set_vec4(colParam, &borderColor);
	DrawRect(HANDLE_RADIUS * pixelRatio / 2, scale);

	gs_matrix_pop();

	return true;
}
struct descendant_info {
	bool exists;
	obs_weak_source_t *target;
	obs_source_t *target2;
};

static void check_descendant(obs_source_t *parent, obs_source_t *child, void *param)
{
	auto *info = (struct descendant_info *)param;
	if (parent == info->target2 || child == info->target2 || obs_weak_source_references_source(info->target, child) ||
	    obs_weak_source_references_source(info->target, parent))
		info->exists = true;
}

bool CanvasDock::add_sources_of_type_to_menu(void *param, obs_source_t *source)
{
	QMenu *menu = static_cast<QMenu *>(param);
	auto parent = qobject_cast<QMenu *>(menu->parent());
	while (parent && qobject_cast<QMenu *>(parent->parent()))
		parent = qobject_cast<QMenu *>(parent->parent());
	CanvasDock *cd = static_cast<CanvasDock *>(parent ? parent->parent() : menu->parent());
	auto a = parent ? parent->menuAction() : menu->menuAction();
	auto t = a->data().toString();
	auto idUtf8 = t.toUtf8();
	const char *id = idUtf8.constData();
	if (strcmp(obs_source_get_unversioned_id(source), id) == 0) {
		auto name = QString::fromUtf8(obs_source_get_name(source));
		QList<QAction *> actions = menu->actions();
		QAction *after = nullptr;
		for (QAction *menuAction : actions) {
			if (menuAction->text().compare(name) >= 0) {
				after = menuAction;
				break;
			}
		}
		auto na = new QAction(name, menu);
		connect(na, &QAction::triggered, cd, [cd, source] { cd->AddSourceToScene(source); }, Qt::QueuedConnection);
		menu->insertAction(after, na);
		struct descendant_info info = {false, cd->source, obs_scene_get_source(cd->scene)};
		obs_source_enum_full_tree(source, check_descendant, &info);
		na->setEnabled(!info.exists);
	}
	return true;
}

void CanvasDock::LoadSourceTypeMenu(QMenu *menu, const char *type)
{
	menu->clear();
	if (obs_get_source_output_flags(type) & OBS_SOURCE_REQUIRES_CANVAS) {
		obs_enum_canvases(
			[](void *param, obs_canvas_t *canvas) {
				QMenu *m = (QMenu *)param;
				auto canvas_name = QString::fromUtf8(obs_canvas_get_name(canvas));
				auto cm = m->addMenu(canvas_name);
				obs_canvas_enum_scenes(canvas, add_sources_of_type_to_menu, cm);
				return true;
			},
			menu);
	} else if (strcmp(type, "scene") == 0) {
		obs_enum_scenes(add_sources_of_type_to_menu, menu);
	} else {
		obs_enum_sources(add_sources_of_type_to_menu, menu);

		auto popupItem = new QAction(QString::fromUtf8(obs_frontend_get_locale_string("New")), menu);
		popupItem->setData(QString::fromUtf8(type));
		connect(popupItem, SIGNAL(triggered(bool)), this, SLOT(AddSourceFromAction()));

		QList<QAction *> actions = menu->actions();
		QAction *first = actions.size() ? actions.first() : nullptr;
		menu->insertAction(first, popupItem);
		menu->insertSeparator(first);
	}
}

void CanvasDock::AddSourceToScene(obs_source_t *s)
{
	obs_scene_add(scene, s);
}

void CanvasDock::AddSourceTypeToMenu(QMenu *popup, const char *source_type, const char *name)
{
	QString qname = QString::fromUtf8(name);
	QAction *popupItem = new QAction(qname, popup);
	if (strcmp(source_type, "scene") == 0) {
		popupItem->setIcon(GetSceneIcon());
	} else if (strcmp(source_type, "group") == 0) {
		popupItem->setIcon(GetGroupIcon());
	} else {
		popupItem->setIcon(GetIconFromType(obs_source_get_icon_type(source_type)));
	}
	popupItem->setData(QString::fromUtf8(source_type));
	QMenu *menu = new QMenu(popup);
	popupItem->setMenu(menu);
	QObject::connect(menu, &QMenu::aboutToShow, [this, menu, source_type] { LoadSourceTypeMenu(menu, source_type); });
	QList<QAction *> actions = popup->actions();
	QAction *after = nullptr;
	for (QAction *menuAction : actions) {
		if (menuAction->text().compare(name) >= 0) {
			after = menuAction;
			break;
		}
	}
	popup->insertAction(after, popupItem);
}

QMenu *CanvasDock::CreateAddSourcePopupMenu()
{
	const char *unversioned_type;
	const char *type;
	bool foundValues = false;
	bool foundDeprecated = false;
	size_t idx = 0;

	QMenu *popup = new QMenu(QString::fromUtf8(obs_frontend_get_locale_string("Add")), this);
	QMenu *deprecated = new QMenu(QString::fromUtf8(obs_frontend_get_locale_string("Deprecated")), popup);

	while (obs_enum_input_types2(idx++, &type, &unversioned_type)) {
		const char *name = obs_source_get_display_name(type);
		if (!name)
			continue;
		uint32_t caps = obs_get_source_output_flags(type);

		if ((caps & OBS_SOURCE_CAP_DISABLED) != 0)
			continue;

		if ((caps & OBS_SOURCE_DEPRECATED) == 0) {
			AddSourceTypeToMenu(popup, unversioned_type, name);
		} else {
			AddSourceTypeToMenu(deprecated, unversioned_type, name);
			foundDeprecated = true;
		}
		foundValues = true;
	}

	AddSourceTypeToMenu(popup, "scene", obs_frontend_get_locale_string("Basic.Scene"));
	AddSourceTypeToMenu(popup, "group", obs_frontend_get_locale_string("Group"));

	if (!foundDeprecated) {
		delete deprecated;
		deprecated = nullptr;
	}

	if (!foundValues) {
		delete popup;
		popup = nullptr;

	} else if (foundDeprecated) {
		popup->addSeparator();
		popup->addMenu(deprecated);
	}

	return popup;
}

void CanvasDock::AddSourceFromAction()
{
	QAction *a = qobject_cast<QAction *>(sender());
	if (!a)
		return;

	auto t = a->data().toString();
	auto idUtf8 = t.toUtf8();
	const char *id = idUtf8.constData();
	if (id && *id && strlen(id)) {
		const char *v_id = obs_get_latest_input_type_id(id);
		QString placeHolderText = QString::fromUtf8(obs_source_get_display_name(v_id));
		QString text = placeHolderText;
		int i = 2;
		OBSSourceAutoRelease s = nullptr;
		while ((s = obs_get_source_by_name(text.toUtf8().constData()))) {
			text = QString("%1 %2").arg(placeHolderText).arg(i++);
		}
		obs_source_t *created_source = obs_source_create(id, text.toUtf8().constData(), nullptr, nullptr);
		obs_scene_add(scene, created_source);
		if (obs_source_configurable(created_source)) {
			obs_frontend_open_source_properties(created_source);
		}
		obs_source_release(created_source);
	}
}

bool CanvasDock::StartVideo()
{
	obs_canvas_t *c = nullptr;
	obs_frontend_canvas_list cl = {};
	obs_frontend_get_canvases(&cl);
	for (size_t i = 0; i < cl.canvases.num; i++) {
		if (strcmp(obs_canvas_get_name(cl.canvases.array[i]), CANVAS_NAME) == 0) {
			c = obs_canvas_get_ref(cl.canvases.array[i]);
		}
	}
	obs_frontend_canvas_list_free(&cl);
	if (canvas)
		obs_canvas_release(canvas);
	canvas = c ? c : obs_frontend_add_canvas(CANVAS_NAME, nullptr, PROGRAM);
	auto ph = obs_get_proc_handler();
	calldata_t cd2 = {0};
	calldata_set_ptr(&cd2, "canvas", canvas);
	calldata_set_string(&cd2, "canvas_name", CANVAS_NAME);
	calldata_set_ptr(&cd2, "get_transitions", (void *)CanvasDock::get_transitions);
	calldata_set_ptr(&cd2, "get_transitions_data", this);
	proc_handler_call(ph, "downstream_keyer_add_canvas", &cd2);
	calldata_free(&cd2);

	//auto sh = obs_canvas_get_signal_handler(canvas);
	//signal_handler_connect(sh, "source_rename", source_rename, this);
	//signal_handler_connect(sh, "source_remove", source_remove, this);

	auto s = obs_weak_source_get_source(source);
	obs_canvas_set_channel(canvas, 0, s);
	obs_source_release(s);

	bool started_video = false;
	if (!obs_canvas_has_video(canvas)) {
		obs_video_info ovi;
		obs_get_video_info(&ovi);
		ovi.base_width = canvas_width;
		ovi.base_height = canvas_height;
		ovi.output_width = canvas_width;
		ovi.output_height = canvas_height;
		started_video = obs_canvas_reset_video(canvas, &ovi);
	}
	return started_video;
}

void CanvasDock::virtual_cam_output_start(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("virtual_camera_started");
	d->CheckReplayBuffer(true);
	QMetaObject::invokeMethod(d, "OnVirtualCamStart");
}

void CanvasDock::virtual_cam_output_stop(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("virtual_camera_stopped");
	QMetaObject::invokeMethod(d, "OnVirtualCamStop");
	signal_handler_t *signal = obs_output_get_signal_handler(d->virtualCamOutput);
	signal_handler_disconnect(signal, "start", virtual_cam_output_start, d);
	signal_handler_disconnect(signal, "stop", virtual_cam_output_stop, d);
	obs_output_release(d->virtualCamOutput);
	d->virtualCamOutput = nullptr;
}

void CanvasDock::OnVirtualCamStart()
{
	virtualCamButton->setIcon(virtualCamActiveIcon);
	virtualCamButton->setChecked(true);
}

void CanvasDock::OnVirtualCamStop()
{
	virtualCamButton->setIcon(virtualCamInactiveIcon);
	virtualCamButton->setChecked(false);
	CheckReplayBuffer();
	if (multiCanvasSource) {
		multi_canvas_source_remove_canvas(obs_obj_get_data(multiCanvasSource), canvas);
		obs_source_release(multiCanvasSource);
		multiCanvasSource = nullptr;
	}

	if (multiCanvasVideo) {
		multiCanvasVideo = nullptr;
		obs_canvas_set_channel(multiCanvas, 0, nullptr);
	}
	if (multiCanvas) {
		obs_canvas_release(multiCanvas);
		multiCanvas = nullptr;
	}
}

void CanvasDock::VirtualCamButtonClicked()
{
	if (obs_output_active(virtualCamOutput)) {
		StopVirtualCam();
	} else {
		StartVirtualCam();
	}
}

void CanvasDock::StartVirtualCam()
{
	CheckReplayBuffer(true);
	const auto output = obs_frontend_get_virtualcam_output();
	if (obs_output_active(output)) {
		if (!virtualCamOutput)
			virtualCamButton->setChecked(false);
		obs_output_release(output);
		return;
	}

	if (!virtual_cam_warned && isVisible()) {
		QMessageBox::warning(this, QString::fromUtf8(obs_module_text("VirtualCameraVertical")),
				     QString::fromUtf8(obs_module_text("VirtualCameraWarning")));
		virtual_cam_warned = true;
	}

	virtualCamOutput = output;

	obs_canvas_t *started_canvas = nullptr;
	bool started_video = false;
	video_t *virtual_video = nullptr;
	if (virtual_cam_mode == VIRTUAL_CAMERA_VERTICAL) {
		started_video = StartVideo();
		started_canvas = canvas;
		virtual_video = obs_canvas_get_video(canvas);
	} else if (virtual_cam_mode == VIRTUAL_CAMERA_BOTH) {
		if (!multiCanvas) {
			multiCanvas = obs_canvas_create("multiCanvas", nullptr, DEVICE);
		}
		started_canvas = multiCanvas;
		if (!multiCanvasSource) {
			multiCanvasSource =
				obs_source_create_private("vertical_multi_canvas_source", "vertical_multi_canvas_source", nullptr);
			void *view_data = obs_obj_get_data(multiCanvasSource);
			multi_canvas_source_add_canvas(view_data, canvas, canvas_width, canvas_height);
		}
		if (!multiCanvasVideo) {
			obs_video_info ovi;
			obs_get_video_info(&ovi);
			ovi.base_width = obs_source_get_width(multiCanvasSource);
			ovi.base_height = obs_source_get_height(multiCanvasSource);
			ovi.output_width = ovi.base_width;
			ovi.output_height = ovi.base_height;
			obs_canvas_reset_video(multiCanvas, &ovi);
			multiCanvasVideo = obs_canvas_get_video(multiCanvas);
			started_video = true;
		}
		virtual_video = multiCanvasVideo;
		if (obs_canvas_get_channel(multiCanvas, 0) != multiCanvasSource)
			obs_canvas_set_channel(multiCanvas, 0, multiCanvasSource);
	} else {
		virtual_video = obs_get_video();
	}
	signal_handler_t *signal = obs_output_get_signal_handler(output);
	signal_handler_disconnect(signal, "start", virtual_cam_output_start, this);
	signal_handler_disconnect(signal, "stop", virtual_cam_output_stop, this);
	signal_handler_connect(signal, "start", virtual_cam_output_start, this);
	signal_handler_connect(signal, "stop", virtual_cam_output_stop, this);

	obs_output_set_media(output, virtual_video, obs_get_audio());
	SendVendorEvent("virtual_camera_starting");
	const bool success = obs_output_start(output);
	if (!success) {
		QMetaObject::invokeMethod(this, "OnVirtualCamStop");
		if (started_video) {
			if (obs_canvas_get_video(canvas) == virtual_video) {
				DestroyVideo();
			} else if (multiCanvasVideo == virtual_video) {
				multiCanvasVideo = nullptr;

				obs_canvas_set_channel(started_canvas, 0, nullptr);
				obs_canvas_release(started_canvas);
				multiCanvas = nullptr;
			} else {
				obs_canvas_set_channel(started_canvas, 0, nullptr);
			}
		}
	}
}

void CanvasDock::StopVirtualCam()
{
	if (!obs_output_active(virtualCamOutput)) {
		virtualCamButton->setChecked(false);
		return;
	}
	SendVendorEvent("virtual_camera_stopping");
	if (obs_output_video(virtualCamOutput) != obs_get_video())
		obs_output_set_media(virtualCamOutput, nullptr, nullptr);
	obs_output_stop(virtualCamOutput);
}

void CanvasDock::ConfigButtonClicked()
{
	if (!configDialog)
		configDialog = new OBSBasicSettings(this, (QMainWindow *)obs_frontend_get_main_window());

	configDialog->LoadSettings();
	configDialog->exec();

	save_canvas();
}

void CanvasDock::ReplayButtonClicked(QString filename)
{
	if (!obs_output_active(replayOutput)) {
		if (replayButton->isChecked())
			replayButton->setChecked(false);
		return;
	}
	if (!replayButton->isChecked())
		replayButton->setChecked(true);
	obs_data_t *s = obs_output_get_settings(replayOutput);
	if (!filename.isEmpty()) {
		if (replayFilename.empty())
			replayFilename = obs_data_get_string(s, "format");
		obs_data_set_string(s, "format", filename.toUtf8().constData());
	} else if (!replayFilename.empty()) {
		obs_data_set_string(s, "format", replayFilename.c_str());
	}
	obs_data_release(s);
	calldata_t cd = {0};
	proc_handler_t *ph = obs_output_get_proc_handler(replayOutput);
	proc_handler_call(ph, "save", &cd);
	calldata_free(&cd);

	statusLabel->setText(QString::fromUtf8(obs_module_text("Saving")));
	replayStatusResetTimer.start(10000);
	SendVendorEvent("backtrack_saving");
}

int GetConfigPath(char *path, size_t size, const char *name)
{
#if ALLOW_PORTABLE_MODE
	if (portable_mode) {
		if (name && *name) {
			return snprintf(path, size, CONFIG_PATH "/%s", name);
		} else {
			return snprintf(path, size, CONFIG_PATH);
		}
	} else {
		return os_get_config_path(path, size, name);
	}
#else
	return os_get_config_path(path, size, name);
#endif
}

static inline int GetProfilePath(char *path, size_t size, const char *file)
{
	char profiles_path[512];
	auto config = get_user_config();
	if (!config)
		return -1;
	const char *profile = config_get_string(config, "Basic", "ProfileDir");

	if (!profile)
		return -1;
	if (!path)
		return -1;
	if (!file)
		file = "";

	int ret = GetConfigPath(profiles_path, 512, "obs-studio/basic/profiles");
	if (ret <= 0)
		return ret;

	if (!*file)
		return snprintf(path, size, "%s/%s", profiles_path, profile);

	return snprintf(path, size, "%s/%s/%s", profiles_path, profile, file);
}

static obs_data_t *GetDataFromJsonFile(const char *jsonFile)
{
	char fullPath[512];
	obs_data_t *data = nullptr;

	int ret = GetProfilePath(fullPath, sizeof(fullPath), jsonFile);
	if (ret > 0) {
		BPtr<char> jsonData = os_quick_read_utf8_file(fullPath);
		if (!!jsonData) {
			data = obs_data_create_from_json(jsonData);
		}
	}

	if (!data)
		data = obs_data_create();

	return data;
}

void CanvasDock::RecordButtonClicked()
{
	if (obs_output_active(recordOutput)) {
		StopRecord();
	} else {
		StartRecord();
	}
}

void CanvasDock::StartRecord()
{
	if (obs_output_active(recordOutput))
		return;

	const char *format = nullptr;
	config_t *config = obs_frontend_get_profile_config();
	const char *mode = config_get_string(config, "Output", "Mode");
	const char *dir = nullptr;

	bool ffmpegOutput = false;

	if (record_advanced_settings) {
		if (file_format.empty())
			file_format = "mkv";
		format = file_format.c_str();
		dir = recordPath.c_str();
	} else if (strcmp(mode, "Advanced") == 0) {
		const char *recType = config_get_string(config, "AdvOut", "RecType");

		if (strcmp(recType, "FFmpeg") == 0) {
			ffmpegOutput = true;
			dir = config_get_string(config, "AdvOut", "FFFilePath");
		} else {
			dir = config_get_string(config, "AdvOut", "RecFilePath");
		}
		bool ffmpegRecording = ffmpegOutput && config_get_bool(config, "AdvOut", "FFOutputToFile");
		if (ffmpegRecording) {
			format = config_get_string(config, "AdvOut", "FFExtension");
		} else if (!config_has_user_value(config, "AdvOut", "RecFormat2") &&
			   config_has_user_value(config, "AdvOut", "RecFormat")) {
			format = config_get_string(config, "AdvOut", "RecFormat");
		} else {
			format = config_get_string(config, "AdvOut", "RecFormat2");
		}
	} else {
		dir = config_get_string(config, "SimpleOutput", "FilePath");
		if (!config_has_user_value(config, "SimpleOutput", "RecFormat2") &&
		    config_has_user_value(config, "SimpleOutput", "RecFormat")) {
			format = config_get_string(config, "SimpleOutput", "RecFormat");
		} else {
			format = config_get_string(config, "SimpleOutput", "RecFormat2");
		}
		const char *quality = config_get_string(config, "SimpleOutput", "RecQuality");
		if (strcmp(quality, "Lossless") == 0) {
			ffmpegOutput = true;
		}
	}

	if (record_advanced_settings) {
		bool use_native = strcmp(format, "hybrid_mp4") == 0;
		const char *output_id = use_native ? "mp4_output" : "ffmpeg_muxer";
		if (!recordOutput || strcmp(obs_output_get_id(recordOutput), output_id) != 0) {
			obs_output_release(recordOutput);
			recordOutput = obs_output_create(output_id, "vertical_canvas_record", nullptr, nullptr);
		}
	} else {
		obs_output_t *output = obs_frontend_get_recording_output();
		if (!output) {
			obs_output_t *replay_output = obs_frontend_get_replay_buffer_output();
			if (!replay_output) {
				ShowNoReplayOutputError();
				return;
			}
			obs_output_release(replay_output);
		}
		if (!recordOutput || strcmp(obs_output_get_id(recordOutput), obs_output_get_id(output)) != 0) {
			obs_output_release(recordOutput);
			recordOutput = obs_output_create(obs_output_get_id(output), "vertical_canvas_record", nullptr, nullptr);
		}

		obs_data_t *settings = obs_output_get_settings(output);
		obs_output_update(recordOutput, settings);
		obs_data_release(settings);
		obs_output_release(output);
	}

	SetRecordAudioEncoders(recordOutput);

	if (recordPath.empty() && (!dir || !strlen(dir))) {
		if (isVisible()) {
			QMessageBox::warning(this, QString::fromUtf8(obs_frontend_get_locale_string("Output.BadPath.Title")),
					     QString::fromUtf8(obs_module_text("RecordPathError")));
		}
		return;
	}

	const bool started_video = StartVideo();

	obs_output_set_video_encoder(recordOutput, GetRecordVideoEncoder());

	SetRecordAudioEncoders(recordOutput);

	signal_handler_t *signal = obs_output_get_signal_handler(recordOutput);
	signal_handler_disconnect(signal, "start", record_output_start, this);
	signal_handler_disconnect(signal, "stop", record_output_stop, this);
	signal_handler_disconnect(signal, "stopping", record_output_stopping, this);
	signal_handler_connect(signal, "start", record_output_start, this);
	signal_handler_connect(signal, "stop", record_output_stop, this);
	signal_handler_connect(signal, "stopping", record_output_stopping, this);

	std::string filenameFormat;
	if (record_advanced_settings) {
		if (filename_formatting.empty()) {
			filename_formatting = config_get_string(config, "Output", "FilenameFormatting");
			filename_formatting += "-vertical";
		}
		if (filename_formatting.empty()) {
			filename_formatting = "%CCYY-%MM-%DD %hh-%mm-%ss-vertical";
		}
		filenameFormat = filename_formatting;
	} else {
		filenameFormat = config_get_string(config, "Output", "FilenameFormatting");
		filenameFormat += "-vertical";
	}
	if (!format || !strlen(format))
		format = "mkv";
	std::string ext = format;
	if (ffmpegOutput)
		ext = "avi";
	else if (ext == "hybrid_mp4")
		ext = "mp4";
	else if (ext == "fragmented_mp4")
		ext = "mp4";
	else if (ext == "fragmented_mov")
		ext = "mov";
	else if (ext == "hls")
		ext = "m3u8";
	else if (ext == "mpegts")
		ext = "ts";
	obs_data_t *ps = obs_data_create();
	char path[512];
	char *filename = os_generate_formatted_filename(ext.c_str(), true, filenameFormat.c_str());
	if (recordPath.empty() && dir) {
		recordPath = dir;
	} else {
		dir = recordPath.c_str();
	}
	snprintf(path, 512, "%s/%s", dir, filename);
	bfree(filename);
	ensure_directory(path);
	obs_data_set_string(ps, ffmpegOutput ? "url" : "path", path);
	obs_data_set_string(ps, "path", path);
	obs_data_set_string(ps, "directory", dir);
	obs_data_set_string(ps, "format", filenameFormat.c_str());
	obs_data_set_string(ps, "extension", ext.c_str());
	obs_data_set_bool(ps, "split_file", true);
	obs_data_set_int(ps, "max_size_mb", max_size_mb);
	obs_data_set_int(ps, "max_time_sec", max_time_sec);
	obs_output_update(recordOutput, ps);
	obs_data_release(ps);

	SendVendorEvent("recording_starting");
	const bool success = obs_output_start(recordOutput);
	if (!success) {
		QMetaObject::invokeMethod(this, "OnRecordStop", Q_ARG(int, OBS_OUTPUT_ERROR),
					  Q_ARG(QString, QString::fromUtf8(obs_output_get_last_error(recordOutput))));
		if (started_video) {
			DestroyVideo();
		}
	}
}

void CanvasDock::StopRecord()
{
	recordButton->setChecked(false);
	if (obs_output_active(recordOutput)) {
		SendVendorEvent("recording_stopping");
		obs_output_stop(recordOutput);
	}
}

void CanvasDock::record_output_start(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("recording_started");
	d->CheckReplayBuffer(true);
	QMetaObject::invokeMethod(d, "OnRecordStart");
}

void CanvasDock::record_output_stop(void *data, calldata_t *calldata)
{
	const char *last_error = (const char *)calldata_ptr(calldata, "last_error");
	QString arg_last_error = QString::fromUtf8(last_error);
	const int code = (int)calldata_int(calldata, "code");
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("recording_stopped");
	QMetaObject::invokeMethod(d, "OnRecordStop", Q_ARG(int, code), Q_ARG(QString, arg_last_error));
}

void CanvasDock::record_output_stopping(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	auto d = static_cast<CanvasDock *>(data);
	d->CheckReplayBuffer();
}

void CanvasDock::replay_saved(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("backtrack_saved");
	QMetaObject::invokeMethod(d, "OnReplaySaved");
}

void CanvasDock::SetRecordAudioEncoders(obs_output_t *output)
{
	size_t idx = 0;
	if (record_advanced_settings) {
		for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
			if ((record_audio_tracks & (1ll << i)) == 0)
				continue;
			obs_encoder_t *aet = obs_output_get_audio_encoder(replayOutput, idx);
			if (!aet && recordOutput)
				aet = obs_output_get_audio_encoder(recordOutput, idx);
			if (aet && strcmp(obs_encoder_get_id(aet), "ffmpeg_aac") != 0)
				aet = nullptr;
			if (!aet) {
				std::string name = "vertical";
				name += std::to_string(idx);
				aet = obs_audio_encoder_create("ffmpeg_aac", name.c_str(), nullptr, i, nullptr);
				obs_encoder_set_audio(aet, obs_get_audio());
			}
			obs_output_set_audio_encoder(output, aet, idx);
			idx++;
		}
	} else {
		bool r = false;
		obs_output_t *main_output = obs_frontend_get_replay_buffer_output();
		if (!main_output) {
			r = true;
			main_output = obs_frontend_get_recording_output();
		}
		size_t mixers = obs_output_get_mixers(main_output);
		if (!mixers) {
			obs_output_t *record_output = obs_frontend_get_recording_output();
			if (record_output) {
				mixers = obs_output_get_mixers(record_output);
				obs_output_release(record_output);
			}
			if (!mixers) {
				config_t *config = obs_frontend_get_profile_config();
				const char *mode = config_get_string(config, "Output", "Mode");
				if (astrcmpi(mode, "Advanced") == 0) {
					const char *recType = config_get_string(config, "AdvOut", "RecType");
					if (astrcmpi(recType, "FFmpeg") == 0) {
						mixers = config_get_int(config, "AdvOut", "FFAudioMixes");
					} else {
						mixers = config_get_int(config, "AdvOut", "RecTracks");
					}

				} else {
					const char *quality = config_get_string(config, "SimpleOutput", "RecQuality");
					if (strcmp(quality, "Stream") == 0) {
						mixers = 1;
					} else {
						mixers = config_get_int(config, "SimpleOutput", "RecTracks");
					}
				}
				if (!mixers)
					mixers = 1;
			}
		}
		for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
			if ((mixers & (1ll << i)) == 0)
				continue;
			obs_encoder_t *aef = obs_output_get_audio_encoder(main_output, idx);
			if (!aef && idx == 0) {
				if (r) {
					obs_frontend_recording_start();
					obs_frontend_recording_stop();
				} else {
					obs_frontend_replay_buffer_start();
					obs_frontend_replay_buffer_stop();
				}
				aef = obs_output_get_audio_encoder(main_output, idx);
			}
			if (aef) {
				obs_encoder_t *aet = obs_output_get_audio_encoder(replayOutput, idx);
				if (!aet && recordOutput)
					aet = obs_output_get_audio_encoder(recordOutput, idx);
				if (aet && strcmp(obs_encoder_get_id(aef), obs_encoder_get_id(aet)) != 0)
					aet = nullptr;
				if (!aet) {
					std::string name = obs_encoder_get_name(aef);
					name += "_vertical";
					aet = obs_audio_encoder_create(obs_encoder_get_id(aef), name.c_str(), nullptr, i, nullptr);
					obs_encoder_set_audio(aet, obs_get_audio());
				}
				auto s = obs_encoder_get_settings(aef);
				obs_encoder_update(aet, s);
				obs_data_release(s);
				obs_output_set_audio_encoder(output, aet, idx);
				idx++;
			}
		}
		obs_output_release(main_output);
	}
	for (; idx < MAX_AUDIO_MIXES; idx++) {
		obs_output_set_audio_encoder(output, nullptr, idx);
	}
}

void CanvasDock::ShowNoReplayOutputError()
{
	config_t *config = obs_frontend_get_profile_config();
	const char *mode = config_get_string(config, "Output", "Mode");
	if (astrcmpi(mode, "Advanced") == 0) {
		const char *recType = config_get_string(config, "AdvOut", "RecType");
		if (astrcmpi(recType, "FFmpeg") == 0) {
			blog(LOG_WARNING, "[Vertical Canvas] error starting backtrack: custom ffmpeg");
			if (isVisible()) {
				QMessageBox::warning(this, QString::fromUtf8(obs_module_text("backtrackStartFail")),
						     QString::fromUtf8(obs_module_text("backtrackCustomFfmpeg")));
			}
			return;
		}
	}
	blog(LOG_WARNING, "[Vertical Canvas] error starting backtrack: no replay buffer found");
	if (isVisible()) {
		QMessageBox::warning(this, QString::fromUtf8(obs_module_text("backtrackStartFail")),
				     QString::fromUtf8(obs_module_text("backtrackNoReplayBuffer")));
	}
}

void CanvasDock::StartReplayBuffer()
{
	if (obs_output_active(replayOutput))
		return;

	if (record_advanced_settings) {
		if (!replayDuration)
			replayDuration = 5;
		auto s = obs_data_create();
		obs_data_set_int(s, "max_time_sec", replayDuration);
		obs_data_set_int(s, "max_size_mb", 0);
		if (filename_formatting.empty())
			filename_formatting = "%CCYY-%MM-%DD %hh-%mm-%ss";
		obs_data_set_string(s, "format", filename_formatting.c_str());
		replayFilename = filename_formatting;
		if (file_format.empty())
			file_format = "mkv";
		std::string ext = file_format;
		if (ext == "hybrid_mp4")
			ext = "mp4";
		else if (ext == "fragmented_mp4")
			ext = "mp4";
		else if (ext == "fragmented_mov")
			ext = "mov";
		else if (ext == "hls")
			ext = "m3u8";
		else if (ext == "mpegts")
			ext = "ts";
		obs_data_set_string(s, "extension", ext.c_str());
		//allow_spaces
		obs_data_set_string(s, "directory", replayPath.c_str());
		obs_output_update(replayOutput, s);
		obs_data_release(s);
	} else {
		obs_output_t *replay_output = obs_frontend_get_replay_buffer_output();
		if (!replay_output) {
			ShowNoReplayOutputError();
			return;
		}
		obs_encoder_t *enc = obs_output_get_video_encoder(replay_output);
		if (!enc) {
			obs_frontend_replay_buffer_start();
			obs_frontend_replay_buffer_stop();
			enc = obs_output_get_video_encoder(replay_output);
		}
		if (!enc) {
			obs_output_release(replay_output);
			blog(LOG_WARNING, "[Vertical Canvas] error starting backtrack: no video encoder found");
			return;
		}

		auto settings = obs_output_get_settings(replay_output);
		obs_output_release(replay_output);
		if (!strlen(obs_data_get_string(settings, "directory"))) {
			obs_frontend_replay_buffer_start();
			obs_frontend_replay_buffer_stop();
		}
		if (!replayDuration) {
			replayDuration = (uint32_t)obs_data_get_int(settings, "max_time_sec");
			if (!replayDuration)
				replayDuration = 5;
		}
		if (replayPath.empty())
			replayPath = obs_data_get_string(settings, "directory");
		obs_output_update(replayOutput, settings);
		if (obs_data_get_int(settings, "max_time_sec") != replayDuration) {
			const auto s = obs_output_get_settings(replayOutput);
			obs_data_set_int(s, "max_time_sec", replayDuration);
			obs_data_release(s);
		}
		if (obs_data_get_int(settings, "max_size_mb")) {
			const auto s = obs_output_get_settings(replayOutput);
			obs_data_set_int(s, "max_size_mb", 0);
			obs_data_release(s);
		}
		if (strcmp(replayPath.c_str(), obs_data_get_string(settings, "directory")) != 0) {
			const auto s = obs_output_get_settings(replayOutput);
			obs_data_set_string(s, "directory", replayPath.c_str());
			obs_data_release(s);
		}
		bool changed_format = false;
		std::string format = obs_data_get_string(settings, "format");
		size_t start_pos = format.find("Replay");
		if (start_pos != std::string::npos) {
			format.replace(start_pos, 6, "Backtrack");
			changed_format = true;
		}
		start_pos = format.find("replay");
		if (start_pos != std::string::npos) {
			format.replace(start_pos, 6, "backtrack");
			changed_format = true;
		}
		if (!changed_format) {
			format += "-backtrack";
			changed_format = true;
		}
		replayFilename = format;
		if (changed_format) {
			const auto s = obs_output_get_settings(replayOutput);
			obs_data_set_string(s, "format", format.c_str());
			obs_data_release(s);
		}
		obs_data_release(settings);
		obs_output_update(replayOutput, nullptr);
	}

	SetRecordAudioEncoders(replayOutput);

	bool started_video = StartVideo();

	bool enc_set = false;
	if (recordOutput) {
		auto re = obs_output_get_video_encoder(recordOutput);
		if (re && obs_encoder_active(re)) {
			obs_output_set_video_encoder(replayOutput, re);
			enc_set = true;
		}
	}
	auto profile_config = obs_frontend_get_profile_config();
	if (!enc_set && config_get_bool(profile_config, "Stream1", "EnableMultitrackVideo")) {
		auto canvas_id = config_get_string(profile_config, "Stream1", "MultitrackExtraCanvas");
		if (canvas_id && strcmp(canvas_id, obs_canvas_get_uuid(canvas)) == 0) {
			auto streaming_output = obs_frontend_get_streaming_output();
			if (streaming_output) {
				for (size_t idx = 0; idx < MAX_OUTPUT_VIDEO_ENCODERS; idx++) {
					auto enc = obs_output_get_video_encoder2(streaming_output, idx);
					if (enc && obs_encoder_active(enc) && obs_encoder_video(enc) == obs_canvas_get_video(canvas)) {
						obs_output_set_video_encoder(replayOutput, enc);
						enc_set = true;
						break;
					}
				}
				obs_output_release(streaming_output);
			}
		}
	}

	if (!enc_set)
		obs_output_set_video_encoder(replayOutput, GetRecordVideoEncoder());

	signal_handler_t *signal = obs_output_get_signal_handler(replayOutput);
	signal_handler_disconnect(signal, "start", replay_output_start, this);
	signal_handler_disconnect(signal, "stop", replay_output_stop, this);
	signal_handler_connect(signal, "start", replay_output_start, this);
	signal_handler_connect(signal, "stop", replay_output_stop, this);

	SendVendorEvent("backtrack_starting");

	const bool success = obs_output_start(replayOutput);
	if (!success) {
		QMetaObject::invokeMethod(this, "OnReplayBufferStop", Q_ARG(int, OBS_OUTPUT_ERROR),
					  Q_ARG(QString, QString::fromUtf8(obs_output_get_last_error(replayOutput))));
		if (started_video) {
			DestroyVideo();
		}
	} else {
		QMetaObject::invokeMethod(this, "OnReplayBufferStart");
	}
}

#define SIMPLE_ENCODER_X264 "x264"
#define SIMPLE_ENCODER_X264_LOWCPU "x264_lowcpu"
#define SIMPLE_ENCODER_QSV "qsv"
#define SIMPLE_ENCODER_QSV_AV1 "qsv_av1"
#define SIMPLE_ENCODER_NVENC "nvenc"
#define SIMPLE_ENCODER_NVENC_AV1 "nvenc_av1"
#define SIMPLE_ENCODER_NVENC_HEVC "nvenc_hevc"
#define SIMPLE_ENCODER_AMD "amd"
#define SIMPLE_ENCODER_AMD_HEVC "amd_hevc"
#define SIMPLE_ENCODER_AMD_AV1 "amd_av1"
#define SIMPLE_ENCODER_APPLE_H264 "apple_h264"
#define SIMPLE_ENCODER_APPLE_HEVC "apple_hevc"

bool EncoderAvailable(const char *encoder)
{
	const char *val;
	int i = 0;

	while (obs_enum_encoder_types(i++, &val))
		if (strcmp(val, encoder) == 0)
			return true;

	return false;
}

const char *get_simple_output_encoder(const char *encoder)
{
	if (strcmp(encoder, SIMPLE_ENCODER_X264) == 0)
		return "obs_x264";
	if (strcmp(encoder, SIMPLE_ENCODER_X264_LOWCPU) == 0)
		return "obs_x264";
	if (strcmp(encoder, SIMPLE_ENCODER_QSV) == 0)
		return "obs_qsv11_v2";
	if (strcmp(encoder, SIMPLE_ENCODER_QSV_AV1) == 0)
		return "obs_qsv11_av1";
	if (strcmp(encoder, SIMPLE_ENCODER_AMD) == 0)
		return "h264_texture_amf";
	if (strcmp(encoder, SIMPLE_ENCODER_AMD_HEVC) == 0)
		return "h265_texture_amf";
	if (strcmp(encoder, SIMPLE_ENCODER_AMD_AV1) == 0)
		return "av1_texture_amf";
	if (strcmp(encoder, SIMPLE_ENCODER_NVENC) == 0)
		return EncoderAvailable("obs_nvenc_h264_tex") ? "obs_nvenc_h264_tex"
							      : (EncoderAvailable("jim_nvenc") ? "jim_nvenc" : "ffmpeg_nvenc");
	if (strcmp(encoder, SIMPLE_ENCODER_NVENC_HEVC) == 0)
		return EncoderAvailable("obs_nvenc_hevc_tex")
			       ? "obs_nvenc_hevc_tex"
			       : (EncoderAvailable("jim_hevc_nvenc") ? "jim_hevc_nvenc" : "ffmpeg_hevc_nvenc");
	if (strcmp(encoder, SIMPLE_ENCODER_NVENC_AV1) == 0)
		return EncoderAvailable("obs_nvenc_av1_tex") ? "obs_nvenc_av1_tex" : "jim_av1_nvenc";
	if (strcmp(encoder, SIMPLE_ENCODER_APPLE_H264) == 0)
		return "com.apple.videotoolbox.videoencoder.ave.avc";
	if (strcmp(encoder, SIMPLE_ENCODER_APPLE_HEVC) == 0)
		return "com.apple.videotoolbox.videoencoder.ave.hevc";
	return "obs_x264";
}

obs_encoder_t *CanvasDock::GetStreamVideoEncoder()
{
	obs_encoder_t *video_encoder = nullptr;
	const char *enc_id = nullptr;
	obs_data_t *video_settings = nullptr;
	bool useRecordEncoder = false;

	config_t *config = obs_frontend_get_profile_config();
	const char *mode = config_get_string(config, "Output", "Mode");

	if (stream_advanced_settings) {
		video_settings = stream_encoder_settings;
		obs_data_addref(video_settings);
		enc_id = stream_encoder.c_str();
		if (record_advanced_settings) {
			useRecordEncoder = record_encoder.empty();
		} else if (strcmp(mode, "Advanced") == 0) {
			const char *recordEncoder = config_get_string(config, "AdvOut", "RecEncoder");
			useRecordEncoder = astrcmpi(recordEncoder, "none") == 0;
		} else {
			const char *quality = config_get_string(config, "SimpleOutput", "RecQuality");
			if (strcmp(quality, "Stream") == 0) {
				useRecordEncoder = true;
			}
		}
	} else {
		if (config_get_bool(config, "Stream1", "EnableMultitrackVideo")) {
			auto canvas_id = config_get_string(config, "Stream1", "MultitrackExtraCanvas");
			if (canvas_id && strcmp(canvas_id, obs_canvas_get_uuid(canvas)) == 0) {
				auto streaming_output = obs_frontend_get_streaming_output();
				if (streaming_output) {
					for (size_t idx = 0; idx < MAX_OUTPUT_VIDEO_ENCODERS; idx++) {
						auto enc = obs_output_get_video_encoder2(streaming_output, idx);
						if (enc && obs_encoder_active(enc) && obs_encoder_video(enc) == obs_canvas_get_video(canvas)) {
							obs_output_release(streaming_output);
							return enc;
						}
					}
					obs_output_release(streaming_output);
				}
			}
		}
		if (strcmp(mode, "Advanced") == 0) {
			video_settings = GetDataFromJsonFile("streamEncoder.json");
			enc_id = config_get_string(config, "AdvOut", "Encoder");
			const char *recordEncoder = config_get_string(config, "AdvOut", "RecEncoder");
			useRecordEncoder = astrcmpi(recordEncoder, "none") == 0;
			if (!streamingVideoBitrate) {
				streamingVideoBitrate = (uint32_t)obs_data_get_int(video_settings, "bitrate");
			} else {
				obs_data_set_int(video_settings, "bitrate", streamingVideoBitrate);
			}
		} else {
			video_settings = obs_data_create();
			bool advanced = config_get_bool(config, "SimpleOutput", "UseAdvanced");
			enc_id = get_simple_output_encoder(config_get_string(config, "SimpleOutput", "StreamEncoder"));
			const char *presetType;
			const char *preset;
			if (strcmp(enc_id, SIMPLE_ENCODER_QSV) == 0) {
				presetType = "QSVPreset";

			} else if (strcmp(enc_id, SIMPLE_ENCODER_QSV_AV1) == 0) {
				presetType = "QSVPreset";

			} else if (strcmp(enc_id, SIMPLE_ENCODER_AMD) == 0) {
				presetType = "AMDPreset";

			} else if (strcmp(enc_id, SIMPLE_ENCODER_AMD_HEVC) == 0) {
				presetType = "AMDPreset";

			} else if (strcmp(enc_id, SIMPLE_ENCODER_NVENC) == 0) {
				presetType = "NVENCPreset2";
			} else if (strcmp(enc_id, SIMPLE_ENCODER_NVENC_HEVC) == 0) {
				presetType = "NVENCPreset2";

			} else if (strcmp(enc_id, SIMPLE_ENCODER_AMD_AV1) == 0) {
				presetType = "AMDAV1Preset";

			} else if (strcmp(enc_id, SIMPLE_ENCODER_NVENC_AV1) == 0) {
				presetType = "NVENCPreset2";

			} else {
				presetType = "Preset";
			}

			preset = config_get_string(config, "SimpleOutput", presetType);
			obs_data_set_string(video_settings, (strcmp(presetType, "NVENCPreset2") == 0) ? "preset2" : "preset",
					    preset);

			obs_data_set_string(video_settings, "rate_control", "CBR");
			if (!streamingVideoBitrate) {
				const int sVideoBitrate = (int)config_get_uint(config, "SimpleOutput", "VBitrate");
				obs_data_set_int(video_settings, "bitrate", sVideoBitrate);
				streamingVideoBitrate = sVideoBitrate;
			} else {
				obs_data_set_int(video_settings, "bitrate", streamingVideoBitrate);
			}

			if (advanced) {
				const char *custom = config_get_string(config, "SimpleOutput", "x264Settings");
				obs_data_set_string(video_settings, "x264opts", custom);
			}

			const char *quality = config_get_string(config, "SimpleOutput", "RecQuality");
			if (strcmp(quality, "Stream") == 0) {
				useRecordEncoder = true;
			}
		}
	}

	obs_encoder_t *se = nullptr;
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		se = obs_output_get_video_encoder(it->output);
		if (se)
			break;
	}
	if (se && strcmp(enc_id, obs_encoder_get_id(se)) == 0) {
		video_encoder = se;
	}
	if (!video_encoder && useRecordEncoder && recordOutput) {
		auto re = obs_output_get_video_encoder(recordOutput);
		if (re && strcmp(enc_id, obs_encoder_get_id(re)) == 0) {
			video_encoder = re;
		}
	}
	if (!video_encoder && useRecordEncoder && replayOutput) {
		auto re = obs_output_get_video_encoder(replayOutput);
		if (re && strcmp(enc_id, obs_encoder_get_id(re)) == 0) {
			video_encoder = re;
		}
	}
	if (!video_encoder) {
		video_encoder = obs_video_encoder_create(enc_id, "vertical_canvas_video_encoder", nullptr, nullptr);
	}

	obs_encoder_update(video_encoder, video_settings);
	obs_data_release(video_settings);

	switch (video_output_get_format(obs_canvas_get_video(canvas))) {
	case VIDEO_FORMAT_I420:
	case VIDEO_FORMAT_NV12:
	case VIDEO_FORMAT_I010:
	case VIDEO_FORMAT_P010:
		break;
	default:
		obs_encoder_set_preferred_video_format(video_encoder, VIDEO_FORMAT_NV12);
	}
	if (!obs_encoder_active(video_encoder))
		obs_encoder_set_video(video_encoder, obs_canvas_get_video(canvas));
	return video_encoder;
}

obs_encoder_t *CanvasDock::GetRecordVideoEncoder()
{
	obs_encoder_t *video_encoder = nullptr;
	const char *enc_id = nullptr;
	obs_data_t *settings = nullptr;
	if (record_advanced_settings) {
		if (record_encoder.empty()) {
			return GetStreamVideoEncoder();
		} else {
			enc_id = record_encoder.c_str();
			settings = record_encoder_settings;
			obs_data_addref(settings);
		}
	} else {
		config_t *config = obs_frontend_get_profile_config();
		const char *mode = config_get_string(config, "Output", "Mode");
		if (strcmp(mode, "Advanced") == 0) {
			if (astrcmpi(config_get_string(config, "AdvOut", "RecEncoder"), "none") == 0)
				return GetStreamVideoEncoder();
			enc_id = config_get_string(config, "AdvOut", "RecEncoder");
		} else {
			if (strcmp(config_get_string(config, "SimpleOutput", "RecQuality"), "Stream") == 0)
				return GetStreamVideoEncoder();
			enc_id = get_simple_output_encoder(config_get_string(config, "SimpleOutput", "RecEncoder"));
		}
	}

	if (!video_encoder && replayOutput) {
		auto re = obs_output_get_video_encoder(replayOutput);
		if (re && strcmp(enc_id, obs_encoder_get_id(re)) == 0) {
			video_encoder = re;
		}
	}
	if (!video_encoder && recordOutput) {
		auto re = obs_output_get_video_encoder(recordOutput);
		if (re && strcmp(enc_id, obs_encoder_get_id(re)) == 0) {
			video_encoder = re;
		}
	}
	if (!video_encoder) {
		video_encoder = obs_video_encoder_create(enc_id, "vertical_canvas_record_video_encoder", nullptr, nullptr);
	}

	obs_encoder_update(video_encoder, settings);
	obs_data_release(settings);

	if (!record_advanced_settings) {
		obs_output_t *main_output = obs_frontend_get_replay_buffer_output();
		if (!main_output)
			main_output = obs_frontend_get_recording_output();
		auto enc = obs_output_get_video_encoder(main_output);
		obs_output_release(main_output);
		obs_data_t *d = obs_encoder_get_settings(enc);
		obs_encoder_update(video_encoder, d);
		if (!recordVideoBitrate) {
			recordVideoBitrate = (uint32_t)obs_data_get_int(d, "bitrate");
		} else {
			auto s = obs_encoder_get_settings(video_encoder);
			if (recordVideoBitrate != obs_data_get_int(s, "bitrate")) {
				obs_data_set_int(s, "bitrate", recordVideoBitrate);
				obs_encoder_update(video_encoder, nullptr);
			}
			obs_data_release(s);
		}
		obs_data_release(d);
	}
	if (!video_output_stopped(obs_canvas_get_video(canvas))) {
		switch (video_output_get_format(obs_canvas_get_video(canvas))) {
		case VIDEO_FORMAT_I420:
		case VIDEO_FORMAT_NV12:
		case VIDEO_FORMAT_I010:
		case VIDEO_FORMAT_P010:
			break;
		default:
			obs_encoder_set_preferred_video_format(video_encoder, VIDEO_FORMAT_NV12);
		}
		if (!obs_encoder_active(video_encoder))
			obs_encoder_set_video(video_encoder, obs_canvas_get_video(canvas));
	}
	return video_encoder;
}

void CanvasDock::StopReplayBuffer()
{
	QMetaObject::invokeMethod(this, "OnReplayBufferStop", Q_ARG(int, OBS_OUTPUT_SUCCESS),
				  Q_ARG(QString, QString::fromUtf8("")));
	if (obs_output_active(replayOutput)) {
		SendVendorEvent("backtrack_stopping");
		obs_output_stop(replayOutput);
	}
}

void CanvasDock::replay_output_start(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	UNUSED_PARAMETER(data);
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("backtrack_started");
	QMetaObject::invokeMethod(d, "OnReplayBufferStart");
}

void CanvasDock::replay_output_stop(void *data, calldata_t *calldata)
{
	const char *last_error = (const char *)calldata_ptr(calldata, "last_error");
	QString arg_last_error = QString::fromUtf8(last_error);
	const int code = (int)calldata_int(calldata, "code");
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("backtrack_stopped");
	QMetaObject::invokeMethod(d, "OnReplayBufferStop", Q_ARG(int, code), Q_ARG(QString, arg_last_error));
}

void CanvasDock::StreamButtonClicked()
{
	int active_count = 0;
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		if (obs_output_active(it->output))
			active_count++;
	}
	if (active_count > 0) {
		StopStream();
		return;
	}
	StartStream();
}

void CanvasDock::StreamButtonMultiMenu(QMenu *menu)
{
	int active_count = 0;
	menu->clear();
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		bool active = obs_output_active(it->output);
		if (active)
			active_count++;
		if (!it->enabled && !active)
			continue;
		auto a = menu->addAction(QString::fromUtf8(it->name.empty() ? it->stream_server : it->name));
		a->setCheckable(true);
		a->setChecked(active);
		auto output = it->output;
		if (active) {
			connect(a, &QAction::triggered, [output] { obs_output_stop(output); });
		} else {
			connect(a, &QAction::triggered, [this, it] { StartStreamOutput(it); });
		}
	}
	menu->addSeparator();
	if (active_count == 0) {
		menu->addAction(QString::fromUtf8(obs_module_text("StartAll")), [this] { StartStream(); });
	} else {
		menu->addAction(QString::fromUtf8(obs_module_text("StopAll")), [this] { StopStream(); });
	}
}

void CanvasDock::StartStreamOutput(std::vector<StreamServer>::iterator it)
{
	CreateStreamOutput(it);
	const bool started_video = StartVideo();
	if (it->settings && obs_data_get_bool(it->settings, "advanced") && obs_get_module("aitum-multistream")) {
		blog(LOG_INFO, "[Vertical Canvas] Start output '%s' with multistream advanced settings", it->name.c_str());
		auto venc_name = obs_data_get_string(it->settings, "video_encoder");
		if (!venc_name || venc_name[0] == '\0') {
			//use main encoder
			obs_output_set_video_encoder(it->output, GetStreamVideoEncoder());
		} else {
			obs_data_t *s = nullptr;
			auto ves = obs_data_get_obj(it->settings, "video_encoder_settings");
			if (ves) {
				s = obs_data_create();
				obs_data_apply(s, ves);
				obs_data_release(ves);
			}
			std::string video_encoder_name = "vertical_canvas_video_encoder_";
			video_encoder_name += it->name;
			auto venc = obs_video_encoder_create(venc_name, video_encoder_name.c_str(), s, nullptr);
			obs_data_release(s);
			obs_encoder_set_video(venc, obs_canvas_get_video(canvas));
			auto divisor = obs_data_get_int(it->settings, "frame_rate_divisor");
			bool scale = obs_data_get_bool(it->settings, "scale");
			void *handle = nullptr;
			if (scale || divisor > 1)
#ifdef _WIN32
				handle = os_dlopen("obs");
#else
				handle = dlopen(nullptr, RTLD_LAZY);
#endif
			if (divisor > 1) {
				auto func =
					(bool (*)(obs_encoder_t *, uint32_t))os_dlsym(handle, "obs_encoder_set_frame_rate_divisor");
				if (func)
					func(venc, (uint32_t)divisor);
				//obs_encoder_set_frame_rate_divisor(venc, (uint32_t)divisor);
			}
			if (scale) {
				auto func = (void (*)(obs_encoder_t *, uint32_t, uint32_t))os_dlsym(handle,
												    "obs_encoder_set_scaled_size");
				if (func)
					func(venc, (uint32_t)obs_data_get_int(it->settings, "width"),
					     (uint32_t)obs_data_get_int(it->settings, "height"));
				//obs_encoder_set_scaled_size(venc, (uint32_t)obs_data_get_int(it->settings, "width"), (uint32_t)obs_data_get_int(it->settings, "height"));
				auto func2 = (void (*)(obs_encoder_t *, obs_scale_type))os_dlsym(handle,
												 "obs_encoder_set_gpu_scale_type");
				if (func2)
					func2(venc, (obs_scale_type)obs_data_get_int(it->settings, "scale_type"));
				//obs_encoder_set_gpu_scale_type(venc, (obs_scale_type)obs_data_get_int(it->settings, "scale_type"));
			}
			if (handle)
				os_dlclose(handle);
			obs_output_set_video_encoder(it->output, venc);
		}
		auto aenc_name = obs_data_get_string(it->settings, "audio_encoder");
		if (!aenc_name || aenc_name[0] == '\0') {
			//use main encoder
			obs_output_set_audio_encoder(it->output, GetStreamAudioEncoder(), 0);
		} else {
			obs_data_t *s = nullptr;
			auto aes = obs_data_get_obj(it->settings, "audio_encoder_settings");
			if (aes) {
				s = obs_data_create();
				obs_data_apply(s, aes);
				obs_data_release(aes);
			}
			std::string audio_encoder_name = "vertical_canvas_audio_encoder_";
			audio_encoder_name += it->name;
			auto aenc = obs_audio_encoder_create(aenc_name, audio_encoder_name.c_str(), s,
							     obs_data_get_int(it->settings, "audio_track"), nullptr);
			obs_data_release(s);
			obs_encoder_set_audio(aenc, obs_get_audio());
			obs_output_set_audio_encoder(it->output, aenc, 0);
		}
	} else {
		blog(LOG_INFO, "[Vertical Canvas] Start output '%s'", it->name.c_str());
		obs_output_set_video_encoder(it->output, GetStreamVideoEncoder());
		obs_output_set_audio_encoder(it->output, GetStreamAudioEncoder(), 0);
	}
	it->stopping = false;
	if (!obs_output_start(it->output)) {
		if (started_video) {
			DestroyVideo();
		}
		it->stopping = true;
		QMetaObject::invokeMethod(this, "OnStreamStop", Q_ARG(int, OBS_OUTPUT_ERROR),
					  Q_ARG(QString, QString::fromUtf8(obs_output_get_last_error(it->output))),
					  Q_ARG(QString, QString::fromUtf8(it->stream_server)),
					  Q_ARG(QString, QString::fromUtf8(it->stream_key)));
	}
}

void CanvasDock::CreateStreamOutput(std::vector<StreamServer>::iterator it)
{
	auto s = obs_data_create();
	obs_data_set_string(s, "server", it->stream_server.c_str());
	obs_data_set_string(s, "key", it->stream_key.c_str());
	obs_data_set_string(s, "bearer_token", it->stream_key.c_str());
	//use_auth
	//username
	//password
	obs_service_update(it->service, s);
	obs_data_release(s);
	const char *type = nullptr;
#ifdef _WIN32
	auto handle = os_dlopen("obs");
#else
	auto handle = dlopen(nullptr, RTLD_LAZY);
#endif
	if (handle) {
		auto type_func = (const char *(*)(obs_service_t *))os_dlsym(handle, "obs_service_get_output_type");
		if (!type_func)
			type_func = (const char *(*)(obs_service_t *))os_dlsym(handle, "obs_service_get_preferred_output_type");
		if (type_func) {
			type = type_func(it->service);
		}
		if (!type) {
			const char *url = nullptr;
			auto url_func = (const char *(*)(obs_service_t *))os_dlsym(handle, "obs_service_get_url");
			if (url_func) {
				url = url_func(it->service);
			} else {
				auto info_func = (const char *(*)(obs_service_t *,
								  uint32_t))os_dlsym(handle, "obs_service_get_connect_info");
				if (info_func)
					url = info_func(it->service, 0); // OBS_SERVICE_CONNECT_INFO_SERVER_URL
			}
			type = "rtmp_output";
			if (url != nullptr && strncmp(url, "ftl", 3) == 0) {
				type = "ftl_output";
			} else if (url != nullptr && strncmp(url, "rtmp", 4) != 0) {
				type = "ffmpeg_mpegts_muxer";
			}
		}
		os_dlclose(handle);
	} else {
		type = "rtmp_output";
	}
	if (!it->output || strcmp(type, obs_output_get_id(it->output)) != 0) {
		if (it->output) {
			if (obs_output_active(it->output))
				obs_output_stop(it->output);
			obs_output_release(it->output);
		}
		std::string name = "vertical_canvas_stream";
		if (!it->name.empty()) {
			name += "_";
			name += it->name;
		}
		it->output = obs_output_create(type, name.c_str(), nullptr, nullptr);
		obs_output_set_service(it->output, it->service);
	}
	config_t *config = obs_frontend_get_profile_config();
	if (config) {
		OBSDataAutoRelease output_settings = obs_data_create();
		obs_data_set_string(output_settings, "bind_ip", config_get_string(config, "Output", "BindIP"));
		obs_data_set_string(output_settings, "ip_family", config_get_string(config, "Output", "IPFamily"));
		obs_output_update(it->output, output_settings);
	}

	if (stream_advanced_settings) {
		obs_output_set_delay(it->output, stream_delay_enabled ? stream_delay_duration : 0,
				     stream_delay_preserve ? OBS_OUTPUT_DELAY_PRESERVE : 0);
	} else {
		bool useDelay = config_get_bool(config, "Output", "DelayEnable");
		int delaySec = (int)config_get_int(config, "Output", "DelaySec");
		bool preserveDelay = config_get_bool(config, "Output", "DelayPreserve");
		obs_output_set_delay(it->output, useDelay ? delaySec : 0, preserveDelay ? OBS_OUTPUT_DELAY_PRESERVE : 0);
	}

	signal_handler_t *signal = obs_output_get_signal_handler(it->output);
	signal_handler_disconnect(signal, "start", stream_output_start, this);
	signal_handler_disconnect(signal, "stop", stream_output_stop, this);
	signal_handler_connect(signal, "start", stream_output_start, this);
	signal_handler_connect(signal, "stop", stream_output_stop, this);
}

obs_encoder_t *CanvasDock::GetStreamAudioEncoder()
{
	const auto audio_settings = obs_data_create();

	config_t *config = obs_frontend_get_profile_config();
	const char *mode = config_get_string(config, "Output", "Mode");
	size_t mix_idx = 0;

	if (stream_advanced_settings) {
		if (stream_audio_track > 0)
			mix_idx = stream_audio_track - 1;

		if (record_advanced_settings) {
			obs_data_set_int(audio_settings, "bitrate", audioBitrate);
		} else if (strcmp(mode, "Advanced") == 0) {
			if (!audioBitrate) {
				uint64_t streamTrack = config_get_uint(config, "AdvOut", "TrackIndex");
				static const char *trackNames[] = {
					"Track1Bitrate", "Track2Bitrate", "Track3Bitrate",
					"Track4Bitrate", "Track5Bitrate", "Track6Bitrate",
				};
				const int advAudioBitrate = (int)config_get_uint(config, "AdvOut", trackNames[streamTrack - 1]);
				obs_data_set_int(audio_settings, "bitrate", advAudioBitrate);
				audioBitrate = advAudioBitrate;
			} else {
				obs_data_set_int(audio_settings, "bitrate", audioBitrate);
			}
		} else {
			if (!audioBitrate) {
				const int sAudioBitrate = (int)config_get_uint(config, "SimpleOutput", "ABitrate");
				obs_data_set_int(audio_settings, "bitrate", sAudioBitrate);
				audioBitrate = sAudioBitrate;
			} else {
				obs_data_set_int(audio_settings, "bitrate", audioBitrate);
			}
		}
	} else if (strcmp(mode, "Advanced") == 0) {
		mix_idx = config_get_uint(config, "AdvOut", "TrackIndex") - 1;
		if (!audioBitrate) {
			static const char *trackNames[] = {
				"Track1Bitrate", "Track2Bitrate", "Track3Bitrate",
				"Track4Bitrate", "Track5Bitrate", "Track6Bitrate",
			};
			const int advAudioBitrate = (int)config_get_uint(config, "AdvOut", trackNames[mix_idx]);
			obs_data_set_int(audio_settings, "bitrate", advAudioBitrate);
			audioBitrate = advAudioBitrate;
		} else {
			obs_data_set_int(audio_settings, "bitrate", audioBitrate);
		}
	} else {
		obs_data_set_string(audio_settings, "rate_control", "CBR");
		if (!audioBitrate) {
			const int sAudioBitrate = (int)config_get_uint(config, "SimpleOutput", "ABitrate");
			obs_data_set_int(audio_settings, "bitrate", sAudioBitrate);
			audioBitrate = sAudioBitrate;
		} else {
			obs_data_set_int(audio_settings, "bitrate", audioBitrate);
		}
	}
	obs_encoder_t *audio_encoder = nullptr;
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		if (!audio_encoder)
			audio_encoder = obs_output_get_audio_encoder(it->output, 0);
	}
	if (!audio_encoder) {
		audio_encoder =
			obs_audio_encoder_create("ffmpeg_aac", "vertical_canvas_audio_encoder", audio_settings, mix_idx, nullptr);
		obs_encoder_set_audio(audio_encoder, obs_get_audio());
		for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it)
			obs_output_set_audio_encoder(it->output, audio_encoder, 0);
	} else {
		obs_encoder_update(audio_encoder, audio_settings);
	}
	obs_data_release(audio_settings);
	return audio_encoder;
}

void CanvasDock::StartStream()
{
	bool to_start = false;
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		if (obs_output_active(it->output)) {
			return;
		}
		if (it->enabled)
			to_start = true;
	}
	if (!to_start) {
		blog(LOG_WARNING, "[Vertical Canvas] No stream output to start");
		QMetaObject::invokeMethod(this, "OnStreamStop", Q_ARG(int, OBS_OUTPUT_SUCCESS),
					  Q_ARG(QString, QString::fromUtf8("")), Q_ARG(QString, QString::fromUtf8("")),
					  Q_ARG(QString, QString::fromUtf8("")));
		if (isVisible()) {
			QMessageBox::information(this, QString::fromUtf8(obs_module_text("NoOutputServer")),
						 QString::fromUtf8(obs_module_text("NoOutputServerWarning")));
		}
		return;
	}

	obs_encoder_t *video_encoder = nullptr;
	obs_encoder_t *audio_encoder = nullptr;
	const bool started_video = StartVideo();
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		if (!it->enabled)
			continue;
		auto s = obs_data_create();
		obs_data_set_string(s, "server", it->stream_server.c_str());
		obs_data_set_string(s, "key", it->stream_key.c_str());
		obs_data_set_string(s, "bearer_token", it->stream_key.c_str());
		//use_auth
		//username
		//password
		obs_service_update(it->service, s);
		obs_data_release(s);

		CreateStreamOutput(it);
		if (it->settings && obs_data_get_bool(it->settings, "advanced") && obs_get_module("aitum-multistream")) {
			blog(LOG_INFO, "[Vertical Canvas] Start output '%s' with multistream advanced settings", it->name.c_str());
			auto venc_name = obs_data_get_string(it->settings, "video_encoder");
			if (!venc_name || venc_name[0] == '\0') {
				//use main encoder
				if (!video_encoder)
					video_encoder = GetStreamVideoEncoder();
				obs_output_set_video_encoder(it->output, video_encoder);
			} else {
				obs_data_t *ves_apply = nullptr;
				auto ves = obs_data_get_obj(it->settings, "video_encoder_settings");
				if (ves) {
					ves_apply = obs_data_create();
					obs_data_apply(ves_apply, ves);
					obs_data_release(ves);
				}
				std::string video_encoder_name = "vertical_canvas_video_encoder_";
				video_encoder_name += it->name;
				auto venc = obs_video_encoder_create(venc_name, video_encoder_name.c_str(), ves_apply, nullptr);
				obs_data_release(ves_apply);
				obs_encoder_set_video(venc, obs_canvas_get_video(canvas));
				auto divisor = obs_data_get_int(it->settings, "frame_rate_divisor");
				bool scale = obs_data_get_bool(it->settings, "scale");
				void *handle = nullptr;
				if (scale || divisor > 1)
#ifdef _WIN32
					handle = os_dlopen("obs");
#else
					handle = dlopen(nullptr, RTLD_LAZY);
#endif
				if (divisor > 1) {
					auto func = (bool (*)(obs_encoder_t *,
							      uint32_t))os_dlsym(handle, "obs_encoder_set_frame_rate_divisor");
					if (func)
						func(venc, (uint32_t)divisor);
					//obs_encoder_set_frame_rate_divisor(venc, (uint32_t)divisor);
				}
				if (scale) {
					auto func = (void (*)(obs_encoder_t *, uint32_t,
							      uint32_t))os_dlsym(handle, "obs_encoder_set_scaled_size");
					if (func)
						func(venc, (uint32_t)obs_data_get_int(it->settings, "width"),
						     (uint32_t)obs_data_get_int(it->settings, "height"));
					//obs_encoder_set_scaled_size(venc, (uint32_t)obs_data_get_int(it->settings, "width"), (uint32_t)obs_data_get_int(it->settings, "height"));
					auto func2 = (void (*)(obs_encoder_t *,
							       obs_scale_type))os_dlsym(handle, "obs_encoder_set_gpu_scale_type");
					if (func2)
						func2(venc, (obs_scale_type)obs_data_get_int(it->settings, "scale_type"));
					//obs_encoder_set_gpu_scale_type(venc, (obs_scale_type)obs_data_get_int(it->settings, "scale_type"));
				}
				if (handle)
					os_dlclose(handle);
				obs_output_set_video_encoder(it->output, venc);
			}
			auto aenc_name = obs_data_get_string(it->settings, "audio_encoder");
			if (!aenc_name || aenc_name[0] == '\0') {
				//use main encoder
				if (!audio_encoder)
					audio_encoder = GetStreamAudioEncoder();
				obs_output_set_audio_encoder(it->output, audio_encoder, 0);
			} else {
				obs_data_t *aes_apply = nullptr;
				auto aes = obs_data_get_obj(it->settings, "audio_encoder_settings");
				if (aes) {
					aes_apply = obs_data_create();
					obs_data_apply(aes_apply, aes);
					obs_data_release(aes);
				}
				std::string audio_encoder_name = "vertical_canvas_audio_encoder_";
				audio_encoder_name += it->name;
				auto aenc = obs_audio_encoder_create(aenc_name, audio_encoder_name.c_str(), aes_apply,
								     obs_data_get_int(it->settings, "audio_track"), nullptr);
				obs_data_release(aes_apply);
				obs_encoder_set_audio(aenc, obs_get_audio());
				obs_output_set_audio_encoder(it->output, aenc, 0);
			}
		} else {
			blog(LOG_INFO, "[Vertical Canvas] Start output '%s'", it->name.c_str());
			if (!video_encoder)
				video_encoder = GetStreamVideoEncoder();
			obs_output_set_video_encoder(it->output, video_encoder);
			if (!audio_encoder)
				audio_encoder = GetStreamAudioEncoder();
			obs_output_set_audio_encoder(it->output, audio_encoder, 0);
		}
	}

	SendVendorEvent("streaming_starting");

	config_t *config = obs_frontend_get_profile_config();
	bool success = false;
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		if (!it->enabled)
			continue;
		if (config) {
			OBSDataAutoRelease output_settings = obs_data_create();
			obs_data_set_string(output_settings, "bind_ip", config_get_string(config, "Output", "BindIP"));
			obs_data_set_string(output_settings, "ip_family", config_get_string(config, "Output", "IPFamily"));
			obs_output_update(it->output, output_settings);
		}
		it->stopping = false;
		if (obs_output_start(it->output)) {
			success = true;
		} else {
			it->stopping = true;
			QMetaObject::invokeMethod(this, "OnStreamStop", Q_ARG(int, OBS_OUTPUT_ERROR),
						  Q_ARG(QString, QString::fromUtf8(obs_output_get_last_error(it->output))),
						  Q_ARG(QString, QString::fromUtf8(it->stream_server)),
						  Q_ARG(QString, QString::fromUtf8(it->stream_key)));
		}
	}
	if (!success && started_video) {
		obs_canvas_set_channel(canvas, 0, nullptr);
	}
}

void CanvasDock::StopStream()
{
	streamButton->setChecked(false);
	bool done = false;
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		if (obs_output_active(it->output)) {
			obs_output_stop(it->output);
			done = true;
		}
	}
	if (done)
		SendVendorEvent("streaming_stopping");
	CheckReplayBuffer();
}

void CanvasDock::stream_output_start(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("streaming_started");
	d->CheckReplayBuffer(true);
	QMetaObject::invokeMethod(d, "OnStreamStart");
}

void CanvasDock::stream_output_stop(void *data, calldata_t *calldata)
{
	const char *last_error = (const char *)calldata_ptr(calldata, "last_error");
	QString arg_last_error = QString::fromUtf8(last_error);
	const int code = (int)calldata_int(calldata, "code");
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("streaming_stopped");
	QString stream_server;
	QString stream_key;
	obs_output_t *t = (obs_output_t *)calldata_ptr(calldata, "output");
	for (auto it = d->streamOutputs.begin(); it != d->streamOutputs.end(); ++it) {
		if (it->output == t) {
			stream_server = QString::fromUtf8(it->stream_server);
			stream_key = QString::fromUtf8(it->stream_key);
			it->stopping = true;
		}
	}

	QMetaObject::invokeMethod(d, "OnStreamStop", Q_ARG(int, code), Q_ARG(QString, arg_last_error),
				  Q_ARG(QString, stream_server), Q_ARG(QString, stream_key));
}

void CanvasDock::DestroyVideo()
{
	if (!canvas || !obs_canvas_has_video(canvas))
		return;

	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		if (it->output && obs_output_active(it->output))
			return;
	}

	if (obs_output_active(replayOutput) || obs_output_active(recordOutput) ||
	    (obs_output_active(virtualCamOutput) && multiCanvasVideo == nullptr))
		return;

	if (replayOutput)
		obs_encoder_set_video(obs_output_get_video_encoder(replayOutput), nullptr);
	if (recordOutput)
		obs_encoder_set_video(obs_output_get_video_encoder(recordOutput), nullptr);
	if (virtualCamOutput)
		obs_output_set_media(virtualCamOutput, nullptr, obs_get_audio());
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		if (it->output)
			obs_encoder_set_video(obs_output_get_video_encoder(it->output), nullptr);
	}
}

obs_scene_t *CanvasDock::GetCurrentScene()
{
	return scene;
}

std::vector<QString> CanvasDock::GetScenes()
{
	std::vector<QString> scenes;
	if (scenesDock) {
		for (int i = 0; i < scenesDock->sceneList->count(); i++) {
			scenes.push_back(scenesDock->sceneList->item(i)->text());
		}
	} else if (scenesCombo) {
		for (int i = 0; i < scenesCombo->count(); i++) {
			scenes.push_back(scenesCombo->itemText(i));
		}
	}
	return scenes;
}

bool CanvasDock::StreamingActive()
{
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		if (obs_output_active(it->output))
			return true;
	}
	return false;
}

bool CanvasDock::RecordingActive()
{
	return obs_output_active(recordOutput);
}

bool CanvasDock::BacktrackActive()
{
	return obs_output_active(replayOutput);
}

bool CanvasDock::VirtualCameraActive()
{
	return obs_output_active(virtualCamOutput);
}

obs_data_t *CanvasDock::SaveSettings()
{
	auto save_data = obs_data_create();
	if (!currentSceneName.isEmpty()) {
		auto cs = currentSceneName.toUtf8();
		obs_data_set_string(save_data, "current_scene", cs.constData());
	}
	if (scenesDock)
		obs_data_set_bool(save_data, "grid_mode", scenesDock->IsGridMode());

	obs_data_set_int(save_data, "width", canvas_width);
	obs_data_set_int(save_data, "height", canvas_height);
	obs_data_set_int(save_data, "partner_block", partnerBlockTime);
	obs_data_set_bool(save_data, "preview_disabled", preview_disabled);
	obs_data_set_bool(save_data, "virtual_cam_warned", virtual_cam_warned);
	obs_data_set_int(save_data, "streaming_video_bitrate", streamingVideoBitrate);
	obs_data_set_bool(save_data, "streaming_match_main", streamingMatchMain);
	obs_data_set_int(save_data, "record_video_bitrate", recordVideoBitrate);
	obs_data_set_int(save_data, "max_size_mb", max_size_mb);
	obs_data_set_int(save_data, "max_time_sec", max_time_sec);
	obs_data_set_bool(save_data, "recording_match_main", recordingMatchMain);
	obs_data_set_int(save_data, "audio_bitrate", audioBitrate);
	obs_data_set_bool(save_data, "backtrack", startReplay);
	obs_data_set_int(save_data, "backtrack_seconds", replayDuration);
	obs_data_set_string(save_data, "backtrack_path", replayPath.c_str());
	if (replayOutput) {
		auto hotkeys = obs_hotkeys_save_output(replayOutput);
		obs_data_set_obj(save_data, "backtrack_hotkeys", hotkeys);
		obs_data_release(hotkeys);
	}

	obs_data_set_int(save_data, "virtual_camera_mode", virtual_cam_mode);

	obs_data_array_t *stream_servers = SaveStreamOutputs();
	obs_data_set_array(save_data, "stream_outputs", stream_servers);
	obs_data_array_release(stream_servers);

	obs_data_set_bool(save_data, "stream_delay_enabled", stream_delay_enabled);
	obs_data_set_int(save_data, "stream_delay_duration", stream_delay_duration);
	obs_data_set_bool(save_data, "stream_delay_preserve", stream_delay_preserve);

	obs_data_set_bool(save_data, "stream_advanced_settings", stream_advanced_settings);
	obs_data_set_int(save_data, "stream_audio_track", stream_audio_track);
	obs_data_set_string(save_data, "stream_encoder", stream_encoder.c_str());
	obs_data_set_obj(save_data, "stream_encoder_settings", stream_encoder_settings);

	obs_data_set_string(save_data, "record_path", recordPath.c_str());
	obs_data_set_bool(save_data, "record_advanced_settings", record_advanced_settings);
	obs_data_set_string(save_data, "filename_formatting", filename_formatting.c_str());
	obs_data_set_string(save_data, "file_format", file_format.c_str());
	obs_data_set_int(save_data, "record_audio_tracks", record_audio_tracks);
	obs_data_set_string(save_data, "record_encoder", record_encoder.c_str());
	obs_data_set_obj(save_data, "record_encoder_settings", record_encoder_settings);

	obs_data_array_t *start_hotkey = nullptr;
	obs_data_array_t *stop_hotkey = nullptr;
	obs_hotkey_pair_save(backtrack_hotkey, &start_hotkey, &stop_hotkey);
	obs_data_set_array(save_data, "start_backtrack_hotkey", start_hotkey);
	obs_data_set_array(save_data, "stop_backtrack_hotkey", stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);
	start_hotkey = nullptr;
	stop_hotkey = nullptr;
	obs_hotkey_pair_save(virtual_cam_hotkey, &start_hotkey, &stop_hotkey);
	obs_data_set_array(save_data, "start_virtual_cam_hotkey", start_hotkey);
	obs_data_set_array(save_data, "stop_virtual_cam_hotkey", stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);
	start_hotkey = nullptr;
	stop_hotkey = nullptr;
	obs_hotkey_pair_save(record_hotkey, &start_hotkey, &stop_hotkey);
	obs_data_set_array(save_data, "start_record_hotkey", start_hotkey);
	obs_data_set_array(save_data, "stop_record_hotkey", stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);
	start_hotkey = nullptr;
	stop_hotkey = nullptr;
	obs_hotkey_pair_save(stream_hotkey, &start_hotkey, &stop_hotkey);
	obs_data_set_array(save_data, "start_stream_hotkey", start_hotkey);
	obs_data_set_array(save_data, "stop_stream_hotkey", stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);
	start_hotkey = nullptr;
	stop_hotkey = nullptr;
	obs_hotkey_pair_save(pause_hotkey, &start_hotkey, &stop_hotkey);
	obs_data_set_array(save_data, "pause_hotkey", start_hotkey);
	obs_data_set_array(save_data, "unpause_hotkey", stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);
	start_hotkey = obs_hotkey_save(chapter_hotkey);
	obs_data_set_array(save_data, "chapter_hotkey", start_hotkey);
	obs_data_array_release(start_hotkey);
	start_hotkey = obs_hotkey_save(split_hotkey);
	obs_data_set_array(save_data, "split_hotkey", start_hotkey);
	obs_data_array_release(start_hotkey);

	obs_data_array_t *transition_array = obs_data_array_create();
	for (auto transition : transitions) {
		const char *id = obs_source_get_unversioned_id(transition);
		if (!obs_is_source_configurable(id))
			continue;
		obs_data_t *transition_data = obs_save_source(transition);
		if (!transition_data)
			continue;
		obs_data_array_push_back(transition_array, transition_data);
		obs_data_release(transition_data);
	}
	obs_data_set_array(save_data, "transitions", transition_array);
	obs_data_array_release(transition_array);

	obs_data_set_string(save_data, "transition", transitionsDock->transition->currentText().toUtf8().constData());

	return save_data;
}

void CanvasDock::ClearScenes()
{
	if (scenesCombo)
		scenesCombo->clear();
	if (scenesDock && scenesDock->sceneList->count())
		scenesDock->sceneList->clear();
	SwitchScene("", false);
	if (canvas) {
		obs_canvas_set_channel(canvas, 0, nullptr);
		obs_canvas_release(canvas);
		canvas = nullptr;
	}
}

void CanvasDock::StopOutputs()
{
	StopRecord();
	StopStream();
	StopReplayBuffer();
	if (canvas)
		obs_canvas_set_channel(canvas, 0, nullptr);
}

void CanvasDock::LoadScenes()
{
	for (uint32_t i = MAX_CHANNELS - 1; i > 0; i--) {
		auto s = obs_get_output_source(i);
		if (s == nullptr) {
			obs_set_output_source(i, transitionAudioWrapper);
			break;
		}
		obs_source_release(s);
	}
	if (scenesCombo)
		scenesCombo->clear();

	if (scenesDock)
		scenesDock->sceneList->clear();

	StartVideo();

	obs_canvas_enum_scenes(
		canvas,
		[](void *param, obs_source_t *src) {
			auto t = (CanvasDock *)param;
			auto sh = obs_source_get_signal_handler(src);
			signal_handler_connect(sh, "rename", source_rename, t);
			QString name = QString::fromUtf8(obs_source_get_name(src));
			if (t->scenesCombo)
				t->scenesCombo->addItem(name);
			if (t->scenesDock)
				t->scenesDock->sceneList->addItem(name);
			obs_data_t *settings = obs_source_get_settings(src);
			if ((t->currentSceneName.isEmpty() && obs_data_get_bool(settings, "canvas_active")) ||
			    name == t->currentSceneName) {
				if (t->scenesCombo)
					t->scenesCombo->setCurrentText(name);
				if (t->scenesDock) {
					for (int j = 0; j < t->scenesDock->sceneList->count(); j++) {
						auto item = t->scenesDock->sceneList->item(j);
						if (item->text() != name)
							continue;
						t->scenesDock->sceneList->setCurrentItem(item);
					}
				}
			}
			obs_data_release(settings);
			return true;
		},
		this);

	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		const obs_source_t *src = scenes.sources.array[i];
		obs_data_t *settings = obs_source_get_settings(src);
		if (obs_data_get_bool(settings, "custom_size") && obs_data_get_int(settings, "cx") == canvas_width &&
		    obs_data_get_int(settings, "cy") == canvas_height) {
			obs_data_set_bool(settings, "custom_size", false);
			auto sh = obs_source_get_signal_handler(src);
			signal_handler_connect(sh, "rename", source_rename, this);
			obs_canvas_move_scene(obs_scene_from_source(src), canvas);
			QString name = QString::fromUtf8(obs_source_get_name(src));
			if (scenesCombo)
				scenesCombo->addItem(name);
			if (scenesDock)
				scenesDock->sceneList->addItem(name);
			if ((currentSceneName.isEmpty() && obs_data_get_bool(settings, "canvas_active")) ||
			    name == currentSceneName) {
				if (scenesCombo)
					scenesCombo->setCurrentText(name);
				if (scenesDock) {
					for (int j = 0; j < scenesDock->sceneList->count(); j++) {
						auto item = scenesDock->sceneList->item(j);
						if (item->text() != name)
							continue;
						scenesDock->sceneList->setCurrentItem(item);
					}
				}
			}
		}
		obs_data_release(settings);
	}
	obs_frontend_source_list_free(&scenes);
	if ((scenesDock && scenesDock->sceneList->count() == 0) || (scenesCombo && scenesCombo->count() == 0)) {
		AddScene("", false);
	}

	if (scenesDock && scenesDock->sceneList->currentRow() < 0)
		scenesDock->sceneList->setCurrentRow(0);
}

void CanvasDock::SwitchScene(const QString &scene_name, bool transition)
{
	auto fs = scene_name.isEmpty() ? nullptr : obs_canvas_get_scene_by_name(canvas, scene_name.toUtf8().constData());
	if (fs == scene || (fs == nullptr && !scene_name.isEmpty())) {
		obs_scene_release(fs);
		return;
	}
	auto s = obs_scene_get_source(fs);
	auto oldSource = obs_scene_get_source(scene);
	auto sh = oldSource ? obs_source_get_signal_handler(oldSource) : nullptr;
	if (sh) {
		signal_handler_disconnect(sh, "item_add", SceneItemAdded, this);
		signal_handler_disconnect(sh, "reorder", SceneReordered, this);
		signal_handler_disconnect(sh, "refresh", SceneRefreshed, this);
	}
	if (!source || obs_weak_source_references_source(source, oldSource)) {
		obs_weak_source_release(source);
		source = obs_source_get_weak_source(s);
		if (canvas)
			obs_canvas_set_channel(canvas, 0, s);
	} else {
		oldSource = obs_weak_source_get_source(source);
		if (oldSource) {
			auto ost = obs_source_get_type(oldSource);
			if (ost == OBS_SOURCE_TYPE_TRANSITION) {
				auto private_settings = s ? obs_source_get_private_settings(s) : nullptr;
				obs_source_t *override_transition =
					GetTransition(obs_data_get_string(private_settings, "transition"));
				if (SwapTransition(override_transition)) {
					obs_source_release(oldSource);
					oldSource = obs_weak_source_get_source(source);
					signal_handler_t *handler = obs_source_get_signal_handler(oldSource);
					signal_handler_connect(handler, "transition_stop", transition_override_stop, this);
				}
				int duration = 0;
				if (override_transition)
					duration = (int)obs_data_get_int(private_settings, "transition_duration");
				if (duration <= 0)
					duration = obs_frontend_get_transition_duration();
				obs_data_release(private_settings);

				auto sourceA = obs_transition_get_source(oldSource, OBS_TRANSITION_SOURCE_A);
				if (sourceA != obs_scene_get_source(scene))
					obs_transition_set(oldSource, obs_scene_get_source(scene));
				obs_source_release(sourceA);
				if (transition) {
					obs_transition_start(oldSource, OBS_TRANSITION_MODE_AUTO, duration, s);
				} else {
					obs_transition_set(oldSource, s);
				}
			} else {
				obs_weak_source_release(source);
				source = obs_source_get_weak_source(s);
				if (canvas)
					obs_canvas_set_channel(canvas, 0, s);
			}
			obs_source_release(oldSource);
		} else {
			obs_weak_source_release(source);
			source = obs_source_get_weak_source(s);
			if (canvas)
				obs_canvas_set_channel(canvas, 0, s);
		}
	}
	scene = obs_scene_from_source(s);
	if (scene) {
		sh = obs_source_get_signal_handler(s);
		if (sh) {
			signal_handler_connect(sh, "item_add", SceneItemAdded, this);
			signal_handler_connect(sh, "reorder", SceneReordered, this);
			signal_handler_connect(sh, "refresh", SceneRefreshed, this);
		}
	}
	auto oldName = currentSceneName;
	if (!scene_name.isEmpty())
		currentSceneName = scene_name;
	if (scenesCombo && scenesCombo->currentText() != scene_name) {
		scenesCombo->setCurrentText(scene_name);
	}
	if (scenesDock && !scene_name.isEmpty()) {
		QListWidgetItem *item = scenesDock->sceneList->currentItem();
		if (!item || item->text() != scene_name) {
			for (int i = 0; i < scenesDock->sceneList->count(); i++) {
				item = scenesDock->sceneList->item(i);
				if (item->text() == scene_name) {
					scenesDock->sceneList->setCurrentRow(i);
					item->setSelected(true);
					break;
				}
			}
		}
	}
	if (sourcesDock) {
		sourcesDock->sourceList->GetStm()->SceneChanged();
	}
	obs_source_release(s);
	if (vendor && oldName != currentSceneName) {
		const auto d = obs_data_create();
		obs_data_set_int(d, "width", canvas_width);
		obs_data_set_int(d, "height", canvas_height);
		obs_data_set_string(d, "old_scene", oldName.toUtf8().constData());
		obs_data_set_string(d, "new_scene", currentSceneName.toUtf8().constData());
		obs_websocket_vendor_emit_event(vendor, "switch_scene", d);
		obs_data_release(d);
	}
}

void CanvasDock::transition_override_stop(void *data, calldata_t *)
{
	auto dock = (CanvasDock *)data;
	QMetaObject::invokeMethod(dock, "SwitchBackToSelectedTransition", Qt::QueuedConnection);
}

void CanvasDock::SwitchBackToSelectedTransition()
{
	auto tn = transitionsDock->transition->currentText().toUtf8();
	auto transition = GetTransition(tn.constData());
	SwapTransition(transition);
}

obs_source_t *CanvasDock::GetTransition(const char *transition_name)
{
	if (!transition_name || !strlen(transition_name))
		return nullptr;
	for (auto transition : transitions) {
		if (strcmp(transition_name, obs_source_get_name(transition)) == 0) {
			return transition;
		}
	}
	return nullptr;
}

bool CanvasDock::SwapTransition(obs_source_t *newTransition)
{
	if (!newTransition || obs_weak_source_references_source(source, newTransition))
		return false;

	obs_transition_set_size(newTransition, canvas_width, canvas_height);

	obs_source_t *oldTransition = obs_weak_source_get_source(source);
	if (!oldTransition || obs_source_get_type(oldTransition) != OBS_SOURCE_TYPE_TRANSITION) {
		obs_source_release(oldTransition);
		obs_weak_source_release(source);
		source = obs_source_get_weak_source(newTransition);
		if (canvas)
			obs_canvas_set_channel(canvas, 0, newTransition);
		obs_source_inc_showing(newTransition);
		obs_source_inc_active(newTransition);
		return true;
	}
	signal_handler_t *handler = obs_source_get_signal_handler(oldTransition);
	signal_handler_disconnect(handler, "transition_stop", transition_override_stop, this);
	obs_source_inc_showing(newTransition);
	obs_source_inc_active(newTransition);
	obs_transition_swap_begin(newTransition, oldTransition);
	obs_weak_source_release(source);
	source = obs_source_get_weak_source(newTransition);
	if (canvas)
		obs_canvas_set_channel(canvas, 0, newTransition);
	obs_transition_swap_end(newTransition, oldTransition);
	obs_source_dec_showing(oldTransition);
	obs_source_dec_active(oldTransition);
	obs_source_release(oldTransition);
	return true;
}

void CanvasDock::source_rename(void *data, calldata_t *calldata)
{
	const auto d = static_cast<CanvasDock *>(data);
	const auto prev_name = QString::fromUtf8(calldata_string(calldata, "prev_name"));
	const auto new_name = QString::fromUtf8(calldata_string(calldata, "new_name"));
	auto source = (obs_source_t *)calldata_ptr(calldata, "source");
	if (!source || !obs_source_is_scene(source))
		return;

	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		const obs_source_t *src = scenes.sources.array[i];
		auto ss = obs_source_get_settings(src);
		auto c = obs_data_get_array(ss, "canvas");
		obs_data_release(ss);
		if (!c)
			continue;
		const auto count = obs_data_array_count(c);
		for (size_t j = 0; j < count; j++) {
			auto item = obs_data_array_item(c, j);
			auto n = QString::fromUtf8(obs_data_get_string(item, "scene"));
			if (n == prev_name) {
				obs_data_set_string(item, "scene", calldata_string(calldata, "new_name"));
			}
			obs_data_release(item);
		}
		obs_data_array_release(c);
	}
	obs_frontend_source_list_free(&scenes);

	if (d->scenesDock) {
		for (int i = 0; i < d->scenesDock->sceneList->count(); i++) {
			const auto item = d->scenesDock->sceneList->item(i);
			if (item->text() != prev_name)
				continue;
			item->setText(new_name);
		}
	}
	if (d->scenesCombo) {

		for (int i = 0; i < d->scenesCombo->count(); i++) {
			if (d->scenesCombo->itemText(i) != prev_name)
				continue;
			const bool selected = d->scenesCombo->currentText() == prev_name;
			d->scenesCombo->removeItem(i);
			d->scenesCombo->addItem(new_name);
			if (selected)
				d->scenesCombo->setCurrentText(new_name);
		}
	}
}

void CanvasDock::source_remove(void *data, calldata_t *calldata)
{
	const auto d = static_cast<CanvasDock *>(data);
	const auto source = (obs_source_t *)calldata_ptr(calldata, "source");
	if (!obs_source_is_scene(source))
		return;
	const auto canvas = obs_source_get_canvas(source);
	obs_canvas_release(canvas);
	if (!canvas || canvas != d->canvas)
		return;
	if (obs_weak_source_references_source(d->source, source) || source == obs_scene_get_source(d->scene)) {
		QMetaObject::invokeMethod(d, "SwitchScene", Q_ARG(QString, ""), Q_ARG(bool, false));
	}
	const auto name = QString::fromUtf8(obs_source_get_name(source));
	if (name.isEmpty())
		return;
	QMetaObject::invokeMethod(d, "SceneRemoved", Q_ARG(QString, name));
}

void CanvasDock::SceneRemoved(const QString name)
{
	if (scenesDock) {
		for (int i = 0; i < scenesDock->sceneList->count(); i++) {
			auto item = scenesDock->sceneList->item(i);
			if (item->text() != name)
				continue;
			scenesDock->sceneList->takeItem(i);
		}
		auto r = scenesDock->sceneList->currentRow();
		auto c = scenesDock->sceneList->count();
		if ((r < 0 && c > 0) || r >= c) {
			scenesDock->sceneList->setCurrentRow(0);
		}
	}
	if (scenesCombo) {
		for (int i = 0; i < scenesCombo->count(); i++) {
			if (scenesCombo->itemText(i) != name)
				continue;
			scenesCombo->removeItem(i);
		}
		if (scenesCombo->currentIndex() < 0 && scenesCombo->count()) {
			scenesCombo->setCurrentIndex(0);
		}
	}
}

void CanvasDock::source_save(void *data, calldata_t *calldata)
{
	const auto d = static_cast<CanvasDock *>(data);
	const auto source = (obs_source_t *)calldata_ptr(calldata, "source");
	if (!obs_source_is_scene(source))
		return;
	obs_data_t *settings = obs_source_get_settings(source);
	if (!settings)
		return;

	const QString name = QString::fromUtf8(obs_source_get_name(source));
	if (d->scenesCombo)
		obs_data_set_bool(settings, "canvas_active", d->scenesCombo->currentText() == name);

	obs_data_release(settings);
}

void CanvasDock::FinishLoading()
{
	if (!first_time)
		return;
	if (action && !action->isChecked())
		action->trigger();
	auto canvasDock = (QDockWidget *)this->parentWidget();
	auto main = ((QMainWindow *)canvasDock->parentWidget());

	main->addDockWidget(Qt::RightDockWidgetArea, canvasDock);
	canvasDock->setFloating(false);
	canvasDock->show();

	if (scenesDockAction && !scenesDockAction->isChecked())
		scenesDockAction->trigger();
	auto dock = (QDockWidget *)(scenesDock->parentWidget());
	auto sd = main->findChild<QDockWidget *>(QStringLiteral("scenesDock"));
	if (sd) {
		auto area = main->dockWidgetArea(sd);

		if (area == Qt::NoDockWidgetArea) {
			main->addDockWidget(Qt::RightDockWidgetArea, dock);
			main->splitDockWidget(canvasDock, dock, Qt::Horizontal);
		} else {
			main->addDockWidget(area, dock);
			main->splitDockWidget(sd, dock, Qt::Vertical);
		}
	} else {
		main->addDockWidget(Qt::RightDockWidgetArea, dock);
		main->splitDockWidget(canvasDock, dock, Qt::Horizontal);
	}
	dock->setFloating(false);
	dock->show();

	dock = (QDockWidget *)(sourcesDock->parentWidget());
	if (sourcesDockAction && !sourcesDockAction->isChecked())
		sourcesDockAction->trigger();
	sd = main->findChild<QDockWidget *>(QStringLiteral("sourcesDock"));
	if (sd) {
		auto area = main->dockWidgetArea(sd);
		if (area == Qt::NoDockWidgetArea) {
			main->addDockWidget(Qt::RightDockWidgetArea, dock);
			main->splitDockWidget(canvasDock, dock, Qt::Horizontal);
		} else {
			main->addDockWidget(area, dock);
			main->splitDockWidget(sd, dock, Qt::Vertical);
		}
	} else {
		main->addDockWidget(Qt::RightDockWidgetArea, dock);
		main->splitDockWidget(canvasDock, dock, Qt::Horizontal);
	}
	dock->setFloating(false);
	dock->show();

	dock = (QDockWidget *)(transitionsDock->parentWidget());
	if (transitionsDockAction && !transitionsDockAction->isChecked())
		transitionsDockAction->trigger();
	sd = main->findChild<QDockWidget *>(QStringLiteral("transitionsDock"));
	if (sd) {
		auto area = main->dockWidgetArea(sd);
		if (area == Qt::NoDockWidgetArea) {
			main->addDockWidget(Qt::RightDockWidgetArea, dock);
			main->splitDockWidget(canvasDock, dock, Qt::Horizontal);
		} else {
			main->addDockWidget(area, dock);
			main->splitDockWidget(sd, dock, Qt::Vertical);
		}
	} else {
		main->addDockWidget(Qt::RightDockWidgetArea, dock);
		main->splitDockWidget(canvasDock, dock, Qt::Horizontal);
	}
	dock->setFloating(false);
	dock->show();
	save_canvas();
}

void CanvasDock::OnRecordStart()
{
	recordButton->setChecked(true);
	recordButton->setIcon(recordActiveIcon);
	recordButton->setText("00:00");
	recordButton->setChecked(true);
	CheckReplayBuffer(true);
}

void CanvasDock::TryRemux(QString path)
{
	const obs_encoder_t *videoEncoder = nullptr;
	obs_output_t *ro = obs_frontend_get_recording_output();
	if (ro) {
		videoEncoder = obs_output_get_video_encoder(ro);
		obs_output_release(ro);
	}
	if (!videoEncoder) {
		if (!config_get_bool(obs_frontend_get_profile_config(), "Video", "AutoRemux"))
			return;
	}
	if (!videoEncoder) {
		obs_frontend_replay_buffer_start();
		obs_frontend_replay_buffer_stop();
		ro = obs_frontend_get_recording_output();
		if (ro) {
			videoEncoder = obs_output_get_video_encoder(ro);
			obs_output_release(ro);
		}
	}
	if (!videoEncoder) {
		obs_frontend_recording_start();
		obs_frontend_recording_stop();
		ro = obs_frontend_get_recording_output();
		if (ro) {
			videoEncoder = obs_output_get_video_encoder(ro);
			obs_output_release(ro);
		}
	}
	if (videoEncoder) {
		const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		QMetaObject::invokeMethod(main_window, "RecordingFileChanged", Q_ARG(QString, path));
	}
}

void CanvasDock::OnRecordStop(int code, QString last_error)
{
	recordButton->setChecked(false);
	recordButton->setIcon(recordInactiveIcon);
	recordButton->setText("");
	recordButton->setChecked(false);
	HandleRecordError(code, last_error);
	CheckReplayBuffer();
	QTimer::singleShot(500, this, [this] { CheckReplayBuffer(); });
	obs_data_t *s = obs_output_get_settings(recordOutput);
	std::string path = obs_data_get_string(s, "path");
	obs_data_release(s);
	if (!path.empty())
		TryRemux(QString::fromUtf8(path.c_str()));
}

void CanvasDock::HandleRecordError(int code, QString last_error)
{
	if (code != OBS_OUTPUT_SUCCESS) {
		if (!last_error.isEmpty()) {
			blog(LOG_WARNING, "[Vertical Canvas] record stop error %s", last_error.toUtf8().constData());
		} else {
			blog(LOG_WARNING, "[Vertical Canvas] record stop error %i", code);
		}
	}
	if (code == OBS_OUTPUT_UNSUPPORTED && isVisible()) {
		QMessageBox::critical(this, QString::fromUtf8(obs_frontend_get_locale_string("Output.RecordFail.Title")),
				      QString::fromUtf8(obs_frontend_get_locale_string("Output.RecordFail.Unsupported")));

	} else if (code == OBS_OUTPUT_ENCODE_ERROR && isVisible()) {
		QString msg =
			last_error.isEmpty()
				? QString::fromUtf8(obs_frontend_get_locale_string("Output.RecordError.EncodeErrorMsg"))
				: QString::fromUtf8(obs_frontend_get_locale_string("Output.RecordError.EncodeErrorMsg.LastError"))
					  .arg(last_error);
		QMessageBox::warning(this, QString::fromUtf8(obs_frontend_get_locale_string("Output.RecordError.Title")), msg);

	} else if (code == OBS_OUTPUT_NO_SPACE && isVisible()) {
		QMessageBox::warning(this, QString::fromUtf8(obs_frontend_get_locale_string("Output.RecordNoSpace.Title")),
				     QString::fromUtf8(obs_frontend_get_locale_string("Output.RecordNoSpace.Msg")));

	} else if (code != OBS_OUTPUT_SUCCESS && isVisible()) {
		QMessageBox::critical(this, QString::fromUtf8(obs_frontend_get_locale_string("Output.RecordError.Title")),
				      QString::fromUtf8(obs_frontend_get_locale_string("Output.RecordError.Msg")) +
					      (!last_error.isEmpty() ? QString::fromUtf8("\n\n") + last_error
								     : QString::fromUtf8("")));
	}
}

void CanvasDock::OnReplaySaved()
{
	std::string path;
	statusLabel->setText(QString::fromUtf8(obs_module_text("Saved")));
	if (replayOutput) {
		proc_handler_t *ph = obs_output_get_proc_handler(replayOutput);
		if (ph) {
			calldata_t cd = {0};
			if (proc_handler_call(ph, "get_last_replay", &cd)) {
				const char *p = calldata_string(&cd, "path");
				if (p)
					path = p;
			}
			calldata_free(&cd);
		}
	}
	if (!path.empty())
		TryRemux(QString::fromUtf8(path.c_str()));
	replayStatusResetTimer.start(4000);
}

void CanvasDock::OnStreamStart()
{
	streamButton->setChecked(true);
	streamButton->setIcon(streamActiveIcon);
	streamButton->setText("00:00");
	streamButton->setChecked(true);
	CheckReplayBuffer(true);
}

#ifndef OBS_OUTPUT_HDR_DISABLED
#define OBS_OUTPUT_HDR_DISABLED -9
#endif // ! OBS_OUTPUT_HDR_DISABLED

void CanvasDock::OnStreamStop(int code, QString last_error, QString stream_server, QString stream_key)
{
	bool active = false;
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		if (stream_server == QString::fromUtf8(it->stream_server) && stream_key == QString::fromUtf8(it->stream_key)) {
		} else if (obs_output_active(it->output)) {
			active = true;
		}
	}
	if (!active) {
		streamButton->setChecked(false);
		streamButton->setIcon(streamInactiveIcon);
		streamButton->setText("");
		streamButton->setChecked(false);
	}
	const char *errorDescription = "";

	bool use_last_error = false;
	bool encode_error = false;

	switch (code) {
	case OBS_OUTPUT_BAD_PATH:
		errorDescription = obs_frontend_get_locale_string("Output.ConnectFail.BadPath");
		break;

	case OBS_OUTPUT_CONNECT_FAILED:
		use_last_error = true;
		if (stream_server.contains("tiktok")) {
			last_error = QString::fromUtf8(obs_module_text("tiktokError"));
		}
		errorDescription = obs_frontend_get_locale_string("Output.ConnectFail.ConnectFailed");
		break;

	case OBS_OUTPUT_INVALID_STREAM:
		if (stream_server.contains("tiktok")) {
			use_last_error = true;
			last_error = QString::fromUtf8(obs_module_text("tiktokError"));
		}
		errorDescription = obs_frontend_get_locale_string("Output.ConnectFail.InvalidStream");
		break;

	case OBS_OUTPUT_ENCODE_ERROR:
		encode_error = true;
		break;

	case OBS_OUTPUT_HDR_DISABLED:
		errorDescription = obs_frontend_get_locale_string("Output.ConnectFail.HdrDisabled");
		break;

	default:
	case OBS_OUTPUT_ERROR:
		use_last_error = true;
		errorDescription = obs_frontend_get_locale_string("Output.ConnectFail.Error");
		break;

	case OBS_OUTPUT_DISCONNECTED:
		/* doesn't happen if output is set to reconnect.  note that
		 * reconnects are handled in the output, not in the UI */
		use_last_error = true;
		errorDescription = obs_frontend_get_locale_string("Output.ConnectFail.Disconnected");
	}
	if (code != OBS_OUTPUT_SUCCESS) {
		if (use_last_error && !last_error.isEmpty()) {
			blog(LOG_WARNING, "[Vertical Canvas] stream stop error %s", last_error.toUtf8().constData());
		} else {
			blog(LOG_WARNING, "[Vertical Canvas] stream stop error %i", code);
		}
	}
	if (encode_error) {
		QString msg = last_error.isEmpty()
				      ? QString::fromUtf8(obs_frontend_get_locale_string("Output.StreamEncodeError.Msg"))
				      : QString::fromUtf8(obs_frontend_get_locale_string("Output.StreamEncodeError.Msg.LastError"))
						.arg(last_error);
		QMessageBox::information(this, QString::fromUtf8(obs_frontend_get_locale_string("Output.StreamEncodeError.Title")),
					 msg);

	} else if (code != OBS_OUTPUT_SUCCESS && isVisible()) {
		QMessageBox::information(this, QString::fromUtf8(obs_frontend_get_locale_string("Output.ConnectFail.Title")),
					 QString::fromUtf8(errorDescription) + (use_last_error && !last_error.isEmpty()
											? QString::fromUtf8("\n\n") + last_error
											: QString::fromUtf8("")));
	}
	CheckReplayBuffer();
	QTimer::singleShot(500, this, [this] { CheckReplayBuffer(); });
}

void CanvasDock::OnReplayBufferStart()
{
	replayButton->setIcon(replayActiveIcon);
	replayButton->setChecked(true);
}

void CanvasDock::OnReplayBufferStop(int code, QString last_error)
{
	replayButton->setIcon(replayInactiveIcon);
	replayButton->setChecked(false);
	if (!replayStatusResetTimer.isActive())
		replayStatusResetTimer.start(4000);
	if (restart_video)
		ProfileChanged();
	HandleRecordError(code, last_error);
	if (code == OBS_OUTPUT_SUCCESS) {
		CheckReplayBuffer(true);
		QTimer::singleShot(500, this, [this] { CheckReplayBuffer(true); });
	}
}

void CanvasDock::MainSceneChanged()
{
	auto current_scene = obs_frontend_get_current_scene();
	if (!current_scene) {
		if (linkedButton)
			linkedButton->setChecked(false);
		return;
	}

	auto ss = obs_source_get_settings(current_scene);
	obs_source_release(current_scene);
	auto c = obs_data_get_array(ss, "canvas");
	obs_data_release(ss);
	if (!c) {
		if (linkedButton)
			linkedButton->setChecked(false);
		return;
	}
	const auto count = obs_data_array_count(c);
	obs_data_t *found = nullptr;
	for (size_t i = 0; i < count; i++) {
		auto item = obs_data_array_item(c, i);
		if (!item)
			continue;
		if (obs_data_get_int(item, "width") == canvas_width && obs_data_get_int(item, "height") == canvas_height) {
			found = item;
			break;
		}
		obs_data_release(item);
	}
	if (found) {
		auto sn = QString::fromUtf8(obs_data_get_string(found, "scene"));
		SwitchScene(sn);
		if (linkedButton)
			linkedButton->setChecked(true);
	} else if (linkedButton) {
		linkedButton->setChecked(false);
	}
	obs_data_release(found);
	obs_data_array_release(c);
}

bool CanvasDock::start_virtual_cam_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto d = static_cast<CanvasDock *>(data);
	if (obs_output_active(d->virtualCamOutput))
		return false;
	QMetaObject::invokeMethod(d, "VirtualCamButtonClicked");
	return true;
}

bool CanvasDock::stop_virtual_cam_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto d = static_cast<CanvasDock *>(data);
	if (!obs_output_active(d->virtualCamOutput))
		return false;
	QMetaObject::invokeMethod(d, "VirtualCamButtonClicked");
	return true;
}

bool CanvasDock::start_recording_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto d = static_cast<CanvasDock *>(data);
	if (obs_output_active(d->recordOutput))
		return false;
	QMetaObject::invokeMethod(d, "RecordButtonClicked");
	return true;
}

bool CanvasDock::stop_recording_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto d = static_cast<CanvasDock *>(data);
	if (!obs_output_active(d->recordOutput))
		return false;
	QMetaObject::invokeMethod(d, "RecordButtonClicked");
	return true;
}

bool CanvasDock::start_streaming_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto d = static_cast<CanvasDock *>(data);
	for (auto it = d->streamOutputs.begin(); it != d->streamOutputs.end(); ++it)
		if (obs_output_active(it->output))
			return false;
	QMetaObject::invokeMethod(d, "StreamButtonClicked");
	return true;
}

bool CanvasDock::stop_streaming_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto d = static_cast<CanvasDock *>(data);
	bool found = false;
	for (auto it = d->streamOutputs.begin(); it != d->streamOutputs.end(); ++it)
		if (obs_output_active(it->output))
			found = true;
	if (!found)
		return false;
	QMetaObject::invokeMethod(d, "StreamButtonClicked");
	return true;
}

bool CanvasDock::pause_recording_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto d = static_cast<CanvasDock *>(data);
	if (!obs_output_active(d->recordOutput) || obs_output_paused(d->recordOutput))
		return false;
	obs_output_pause(d->recordOutput, true);
	return true;
}

bool CanvasDock::unpause_recording_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto d = static_cast<CanvasDock *>(data);
	if (!obs_output_active(d->recordOutput) || !obs_output_paused(d->recordOutput))
		return false;
	obs_output_pause(d->recordOutput, false);
	return true;
}

void CanvasDock::recording_chapter_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	const auto d = static_cast<CanvasDock *>(data);
	if (!obs_output_active(d->recordOutput))
		return;
	proc_handler_t *ph = obs_output_get_proc_handler(d->recordOutput);
	calldata cd2;
	calldata_init(&cd2);
	proc_handler_call(ph, "add_chapter", &cd2);
	calldata_free(&cd2);
}

void CanvasDock::recording_split_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	const auto d = static_cast<CanvasDock *>(data);
	if (!obs_output_active(d->recordOutput))
		return;
	proc_handler_t *ph = obs_output_get_proc_handler(d->recordOutput);
	calldata cd;
	calldata_init(&cd);
	proc_handler_call(ph, "split_file", &cd);
	calldata_free(&cd);
}

QIcon CanvasDock::GetIconFromType(enum obs_icon_type icon_type) const
{
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());

	switch (icon_type) {
	case OBS_ICON_TYPE_IMAGE:
		return main_window->property("imageIcon").value<QIcon>();
	case OBS_ICON_TYPE_COLOR:
		return main_window->property("colorIcon").value<QIcon>();
	case OBS_ICON_TYPE_SLIDESHOW:
		return main_window->property("slideshowIcon").value<QIcon>();
	case OBS_ICON_TYPE_AUDIO_INPUT:
		return main_window->property("audioInputIcon").value<QIcon>();
	case OBS_ICON_TYPE_AUDIO_OUTPUT:
		return main_window->property("audioOutputIcon").value<QIcon>();
	case OBS_ICON_TYPE_DESKTOP_CAPTURE:
		return main_window->property("desktopCapIcon").value<QIcon>();
	case OBS_ICON_TYPE_WINDOW_CAPTURE:
		return main_window->property("windowCapIcon").value<QIcon>();
	case OBS_ICON_TYPE_GAME_CAPTURE:
		return main_window->property("gameCapIcon").value<QIcon>();
	case OBS_ICON_TYPE_CAMERA:
		return main_window->property("cameraIcon").value<QIcon>();
	case OBS_ICON_TYPE_TEXT:
		return main_window->property("textIcon").value<QIcon>();
	case OBS_ICON_TYPE_MEDIA:
		return main_window->property("mediaIcon").value<QIcon>();
	case OBS_ICON_TYPE_BROWSER:
		return main_window->property("browserIcon").value<QIcon>();
	case OBS_ICON_TYPE_CUSTOM:
		//TODO: Add ability for sources to define custom icons
		return main_window->property("defaultIcon").value<QIcon>();
	case OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT:
		return main_window->property("audioProcessOutputIcon").value<QIcon>();
	default:
		return main_window->property("defaultIcon").value<QIcon>();
	}
}

QIcon CanvasDock::GetSceneIcon() const
{
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	return main_window->property("sceneIcon").value<QIcon>();
}

QIcon CanvasDock::GetGroupIcon() const
{
	const auto main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	return main_window->property("groupIcon").value<QIcon>();
}

void CanvasDock::MainStreamStart()
{
	CheckReplayBuffer(true);
	if (streamingMatchMain)
		StartStream();
}

void CanvasDock::MainStreamStop()
{
	CheckReplayBuffer();
	if (streamingMatchMain)
		StopStream();
}

void CanvasDock::MainRecordStart()
{
	CheckReplayBuffer(true);
	if (recordingMatchMain)
		StartRecord();
}

void CanvasDock::MainRecordStop()
{
	CheckReplayBuffer();
	if (recordingMatchMain)
		StopRecord();
}

void CanvasDock::MainReplayBufferStart()
{
	CheckReplayBuffer(true);
}

void CanvasDock::MainReplayBufferStop()
{
	CheckReplayBuffer();
}

void CanvasDock::MainVirtualCamStart()
{
	CheckReplayBuffer(true);
}

void CanvasDock::MainVirtualCamStop()
{
	CheckReplayBuffer();
}

void CanvasDock::SceneReordered(void *data, calldata_t *params)
{
	CanvasDock *window = static_cast<CanvasDock *>(data);

	obs_scene_t *scene = (obs_scene_t *)calldata_ptr(params, "scene");

	QMetaObject::invokeMethod(window, "ReorderSources", Qt::QueuedConnection, Q_ARG(OBSScene, OBSScene(scene)));
}

void CanvasDock::ReorderSources(OBSScene order_scene)
{
	if (order_scene != scene || sourcesDock->sourceList->IgnoreReorder())
		return;

	sourcesDock->sourceList->ReorderItems();
}

void CanvasDock::SceneRefreshed(void *data, calldata_t *params)
{
	CanvasDock *window = static_cast<CanvasDock *>(data);

	obs_scene_t *scene = (obs_scene_t *)calldata_ptr(params, "scene");

	QMetaObject::invokeMethod(window, "RefreshSources", Qt::QueuedConnection, Q_ARG(OBSScene, OBSScene(scene)));
}

void CanvasDock::RefreshSources(OBSScene refresh_scene)
{
	if (refresh_scene != scene || sourcesDock->sourceList->IgnoreReorder())
		return;

	sourcesDock->sourceList->RefreshItems();
}

void CanvasDock::SceneItemAdded(void *data, calldata_t *params)
{
	CanvasDock *window = static_cast<CanvasDock *>(data);

	obs_sceneitem_t *item = (obs_sceneitem_t *)calldata_ptr(params, "item");

	QMetaObject::invokeMethod(window, "AddSceneItem", Qt::QueuedConnection, Q_ARG(OBSSceneItem, OBSSceneItem(item)));
}

void CanvasDock::AddSceneItem(OBSSceneItem item)
{
	obs_scene_t *add_scene = obs_sceneitem_get_scene(item);

	if (sourcesDock && scene == add_scene)
		sourcesDock->sourceList->Add(item);

	obs_scene_enum_items(add_scene, select_one, (obs_sceneitem_t *)item);
}

void CanvasDock::SendVendorEvent(const char *event_name)
{
	if (!vendor)
		return;
	const auto d = obs_data_create();
	obs_data_set_int(d, "width", canvas_width);
	obs_data_set_int(d, "height", canvas_height);
	obs_websocket_vendor_emit_event(vendor, event_name, d);
	obs_data_release(d);
}

static bool nudge_callback(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	if (obs_sceneitem_locked(item))
		return true;

	struct vec2 &offset = *reinterpret_cast<struct vec2 *>(param);
	struct vec2 pos;

	if (!obs_sceneitem_selected(item)) {
		if (obs_sceneitem_is_group(item)) {
			struct vec3 offset3;
			vec3_set(&offset3, offset.x, offset.y, 0.0f);

			struct matrix4 matrix;
			obs_sceneitem_get_draw_transform(item, &matrix);
			vec4_set(&matrix.t, 0.0f, 0.0f, 0.0f, 1.0f);
			matrix4_inv(&matrix, &matrix);
			vec3_transform(&offset3, &offset3, &matrix);

			struct vec2 new_offset;
			vec2_set(&new_offset, offset3.x, offset3.y);
			obs_sceneitem_group_enum_items(item, nudge_callback, &new_offset);
		}

		return true;
	}

	obs_sceneitem_get_pos(item, &pos);
	vec2_add(&pos, &pos, &offset);
	obs_sceneitem_set_pos(item, &pos);
	return true;
}

void CanvasDock::Nudge(int dist, MoveDir dir)
{
	if (locked)
		return;

	struct vec2 offset;
	vec2_set(&offset, 0.0f, 0.0f);

	switch (dir) {
	case MoveDir::Up:
		offset.y = (float)-dist;
		break;
	case MoveDir::Down:
		offset.y = (float)dist;
		break;
	case MoveDir::Left:
		offset.x = (float)-dist;
		break;
	case MoveDir::Right:
		offset.x = (float)dist;
		break;
	}

	obs_scene_enum_items(scene, nudge_callback, &offset);
}

void RemoveWidget(QWidget *widget);

void RemoveLayoutItem(QLayoutItem *item)
{
	if (!item)
		return;
	RemoveWidget(item->widget());
	if (item->layout()) {
		while (QLayoutItem *item2 = item->layout()->takeAt(0))
			RemoveLayoutItem(item2);
	}
	delete item;
}

void RemoveWidget(QWidget *widget)
{
	if (!widget)
		return;
	if (widget->layout()) {
		auto l = widget->layout();
		QLayoutItem *item;
		while (l->count() > 0 && (item = l->takeAt(0))) {
			RemoveLayoutItem(item);
		}
		delete l;
	}
	delete widget;
}

void CanvasDock::ApiInfo(QString info)
{
	auto d = obs_data_create_from_json(info.toUtf8().constData());
	if (!d)
		return;
	auto data_obj = obs_data_get_obj(d, "data");
	obs_data_release(d);
	if (!data_obj)
		return;
	auto version = obs_data_get_string(data_obj, "version");
	int major;
	int minor;
	int patch;
	if (sscanf(version, "%d.%d.%d", &major, &minor, &patch) == 3) {
		auto sv = MAKE_SEMANTIC_VERSION(major, minor, patch);
		if (sv > MAKE_SEMANTIC_VERSION(PROJECT_VERSION_MAJOR, PROJECT_VERSION_MINOR, PROJECT_VERSION_PATCH)) {
			newer_version_available = QString::fromUtf8(version);
			configButton->setStyleSheet(QString::fromUtf8("background: rgb(192,128,0);"));
		}
	}
	time_t current_time = time(nullptr);
	if (current_time < partnerBlockTime || current_time - partnerBlockTime > 1209600) {
		obs_data_array_t *blocks = obs_data_get_array(data_obj, "partnerBlocks");
		size_t count = obs_data_array_count(blocks);
		size_t added_count = 0;
		for (size_t i = count; i > 0; i--) {
			obs_data_t *block = obs_data_array_item(blocks, i - 1);
			auto block_type = obs_data_get_string(block, "type");
			QBoxLayout *layout = nullptr;
			if (strcmp(block_type, "LINK") == 0) {
				auto button = new QPushButton(QString::fromUtf8(obs_data_get_string(block, "label")));
				button->setStyleSheet(QString::fromUtf8(obs_data_get_string(block, "qss")));
				auto url = QString::fromUtf8(obs_data_get_string(block, "data"));
				connect(button, &QPushButton::clicked, [url] { QDesktopServices::openUrl(QUrl(url)); });
				auto buttonRow = new QHBoxLayout;
				buttonRow->addWidget(button);
				layout = buttonRow;

			} else if (strcmp(block_type, "IMAGE") == 0) {
				auto image_data = QString::fromUtf8(obs_data_get_string(block, "data"));
				if (image_data.startsWith("data:image/")) {
					auto pos = image_data.indexOf(";");
					auto format = image_data.mid(11, pos - 11);
					QImage image;
					if (image.loadFromData(QByteArray::fromBase64(image_data.mid(pos + 7).toUtf8().constData()),
							       format.toUtf8().constData())) {
						auto label = new AspectRatioPixmapLabel;
						label->setPixmap(QPixmap::fromImage(image));
						label->setAlignment(Qt::AlignCenter);
						label->setStyleSheet(QString::fromUtf8(obs_data_get_string(block, "qss")));
						auto labelRow = new QHBoxLayout;
						labelRow->addWidget(label, 1, Qt::AlignCenter);
						layout = labelRow;
					}
				}
			}
			if (layout) {
				added_count++;
				if (i == 1) {
					auto closeButton = new QPushButton("🞫");
					connect(closeButton, &QPushButton::clicked, [this, added_count] {
						for (size_t j = 0; j < added_count; j++) {
							auto item = mainLayout->takeAt(2);
							RemoveLayoutItem(item);
						}
						partnerBlockTime = time(nullptr);
						SaveSettings();
					});
					layout->addWidget(closeButton);
				}
				mainLayout->insertLayout(2, layout, 0);
			}
			obs_data_release(block);
		}
		obs_data_array_release(blocks);
	}
	obs_data_release(data_obj);
}

void CanvasDock::ProfileChanged()
{
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it)
		if (obs_output_active(it->output))
			return;

	if (obs_output_active(recordOutput))
		return;

	if (obs_output_active(replayOutput)) {
		obs_output_stop(replayOutput);
		restart_video = true;
		return;
	}

	bool virtual_cam_active = obs_output_active(virtualCamOutput);
	if (virtual_cam_active)
		StopVirtualCam();

	DestroyVideo();
	StartVideo();

	if (virtual_cam_active)
		StartVirtualCam();
	if (restart_video)
		CheckReplayBuffer(true);

	restart_video = false;
}

void CanvasDock::DeleteProjector(OBSProjector *projector)
{
	for (size_t i = 0; i < projectors.size(); i++) {
		if (projectors[i] == projector) {
			projectors[i]->deleteLater();
			projectors.erase(projectors.begin() + i);
			break;
		}
	}
}
OBSProjector *CanvasDock::OpenProjector(int monitor)
{
	/* seriously?  10 monitors? */
	if (monitor > 9 || monitor > QGuiApplication::screens().size() - 1)
		return nullptr;
	auto config = get_user_config();
	if (!config)
		return nullptr;

	bool closeProjectors = config_get_bool(config, "BasicWindow", "CloseExistingProjectors");

	if (closeProjectors && monitor > -1) {
		for (size_t i = projectors.size(); i > 0; i--) {
			size_t idx = i - 1;
			if (projectors[idx]->GetMonitor() == monitor)
				DeleteProjector(projectors[idx]);
		}
	}

	OBSProjector *projector = new OBSProjector(this, monitor);

	projectors.emplace_back(projector);

	return projector;
}

QString GetMonitorName(const QString &id);

void CanvasDock::AddProjectorMenuMonitors(QMenu *parent, QObject *target, const char *slot)
{
	QList<QScreen *> screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); i++) {
		QScreen *screen = screens[i];
		QRect screenGeometry = screen->geometry();
		qreal ratio = screen->devicePixelRatio();
		QString name = "";
#if defined(_WIN32) && QT_VERSION < QT_VERSION_CHECK(6, 4, 0)
		QTextStream fullname(&name);
		fullname << GetMonitorName(screen->name());
		fullname << " (";
		fullname << (i + 1);
		fullname << ")";
#elif defined(__APPLE__) || defined(_WIN32)
		name = screen->name();
#else
		name = screen->model().simplified();

		if (name.length() > 1 && name.endsWith("-"))
			name.chop(1);
#endif
		name = name.simplified();

		if (name.length() == 0) {
			name = QString("%1 %2")
				       .arg(QString::fromUtf8(obs_frontend_get_locale_string("Display")))
				       .arg(QString::number(i + 1));
		}
		QString str = QString("%1: %2x%3 @ %4,%5")
				      .arg(name, QString::number(screenGeometry.width() * ratio),
					   QString::number(screenGeometry.height() * ratio), QString::number(screenGeometry.x()),
					   QString::number(screenGeometry.y()));

		QAction *a = parent->addAction(str, target, slot);
		a->setProperty("monitor", i);
	}
}

void CanvasDock::OpenPreviewProjector()
{
	int monitor = sender()->property("monitor").toInt();
	OpenProjector(monitor);
}

void CanvasDock::OpenSourceProjector()
{
	int monitor = sender()->property("monitor").toInt();
	OBSSceneItem item = GetSelectedItem();
	if (!item)
		return;

	obs_source_t *open_source = obs_sceneitem_get_source(item);
	if (!open_source)
		return;
	obs_frontend_open_projector("Source", monitor, nullptr, obs_source_get_name(open_source));
}

void CanvasDock::updateStreamKey(const QString &newStreamKey, int index)
{
	if ((int)streamOutputs.size() < index)
		return;
	streamOutputs[index].stream_key = newStreamKey.toStdString();
}

void CanvasDock::updateStreamServer(const QString &newStreamServer, int index)
{
	if ((int)streamOutputs.size() < index)
		return;
	streamOutputs[index].stream_server = newStreamServer.toStdString();
}

QMenu *CanvasDock::CreateVisibilityTransitionMenu(bool visible, obs_sceneitem_t *si)
{
	QMenu *menu = new QMenu(QString::fromUtf8(obs_frontend_get_locale_string(visible ? "ShowTransition" : "HideTransition")));

	obs_source_t *curTransition = obs_sceneitem_get_transition(si, visible);
	const char *curId = curTransition ? obs_source_get_id(curTransition) : nullptr;
	int curDuration = (int)obs_sceneitem_get_transition_duration(si, visible);

	if (curDuration <= 0)
		curDuration = obs_frontend_get_transition_duration();

	QSpinBox *duration = new QSpinBox(menu);
	duration->setMinimum(50);
	duration->setSuffix(" ms");
	duration->setMaximum(20000);
	duration->setSingleStep(50);
	duration->setValue(curDuration);

	auto setTransition = [](QAction *a, bool vis, obs_sceneitem_t *si2) {
		std::string id = a->property("transition_id").toString().toUtf8().constData();
		if (id.empty()) {
			obs_sceneitem_set_transition(si2, vis, nullptr);
		} else {
			obs_source_t *tr = obs_sceneitem_get_transition(si2, vis);

			if (!tr || strcmp(id.c_str(), obs_source_get_id(tr)) != 0) {
				QString name = QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(si2)));
				name += " ";
				name += QString::fromUtf8(
					obs_frontend_get_locale_string(vis ? "ShowTransition" : "HideTransition"));
				tr = obs_source_create_private(id.c_str(), name.toUtf8().constData(), nullptr);
				obs_sceneitem_set_transition(si2, vis, tr);
				obs_source_release(tr);

				int dur = (int)obs_sceneitem_get_transition_duration(si2, vis);
				if (dur <= 0) {
					dur = obs_frontend_get_transition_duration();
					obs_sceneitem_set_transition_duration(si2, vis, dur);
				}
			}
			if (obs_source_configurable(tr))
				obs_frontend_open_source_properties(tr);
		}
	};
	auto setDuration = [visible, si](int dur) {
		obs_sceneitem_set_transition_duration(si, visible, dur);
	};
	connect(duration, (void(QSpinBox::*)(int)) & QSpinBox::valueChanged, setDuration);

	QAction *a = menu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("None")));
	a->setProperty("transition_id", QString::fromUtf8(""));
	a->setCheckable(true);
	a->setChecked(!curId);
	connect(a, &QAction::triggered, std::bind(setTransition, a, visible, si));
	size_t idx = 0;
	const char *id;
	while (obs_enum_transition_types(idx++, &id)) {
		const char *name = obs_source_get_display_name(id);
		const bool match = id && curId && strcmp(id, curId) == 0;
		a = menu->addAction(QString::fromUtf8(name));
		a->setProperty("transition_id", QString::fromUtf8(id));
		a->setCheckable(true);
		a->setChecked(match);
		connect(a, &QAction::triggered, std::bind(setTransition, a, visible, si));
	}

	QWidgetAction *durationAction = new QWidgetAction(menu);
	durationAction->setDefaultWidget(duration);

	menu->addSeparator();
	menu->addAction(durationAction);
	if (curId && obs_is_source_configurable(curId)) {
		menu->addSeparator();
		menu->addAction(QString::fromUtf8(obs_frontend_get_locale_string("Properties")), this,
				[curTransition] { obs_frontend_open_source_properties(curTransition); });
	}

	return menu;
}

void CanvasDock::get_transitions(void *data, struct obs_frontend_source_list *sources)
{
	auto dock = (CanvasDock *)data;
	for (auto transition : dock->transitions) {
		obs_source_t *tr = transition;
		if (obs_source_get_ref(tr) != nullptr)
			da_push_back(sources->sources, &tr);
	}
}

bool CanvasDock::LoadStreamOutputs(obs_data_array_t *outputs)
{
	auto count = obs_data_array_count(outputs);
	auto enabled_count = 0;
	for (auto it = streamOutputs.begin(); it != streamOutputs.end();) {
		bool found = false;
		for (size_t i = 0; !found && i < count; i++) {
			auto item = obs_data_array_item(outputs, i);
			if (it->name == obs_data_get_string(item, "name")) {
				it->stream_server = obs_data_get_string(item, "stream_server");
				it->stream_key = obs_data_get_string(item, "stream_key");
				it->enabled = obs_data_get_bool(item, "enabled");
				if (it->enabled)
					enabled_count++;
				obs_data_release(it->settings);
				it->settings = item;
				found = true;
				break;
			}
			obs_data_release(item);
		}
		if (!found) {
			if (obs_output_active(it->output))
				obs_output_stop(it->output);
			obs_service_release(obs_output_get_service(it->output));
			obs_output_release(it->output);
			obs_data_release(it->settings);
			it = streamOutputs.erase(it);
		} else {
			it++;
		}
	}

	for (size_t i = 0; i < count; i++) {
		auto item = obs_data_array_item(outputs, i);
		auto name = obs_data_get_string(item, "name");
		bool found = false;
		for (auto it = streamOutputs.begin(); it != streamOutputs.end(); it++) {
			if (it->name == name) {
				found = true;
				break;
			}
		}
		if (found) {
			obs_data_release(item);
			continue;
		}
		StreamServer ss;
		ss.name = name;
		ss.stream_server = obs_data_get_string(item, "stream_server");
		ss.stream_key = obs_data_get_string(item, "stream_key");
		ss.enabled = obs_data_get_bool(item, "enabled");
		if (ss.enabled)
			enabled_count++;
		std::string service_name = "vertical_canvas_stream_service_";
		service_name += std::to_string(i);
		bool whip = strstr(ss.stream_server.c_str(), "whip") != nullptr;
		ss.service = obs_service_create(whip ? "whip_custom" : "rtmp_custom", service_name.c_str(), nullptr, nullptr);
		ss.settings = item;
		streamOutputs.push_back(ss);
	}
	return enabled_count > 1;
}

obs_data_array_t *CanvasDock::SaveStreamOutputs()
{
	obs_data_array_t *outputs = obs_data_array_create();
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); ++it) {
		obs_data_t *s = it->settings;
		if (s)
			obs_data_addref(s);
		else
			s = obs_data_create();
		obs_data_set_string(s, "name", it->name.c_str());
		obs_data_set_string(s, "stream_server", it->stream_server.c_str());
		obs_data_set_string(s, "stream_key", it->stream_key.c_str());
		obs_data_set_bool(s, "enabled", it->enabled);
		obs_data_array_push_back(outputs, s);
		obs_data_release(s);
	}
	return outputs;
}

void CanvasDock::StartStreamOutput(std::string name)
{
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); it++) {
		if (it->name == name) {
			StartStreamOutput(it);
			break;
		}
	}
}

void CanvasDock::StopStreamOutput(std::string name)
{
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); it++) {
		if (it->name == name) {
			if (it->output)
				obs_output_stop(it->output);
			break;
		}
	}
}

obs_output_t *CanvasDock::GetStreamOutput(std::string name)
{
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); it++) {
		if (it->name == name)
			return obs_output_get_ref(it->output);
	}
	return nullptr;
}

void CanvasDock::UpdateMulti()
{
	int enabled_count = 0;
	int active_count = 0;
	for (auto it = streamOutputs.begin(); it != streamOutputs.end(); it++) {
		if (obs_output_active(it->output))
			active_count++;
		if (it->enabled)
			enabled_count++;
	}
	if (enabled_count > 1 && !multi_rtmp) {
		streamButtonMulti->setVisible(true);
		multi_rtmp = true;
		streamButton->setChecked(active_count > 0);
		streamButton->setStyleSheet(QString::fromUtf8(
			"QPushButton:checked{background: rgb(0,210,153);} QPushButton{border-top-right-radius: 0; border-bottom-right-radius: 0;}"));
	} else if (enabled_count <= 1 && multi_rtmp) {
		streamButtonMulti->setVisible(false);
		multi_rtmp = false;
		streamButton->setChecked(active_count > 0);
		streamButton->setStyleSheet(QString::fromUtf8("QPushButton:checked{background: rgb(0,210,153);}"));
	}
}

void CanvasDock::DisableStreamSettings()
{
	disable_stream_settings = true;
}

LockedCheckBox::LockedCheckBox()
{
	setProperty("lockCheckBox", true);
	setProperty("class", "indicator-lock");
}

LockedCheckBox::LockedCheckBox(QWidget *parent) : QCheckBox(parent) {}

VisibilityCheckBox::VisibilityCheckBox()
{
	setProperty("visibilityCheckBox", true);
	setProperty("class", "indicator-visibility");
}

VisibilityCheckBox::VisibilityCheckBox(QWidget *parent) : QCheckBox(parent) {}

AspectRatioPixmapLabel::AspectRatioPixmapLabel(QWidget *parent) : QLabel(parent)
{
	setMinimumSize(1, 1);
	setScaledContents(false);
}

void AspectRatioPixmapLabel::setPixmap(const QPixmap &p)
{
	pix = p;
	QLabel::setPixmap(scaledPixmap());
}

int AspectRatioPixmapLabel::heightForWidth(int width) const
{
	return pix.isNull() ? height() : (pix.height() * width) / pix.width();
}

QSize AspectRatioPixmapLabel::sizeHint() const
{
	int w = width();
	return QSize(w, heightForWidth(w));
}

QPixmap AspectRatioPixmapLabel::scaledPixmap() const
{
	return pix.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void AspectRatioPixmapLabel::resizeEvent(QResizeEvent *e)
{
	UNUSED_PARAMETER(e);
	if (!pix.isNull())
		QLabel::setPixmap(scaledPixmap());
}
