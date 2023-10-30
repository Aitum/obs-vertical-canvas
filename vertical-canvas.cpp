#include "vertical-canvas.hpp"

#include <list>

#include "version.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QDesktopServices>

#include <QMainWindow>
#include <QPushButton>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QToolBar>
#include <QWidgetAction>

#include "scenes-dock.hpp"
#include "config-dialog.hpp"
#include "display-helpers.hpp"
#include "name-dialog.hpp"
#include "obs-websocket-api.h"
#include "sources-dock.hpp"
#include "transitions-dock.hpp"
#include "audio-wrapper-source.h"
#include "media-io/video-frame.h"
#include "util/config-file.h"
#include "util/dstr.h"
#include "util/platform.h"
#include "util/util.hpp"
extern "C" {
#include "file-updater.h"
}

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Aitum");
OBS_MODULE_USE_DEFAULT_LOCALE("vertical-canvas", "en-US")

#define HANDLE_RADIUS 4.0f
#define HANDLE_SEL_RADIUS (HANDLE_RADIUS * 1.5f)
#define HELPER_ROT_BREAKPONT 45.0f

#define SPACER_LABEL_MARGIN 6.0f

inline std::list<CanvasDock *> canvas_docks;

void clear_canvas_docks()
{
	for (const auto &it : canvas_docks) {
		it->ClearScenes();
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
	char path[512];
	strcpy(path, obs_module_config_path("config.json"));
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
}

void transition_start(void *, calldata_t *)
{
	for (const auto &it : canvas_docks) {
		QMetaObject::invokeMethod(it, "MainSceneChanged",
					  Qt::QueuedConnection);
	}
}

void frontend_event(obs_frontend_event event, void *private_data)
{
	UNUSED_PARAMETER(private_data);
	if (event == OBS_FRONTEND_EVENT_EXIT ||
	    event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
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
			auto sh = obs_source_get_signal_handler(
				transitions.sources.array[i]);
			signal_handler_connect(sh, "transition_start",
					       transition_start, nullptr);
		}
		obs_frontend_source_list_free(&transitions);
		for (const auto &it : canvas_docks) {
			it->LoadScenes();
		}
	} else if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		struct obs_frontend_source_list transitions = {};
		obs_frontend_get_transitions(&transitions);
		for (size_t i = 0; i < transitions.sources.num; i++) {
			auto sh = obs_source_get_signal_handler(
				transitions.sources.array[i]);
			signal_handler_connect(sh, "transition_start",
					       transition_start, nullptr);
		}
		obs_frontend_source_list_free(&transitions);
		for (const auto &it : canvas_docks) {
			it->LoadScenes();
			it->FinishLoading();
		}
	} else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainSceneChanged");
		}
	} else if ( //event == OBS_FRONTEND_EVENT_STREAMING_STARTING ||
		event == OBS_FRONTEND_EVENT_STREAMING_STARTED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainStreamStart",
						  Qt::QueuedConnection);
		}
	} else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPING ||
		   event == OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainStreamStop",
						  Qt::QueuedConnection);
			QTimer::singleShot(200, it, [it] {
				QMetaObject::invokeMethod(it, "MainStreamStop",
							  Qt::QueuedConnection);
			});
		}
	} else if ( //event == OBS_FRONTEND_EVENT_RECORDING_STARTING ||
		event == OBS_FRONTEND_EVENT_RECORDING_STARTED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainRecordStart",
						  Qt::QueuedConnection);
		}
	} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPING ||
		   event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainRecordStop",
						  Qt::QueuedConnection);
			QTimer::singleShot(200, it, [it] {
				QMetaObject::invokeMethod(it, "MainRecordStop",
							  Qt::QueuedConnection);
			});
		}

	} else if ( //event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING ||
		event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainReplayBufferStart",
						  Qt::QueuedConnection);
		}
	} else if (event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPING ||
		   event == OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainReplayBufferStop",
						  Qt::QueuedConnection);
			QTimer::singleShot(200, it, [it] {
				QMetaObject::invokeMethod(
					it, "MainReplayBufferStop",
					Qt::QueuedConnection);
			});
		}

	} else if (event == OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainVirtualCamStart",
						  Qt::QueuedConnection);
		}

	} else if (event == OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "MainVirtualCamStop",
						  Qt::QueuedConnection);
			QTimer::singleShot(200, it, [it] {
				QMetaObject::invokeMethod(it,
							  "MainVirtualCamStop",
							  Qt::QueuedConnection);
			});
		}
	} else if (event == OBS_FRONTEND_EVENT_PROFILE_CHANGED) {
		for (const auto &it : canvas_docks) {
			QMetaObject::invokeMethod(it, "ProfileChanged",
						  Qt::QueuedConnection);
		}
	}
}

obs_websocket_vendor vendor = nullptr;

void vendor_request_version(obs_data_t *request_data, obs_data_t *response_data,
			    void *)
{
	UNUSED_PARAMETER(request_data);
	obs_data_set_string(response_data, "version", PROJECT_VERSION);
	obs_data_set_bool(response_data, "success", true);
}

void vendor_request_switch_scene(obs_data_t *request_data,
				 obs_data_t *response_data, void *)
{
	const char *scene_name = obs_data_get_string(request_data, "scene");
	if (!scene_name || !strlen(scene_name)) {
		obs_data_set_string(response_data, "error", "'scene' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	const auto scene_source = obs_get_source_by_name(scene_name);
	if (!scene_source) {
		obs_data_set_string(response_data, "error",
				    "'scene' not found");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	const auto scene = obs_scene_from_source(scene_source);
	if (!scene) {
		obs_source_release(scene_source);
		obs_data_set_string(response_data, "error",
				    "'scene' not a scene");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	const auto settings = obs_source_get_settings(scene_source);
	obs_source_release(scene_source);
	if (!settings || !obs_data_get_bool(settings, "custom_size")) {
		obs_data_release(settings);
		obs_data_set_string(response_data, "error",
				    "'scene' not a vertical canvas scene");
		obs_data_set_bool(response_data, "success", false);
		return;
	}
	const auto width = obs_data_get_int(settings, "cx");
	const auto height = obs_data_get_int(settings, "cy");
	obs_data_release(settings);
	for (const auto &it : canvas_docks) {
		if (it->GetCanvasWidth() != width ||
		    it->GetCanvasHeight() != height)
			continue;
		QMetaObject::invokeMethod(it, "SwitchScene",
					  Q_ARG(QString,
						QString::fromUtf8(scene_name)));
	}

	obs_data_set_bool(response_data, "success", true);
}

void vendor_request_current_scene(obs_data_t *request_data,
				  obs_data_t *response_data, void *)
{
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) ||
		    (height && it->GetCanvasHeight() != height))
			continue;
		auto scene = obs_scene_get_source(it->GetCurrentScene());
		if (scene) {
			obs_data_set_string(response_data, "scene",
					    obs_source_get_name(scene));
		} else {
			obs_data_set_string(response_data, "scene", "");
		}
		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_get_scenes(obs_data_t *request_data,
			       obs_data_t *response_data, void *)
{
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	auto sa = obs_data_array_create();
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) ||
		    (height && it->GetCanvasHeight() != height))
			continue;
		auto scenes = it->GetScenes();
		for (auto &scene : scenes) {
			auto s = obs_data_create();
			obs_data_set_string(s, "name",
					    scene.toUtf8().constData());
			obs_data_array_push_back(sa, s);
			obs_data_release(s);
		}
	}
	obs_data_set_array(response_data, "scenes", sa);
	obs_data_array_release(sa);
	obs_data_set_bool(response_data, "success", true);
}

void vendor_request_status(obs_data_t *request_data, obs_data_t *response_data,
			   void *)
{
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) ||
		    (height && it->GetCanvasHeight() != height))
			continue;
		obs_data_set_bool(response_data, "streaming",
				  it->StreamingActive());
		obs_data_set_bool(response_data, "recording",
				  it->RecordingActive());
		obs_data_set_bool(response_data, "backtrack",
				  it->BacktrackActive());
		obs_data_set_bool(response_data, "virtual_camera",
				  it->VirtualCameraActive());
		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_invoke(obs_data_t *request_data, obs_data_t *response_data,
			   void *p)
{
	const char *method = static_cast<char *>(p);
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) ||
		    (height && it->GetCanvasHeight() != height))
			continue;
		QMetaObject::invokeMethod(it, method);
		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_save_replay(obs_data_t *request_data,
				obs_data_t *response_data, void *p)
{
	UNUSED_PARAMETER(p);
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) ||
		    (height && it->GetCanvasHeight() != height))
			continue;
		QMetaObject::invokeMethod(
			it, "ReplayButtonClicked",
			Q_ARG(QString, QString::fromUtf8(obs_data_get_string(
					       request_data, "filename"))));
		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_update_stream_key(obs_data_t *request_data,
				      obs_data_t *response_data, void *)
{
	// Parse request_data to get the new stream_key
	const char *new_stream_key =
		obs_data_get_string(request_data, "stream_key");
	const auto width = obs_data_get_int(request_data, "width");
	const auto height = obs_data_get_int(request_data, "height");

	if (!new_stream_key || !strlen(new_stream_key)) {
		obs_data_set_string(response_data, "error",
				    "'stream_key' not set");
		obs_data_set_bool(response_data, "success", false);
		return;
	}

	// Loop through each CanvasDock to find the right one
	for (const auto &it : canvas_docks) {
		if ((width && it->GetCanvasWidth() != width) ||
		    (height && it->GetCanvasHeight() != height))
			continue;

		// Update stream_key using the UpdateStreamKey method of CanvasDock
		QMetaObject::invokeMethod(
			it, "UpdateStreamKey",
			Q_ARG(QString, QString::fromUtf8(new_stream_key)));

		obs_data_set_bool(response_data, "success", true);
		return;
	}
	obs_data_set_bool(response_data, "success", false);
}

void vendor_request_update_stream_server(obs_data_t *request_data,
                                      obs_data_t *response_data, void *)
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
        if ((width && it->GetCanvasWidth() != width) ||
            (height && it->GetCanvasHeight() != height))
            continue;

        // Update stream_server using the UpdateStreamServer method of CanvasDock
        QMetaObject::invokeMethod(
            it, "UpdateStreamServer",
            Q_ARG(QString, QString::fromUtf8(new_stream_server)));
            
        obs_data_set_bool(response_data, "success", true);
        return;
    }

    obs_data_set_bool(response_data, "success", false);
}

update_info_t *verison_update_info = nullptr;

bool version_info_downloaded(void *param, struct file_download_data *file)
{
	UNUSED_PARAMETER(param);
	if (!file || !file->buffer.num)
		return true;
	auto d = obs_data_create_from_json((const char *)file->buffer.array);
	if (!d)
		return true;
	auto data = obs_data_get_obj(d, "data");
	obs_data_release(d);
	if (!data)
		return true;
	auto version = QString::fromUtf8(obs_data_get_string(data, "version"));
	QStringList pieces = version.split(".");
	if (pieces.count() > 2) {
		auto major = pieces[0].toInt();
		auto minor = pieces[1].toInt();
		auto patch = pieces[2].toInt();
		auto sv = MAKE_SEMANTIC_VERSION(major, minor, patch);
		if (sv > MAKE_SEMANTIC_VERSION(PROJECT_VERSION_MAJOR,
					       PROJECT_VERSION_MINOR,
					       PROJECT_VERSION_PATCH)) {
			for (const auto &it : canvas_docks) {
				QMetaObject::invokeMethod(
					it, "NewerVersionAvailable",
					Q_ARG(QString, version));
			}
		}
	}
	obs_data_release(data);
	return true;
}

bool obs_module_load(void)
{
	if (obs_get_version() < MAKE_SEMANTIC_VERSION(29, 0, 0)) {
		blog(LOG_ERROR,
		     "[Vertical Canvas] loading version %s failed, OBS version %s is to low",
		     PROJECT_VERSION, obs_get_version_string());
		return false;
	}
	blog(LOG_INFO, "[Vertical Canvas] loaded version %s", PROJECT_VERSION);
	obs_frontend_add_event_callback(frontend_event, nullptr);

	obs_register_source(&audio_wrapper_source);

	return true;
}

void obs_module_post_load(void)
{
	const auto path = obs_module_config_path("config.json");
	obs_data_t *config = obs_data_create_from_json_file_safe(path, "bak");
	bfree(path);
	if (!config) {
		config = obs_data_create();
		blog(LOG_WARNING,
		     "[Vertical Canvas] No configuration file loaded");
	} else {
		blog(LOG_INFO, "[Vertical Canvas] Loaded configuration file");
	}

	const auto main_window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto canvas = obs_data_get_array(config, "canvas");
	obs_data_release(config);
	if (!canvas) {
		canvas = obs_data_array_create();
		blog(LOG_WARNING,
		     "[Vertical Canvas] no canvas found in configuration");
	}
	const auto count = obs_data_array_count(canvas);
	if (!count) {
		const auto dock = new CanvasDock(nullptr, main_window);
		auto *a = static_cast<QAction *>(obs_frontend_add_dock(dock));
		dock->setAction(a);
		canvas_docks.push_back(dock);
		obs_data_array_release(canvas);
		blog(LOG_INFO, "[Vertical Canvas] New Canvas created");
		return;
	}
	for (size_t i = 0; i < count; i++) {
		const auto item = obs_data_array_item(canvas, i);
		const auto dock = new CanvasDock(item, main_window);
		obs_data_release(item);
		auto *a = static_cast<QAction *>(obs_frontend_add_dock(dock));
		dock->setAction(a);
		canvas_docks.push_back(dock);
	}
	obs_data_array_release(canvas);

	if (!vendor)
		vendor = obs_websocket_register_vendor("aitum-vertical-canvas");
	if (!vendor)
		return;
	obs_websocket_vendor_register_request(vendor, "version",
					      vendor_request_version, nullptr);
	obs_websocket_vendor_register_request(
		vendor, "switch_scene", vendor_request_switch_scene, nullptr);
	obs_websocket_vendor_register_request(
		vendor, "current_scene", vendor_request_current_scene, nullptr);
	obs_websocket_vendor_register_request(
		vendor, "get_scenes", vendor_request_get_scenes, nullptr);
	obs_websocket_vendor_register_request(vendor, "status",
					      vendor_request_status, nullptr);
	obs_websocket_vendor_register_request(vendor, "start_streaming",
					      vendor_request_invoke,
					      (void *)"StartStream");
	obs_websocket_vendor_register_request(vendor, "stop_streaming",
					      vendor_request_invoke,
					      (void *)"StopStream");
	obs_websocket_vendor_register_request(vendor, "toggle_streaming",
					      vendor_request_invoke,
					      (void *)"StreamButtonClicked");
	obs_websocket_vendor_register_request(vendor, "start_recording",
					      vendor_request_invoke,
					      (void *)"StartRecord");
	obs_websocket_vendor_register_request(vendor, "stop_recording",
					      vendor_request_invoke,
					      (void *)"StopRecord");
	obs_websocket_vendor_register_request(vendor, "toggle_recording",
					      vendor_request_invoke,
					      (void *)"RecordButtonClicked");
	obs_websocket_vendor_register_request(vendor, "start_backtrack",
					      vendor_request_invoke,
					      (void *)"StartReplayBuffer");
	obs_websocket_vendor_register_request(vendor, "stop_backtrack",
					      vendor_request_invoke,
					      (void *)"StopReplayBuffer");
	obs_websocket_vendor_register_request(
		vendor, "save_backtrack", vendor_request_save_replay, nullptr);
	obs_websocket_vendor_register_request(vendor, "start_virtual_camera",
					      vendor_request_invoke,
					      (void *)"StartVirtualCam");
	obs_websocket_vendor_register_request(vendor, "stop_virtual_camera",
					      vendor_request_invoke,
					      (void *)"StopVirtualCam");
	obs_websocket_vendor_register_request(vendor, "update_stream_key",
					      vendor_request_update_stream_key,
					      (void *)"UpdateStreamKey");
	obs_websocket_vendor_register_request(vendor, "update_stream_server",
					      vendor_request_update_stream_server,
					      (void *)"UpdateStreamServer");

	verison_update_info = update_info_create_single(
		"[vertical-canvas]", "OBS", "https://api.aitum.tv/vertical",
		version_info_downloaded, nullptr);
}

void obs_module_unload(void)
{
	if (vendor && obs_get_module("obs-websocket")) {
		obs_websocket_vendor_unregister_request(vendor, "version");
		obs_websocket_vendor_unregister_request(vendor, "switch_scene");
		obs_websocket_vendor_unregister_request(vendor,
							"current_scene");
		obs_websocket_vendor_unregister_request(vendor, "get_scenes");
		obs_websocket_vendor_unregister_request(vendor, "status");
		obs_websocket_vendor_unregister_request(vendor,
							"start_streaming");
		obs_websocket_vendor_unregister_request(vendor,
							"stop_streaming");
		obs_websocket_vendor_unregister_request(vendor,
							"toggle_streaming");
		obs_websocket_vendor_unregister_request(vendor,
							"start_recording");
		obs_websocket_vendor_unregister_request(vendor,
							"stop_recording");
		obs_websocket_vendor_unregister_request(vendor,
							"toggle_recording");
		obs_websocket_vendor_unregister_request(vendor,
							"start_backtrack");
		obs_websocket_vendor_unregister_request(vendor,
							"stop_backtrack");
		obs_websocket_vendor_unregister_request(vendor,
							"save_backtrack");
		obs_websocket_vendor_unregister_request(vendor,
							"start_virtual_camera");
		obs_websocket_vendor_unregister_request(vendor,
							"stop_virtual_camera");
		obs_websocket_vendor_unregister_request(vendor,
							"update_stream_key");
		obs_websocket_vendor_unregister_request(vendor,
							"update_stream_server");
	}
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	update_info_destroy(verison_update_info);
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

QListWidget *CanvasDock::GetGlobalScenesList()
{
	auto p = parentWidget();
	if (!p)
		return nullptr;
	auto scenesDock =
		p->findChild<QDockWidget *>(QStringLiteral("scenesDock"));
	if (!scenesDock)
		return nullptr;
	return scenesDock->findChild<QListWidget *>(QStringLiteral("scenes"));
}

void CanvasDock::AddScene(QString duplicate, bool ask_name)
{
	std::string name = duplicate.isEmpty()
				   ? obs_module_text("VerticalScene")
				   : duplicate.toUtf8().constData();
	obs_source_t *s = obs_get_source_by_name(name.c_str());
	int i = 0;
	while (s) {
		obs_source_release(s);
		i++;
		name = obs_module_text("VerticalScene");
		name += " ";
		name += std::to_string(i);
		s = obs_get_source_by_name(name.c_str());
	}
	do {
		obs_source_release(s);
		if (ask_name &&
		    !NameDialog::AskForName(
			    this,
			    QString::fromUtf8(obs_module_text("SceneName")),
			    name)) {
			break;
		}
		s = obs_get_source_by_name(name.c_str());
		if (s)
			continue;

		obs_source_t *new_scene = nullptr;
		if (!duplicate.isEmpty()) {
			auto origScene = obs_get_source_by_name(
				duplicate.toUtf8().constData());
			if (origScene) {
				auto scene = obs_scene_from_source(origScene);
				if (scene) {
					new_scene = obs_scene_get_source(
						obs_scene_duplicate(
							scene, name.c_str(),
							OBS_SCENE_DUP_REFS));
				}
				obs_source_release(origScene);
				if (new_scene) {
					obs_source_save(new_scene);
					obs_data_t *settings =
						obs_source_get_settings(
							new_scene);
					obs_data_set_bool(settings,
							  "custom_size", true);
					obs_data_set_int(settings, "cx",
							 canvas_width);
					obs_data_set_int(settings, "cy",
							 canvas_height);
					obs_source_load(new_scene);
					obs_data_release(settings);
				}
			}
		}
		if (!new_scene) {
			obs_data_t *settings = obs_data_create();
			obs_data_set_bool(settings, "custom_size", true);
			obs_data_set_int(settings, "cx", canvas_width);
			obs_data_set_int(settings, "cy", canvas_height);
			obs_data_array_t *items = obs_data_array_create();
			obs_data_set_array(settings, "items", items);
			obs_data_array_release(items);
			new_scene = obs_source_create("scene", name.c_str(),
						      settings, nullptr);
			obs_data_release(settings);
			obs_source_load(new_scene);
		}
		auto sn = QString::fromUtf8(obs_source_get_name(new_scene));
		if (scenesCombo)
			scenesCombo->addItem(sn);
		if (scenesDock)
			scenesDock->sceneList->addItem(sn);

		SwitchScene(sn);
		obs_source_release(new_scene);

		auto sl = GetGlobalScenesList();

		if (hideScenes) {
			for (int j = 0; j < sl->count(); j++) {
				auto item = sl->item(j);
				if (item->text() == sn) {
					item->setHidden(true);
				}
			}
		}
	} while (ask_name && s);
}

void CanvasDock::RemoveScene(const QString &sceneName)
{
	auto s = obs_get_source_by_name(sceneName.toUtf8().constData());
	if (!s)
		return;
	if (!obs_source_is_scene(s)) {
		obs_source_release(s);
		return;
	}

	QMessageBox mb(QMessageBox::Question,
		       QString::fromUtf8(obs_frontend_get_locale_string(
			       "ConfirmRemove.Title")),
		       QString::fromUtf8(obs_frontend_get_locale_string(
						 "ConfirmRemove.Text"))
			       .arg(QString::fromUtf8(obs_source_get_name(s))),
		       QMessageBox::StandardButtons(QMessageBox::Yes |
						    QMessageBox::No));
	mb.setDefaultButton(QMessageBox::NoButton);
	if (mb.exec() == QMessageBox::Yes) {
		obs_source_remove(s);
	}

	obs_source_release(s);
}

void CanvasDock::SetLinkedScene(obs_source_t *scene, const QString &linkedScene)
{
	auto ss = obs_source_get_settings(scene);
	auto c = obs_data_get_array(ss, "canvas");

	auto count = obs_data_array_count(c);
	obs_data_t *found = nullptr;
	for (size_t i = 0; i < count; i++) {
		auto item = obs_data_array_item(c, i);
		if (!item)
			continue;
		if (obs_data_get_int(item, "width") == canvas_width &&
		    obs_data_get_int(item, "height") == canvas_height) {
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
		obs_data_set_string(found, "scene",
				    linkedScene.toUtf8().constData());
	}
	obs_data_release(ss);
	obs_data_release(found);
	obs_data_array_release(c);
}

bool CanvasDock::HasScene(QString scene) const
{
	if (scenesCombo) {
		for (int i = 0; i < scenesCombo->count(); i++) {
			if (scene == scenesCombo->itemText(i)) {
				return true;
			}
		}
	}
	if (scenesDock) {
		for (int i = 0; i < scenesDock->sceneList->count(); i++) {
			if (scene == scenesDock->sceneList->item(i)->text()) {
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
	struct check_output {
		obs_output_t *output;
		bool found;
	};
	struct check_output t = {replayOutput, false};
	obs_enum_outputs(
		[](void *b, obs_output_t *output) {
			auto t = (struct check_output *)b;
			if (t->output == output || !obs_output_active(output))
				return true;
			t->found = true;
			return false;
		},
		&t);
	if (!start && !t.found)
		StopReplayBuffer();
	if (start && t.found)
		StartReplayBuffer();
}

void CanvasDock::CreateScenesRow()
{
	const auto sceneRow = new QHBoxLayout(this);
	scenesCombo = new QComboBox;
	connect(scenesCombo, &QComboBox::currentTextChanged,
		[this]() { SwitchScene(scenesCombo->currentText()); });
	sceneRow->addWidget(scenesCombo, 1);

	linkedButton = new LockedCheckBox;
	connect(linkedButton, &QCheckBox::stateChanged, [this] {
		auto scene = obs_frontend_get_current_scene();
		if (!scene)
			return;
		SetLinkedScene(scene, linkedButton->isChecked()
					      ? scenesCombo->currentText()
					      : "");
		obs_source_release(scene);
	});

	sceneRow->addWidget(linkedButton);

	auto addButton = new QPushButton;
	addButton->setProperty("themeID", "addIconSmall");
	addButton->setToolTip(
		QString::fromUtf8(obs_module_text("AddVerticalScene")));
	connect(addButton, &QPushButton::clicked, [this] { AddScene(); });
	sceneRow->addWidget(addButton);
	auto removeButton = new QPushButton;
	removeButton->setProperty("themeID", "removeIconSmall");
	removeButton->setToolTip(
		QString::fromUtf8(obs_module_text("RemoveVerticalScene")));
	connect(removeButton, &QPushButton::clicked,
		[this] { RemoveScene(scenesCombo->currentText()); });
	sceneRow->addWidget(removeButton);
	mainLayout->insertLayout(0, sceneRow);
}

CanvasDock::CanvasDock(obs_data_t *settings, QWidget *parent)
	: QDockWidget(parent),
	  action(nullptr),
	  mainLayout(new QVBoxLayout(this)),
	  preview(new OBSQTDisplay(this)),
	  eventFilter(BuildEventFilter())
{
	setFeatures(DockWidgetClosable | DockWidgetMovable |
		    DockWidgetFloatable);

	if (!settings) {
		settings = obs_data_create();
		obs_data_set_bool(settings, "backtrack", true);
		first_time = true;
	}

	hideScenes = !obs_data_get_bool(settings, "show_scenes");
	canvas_width = (uint32_t)obs_data_get_int(settings, "width");
	canvas_height = (uint32_t)obs_data_get_int(settings, "height");
	if (!canvas_width || !canvas_height) {
		canvas_width = 1080;
		canvas_height = 1920;
	}
	videoBitrate = (uint32_t)obs_data_get_int(settings, "video_bitrate");
	audioBitrate = (uint32_t)obs_data_get_int(settings, "audio_bitrate");
	startReplay = obs_data_get_bool(settings, "backtrack");
	replayAlwaysOn = obs_data_get_bool(settings, "backtrack_always");
	replayDuration =
		(uint32_t)obs_data_get_int(settings, "backtrack_seconds");
	replayPath = obs_data_get_string(settings, "backtrack_path");

	stream_server = obs_data_get_string(settings, "stream_server");
	stream_key = obs_data_get_string(settings, "stream_key");

	stream_advanced_settings =
		obs_data_get_bool(settings, "stream_advanced_settings");
	stream_audio_track = obs_data_get_int(settings, "stream_audio_track");
	stream_encoder = obs_data_get_string(settings, "stream_encoder");
	stream_encoder_settings =
		obs_data_get_obj(settings, "stream_encoder_settings");
	if (!stream_encoder_settings)
		stream_encoder_settings = obs_data_create();

	recordPath = obs_data_get_string(settings, "record_path");
	record_advanced_settings =
		obs_data_get_bool(settings, "record_advanced_settings");
	filename_formatting =
		obs_data_get_string(settings, "filename_formatting");
	file_format = obs_data_get_string(settings, "file_format");
	record_audio_tracks = obs_data_get_int(settings, "record_audio_tracks");
	if (!record_audio_tracks)
		record_audio_tracks = 1;
	record_encoder = obs_data_get_string(settings, "record_encoder");
	record_encoder_settings =
		obs_data_get_obj(settings, "record_encoder_settings");
	if (!record_encoder_settings)
		record_encoder_settings = obs_data_create();

	preview_disabled = obs_data_get_bool(settings, "preview_disabled");

	virtual_cam_warned = obs_data_get_bool(settings, "virtual_cam_warned");

	if (!record_advanced_settings && (replayAlwaysOn || startReplay)) {
		const auto profile_config = obs_frontend_get_profile_config();
		if (!config_get_bool(profile_config, "AdvOut", "RecRB") ||
		    !config_get_bool(profile_config, "SimpleOutput", "RecRB")) {
			config_set_bool(profile_config, "AdvOut", "RecRB",
					true);
			config_set_bool(profile_config, "SimpleOutput", "RecRB",
					true);
			config_save(profile_config);
		}
	}

	size_t idx = 0;
	const char *id;
	while (obs_enum_transition_types(idx++, &id)) {
		if (obs_is_source_configurable(id))
			continue;
		const char *name = obs_source_get_display_name(id);

		OBSSourceAutoRelease tr =
			obs_source_create_private(id, name, NULL);
		transitions.emplace_back(tr);

		//signals "transition_stop" and "transition_video_stop"
		//        TransitionFullyStopped TransitionStopped
	}

	obs_data_array_t *transition_array =
		obs_data_get_array(settings, "transitions");
	if (transition_array) {
		size_t c = obs_data_array_count(transition_array);
		for (size_t i = 0; i < c; i++) {
			obs_data_t *td =
				obs_data_array_item(transition_array, i);
			if (!td)
				continue;
			OBSSourceAutoRelease transition =
				obs_load_private_source(td);
			if (transition)
				transitions.emplace_back(transition);

			obs_data_release(td);
		}
		obs_data_array_release(transition_array);
	}

	auto transition =
		GetTransition(obs_data_get_string(settings, "transition"));
	if (!transition)
		transition = GetTransition(
			obs_source_get_display_name("fade_transition"));

	SwapTransition(transition);

	const QString title = QString::fromUtf8(obs_module_text("Vertical"));
	setWindowTitle(title);

	const QString name = "CanvasDock" + QString::number(canvas_width) +
			     "x" + QString::number(canvas_height);
	setObjectName(name);
	setFloating(true);

	auto *dockWidgetContents = new QWidget;
	dockWidgetContents->setObjectName(QStringLiteral("contextContainer"));
	dockWidgetContents->setLayout(mainLayout);

	setWidget(dockWidgetContents);

	const QString replayName =
		title + " " + QString::fromUtf8(obs_module_text("Backtrack"));
	auto hotkeyData = obs_data_get_obj(settings, "backtrack_hotkeys");
	replayOutput = obs_output_create("replay_buffer",
					 replayName.toUtf8().constData(),
					 nullptr, hotkeyData);
	obs_data_release(hotkeyData);
	auto rpsh = obs_output_get_signal_handler(replayOutput);
	signal_handler_connect(rpsh, "saved", replay_saved, this);

	if (obs_data_get_bool(settings, "scenes_row")) {
		CreateScenesRow();
	}
	scenesDock = new CanvasScenesDock(this, parent);
	scenesDock->SetGridMode(obs_data_get_bool(settings, "grid_mode"));
	scenesDockAction =
		static_cast<QAction *>(obs_frontend_add_dock(scenesDock));

	sourcesDock = new CanvasSourcesDock(this, parent);
	sourcesDockAction =
		static_cast<QAction *>(obs_frontend_add_dock(sourcesDock));

	transitionsDock = new CanvasTransitionsDock(this, parent);
	transitionsDockAction =
		static_cast<QAction *>(obs_frontend_add_dock(transitionsDock));

	preview->setObjectName(QStringLiteral("preview"));
	preview->setMinimumSize(QSize(24, 24));
	QSizePolicy sizePolicy1(QSizePolicy::Expanding, QSizePolicy::Expanding);
	sizePolicy1.setHorizontalStretch(0);
	sizePolicy1.setVerticalStretch(0);
	sizePolicy1.setHeightForWidth(
		preview->sizePolicy().hasHeightForWidth());
	preview->setSizePolicy(sizePolicy1);

	preview->setMouseTracking(true);
	preview->setFocusPolicy(Qt::StrongFocus);
	preview->installEventFilter(eventFilter.get());

	auto addDrawCallback = [this]() {
		obs_display_add_draw_callback(preview->GetDisplay(),
					      DrawPreview, this);
	};
	preview->show();
	connect(preview, &OBSQTDisplay::DisplayCreated, addDrawCallback);
	preview->setVisible(!preview_disabled);
	obs_display_set_enabled(preview->GetDisplay(), !preview_disabled);

	auto addNudge = [this](const QKeySequence &seq, MoveDir direction,
			       int distance) {
		QAction *nudge = new QAction(preview);
		nudge->setShortcut(seq);
		nudge->setShortcutContext(Qt::WidgetShortcut);
		preview->addAction(nudge);
		connect(nudge, &QAction::triggered,
			[this, distance, direction]() {
				Nudge(distance, direction);
			});
	};

	addNudge(Qt::Key_Up, MoveDir::Up, 1);
	addNudge(Qt::Key_Down, MoveDir::Down, 1);
	addNudge(Qt::Key_Left, MoveDir::Left, 1);
	addNudge(Qt::Key_Right, MoveDir::Right, 1);
	addNudge(Qt::SHIFT | Qt::Key_Up, MoveDir::Up, 10);
	addNudge(Qt::SHIFT | Qt::Key_Down, MoveDir::Down, 10);
	addNudge(Qt::SHIFT | Qt::Key_Left, MoveDir::Left, 10);
	addNudge(Qt::SHIFT | Qt::Key_Right, MoveDir::Right, 10);

	mainLayout->addWidget(preview, 1);

	previewDisabledWidget = new QFrame;
	auto l = new QVBoxLayout;

	auto enablePreviewButton = new QPushButton(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.Main.PreviewConextMenu.Enable")));
	connect(enablePreviewButton, &QPushButton::clicked, [this] {
		preview_disabled = false;
		obs_display_set_enabled(preview->GetDisplay(), true);
		preview->setVisible(true);
		previewDisabledWidget->setVisible(false);
	});
	l->addWidget(enablePreviewButton);

	previewDisabledWidget->setLayout(l);

	previewDisabledWidget->setVisible(preview_disabled);
	mainLayout->addWidget(previewDisabledWidget, 1);

	QSizePolicy sp2;
	sp2.setHeightForWidth(true);

	auto buttonRow = new QHBoxLayout(this);

	streamButton = new QPushButton;
	streamButton->setObjectName(QStringLiteral("canvasStream"));
	streamButton->setIcon(streamInactiveIcon);
	streamButton->setCheckable(true);
	streamButton->setChecked(false);
	streamButton->setSizePolicy(sp2);
	streamButton->setToolTip(
		QString::fromUtf8(obs_module_text("StreamVertical")));
	connect(streamButton, SIGNAL(clicked()), this,
		SLOT(StreamButtonClicked()));
	buttonRow->addWidget(streamButton);

	recordButton = new QPushButton;
	recordButton->setObjectName(QStringLiteral("canvasRecord"));
	recordButton->setIcon(recordInactiveIcon);
	recordButton->setCheckable(true);
	recordButton->setChecked(false);
	recordButton->setSizePolicy(sp2);
	recordButton->setToolTip(
		QString::fromUtf8(obs_module_text("RecordVertical")));
	connect(recordButton, SIGNAL(clicked()), this,
		SLOT(RecordButtonClicked()));
	buttonRow->addWidget(recordButton);

	replayButton = new QPushButton;
	replayButton->setObjectName(QStringLiteral("canvasReplay"));
	replayButton->setIcon(replayInactiveIcon);
	replayButton->setContentsMargins(0, 0, 0, 0);
	replayButton->setSizePolicy(sp2);
	replayButton->setToolTip(
		QString::fromUtf8(obs_module_text("BacktrackClipVertical")));
	connect(replayButton, SIGNAL(clicked()), this,
		SLOT(ReplayButtonClicked()));
	buttonRow->addWidget(replayButton);

	virtualCamButton = new QPushButton;
	virtualCamButton->setObjectName(QStringLiteral("canvasVirtualCam"));
	virtualCamButton->setIcon(virtualCamInactiveIcon);
	virtualCamButton->setCheckable(true);
	virtualCamButton->setChecked(false);
	virtualCamButton->setSizePolicy(sp2);
	virtualCamButton->setToolTip(
		QString::fromUtf8(obs_module_text("VirtualCameraVertical")));
	if (obs_get_version() < MAKE_SEMANTIC_VERSION(29, 1, 0) &&
	    strncmp(obs_get_version_string(), "29.1.", 5) != 0) {
		virtualCamButton->setVisible(false);
	}
	connect(virtualCamButton, SIGNAL(clicked()), this,
		SLOT(VirtualCamButtonClicked()));
	buttonRow->addWidget(virtualCamButton);

	statusLabel = new QLabel;
	statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	buttonRow->addWidget(statusLabel, 1);

	recordDurationTimer.setInterval(1000);
	recordDurationTimer.setSingleShot(false);
	connect(&recordDurationTimer, &QTimer::timeout, [this] {
		if (obs_output_active(recordOutput)) {
			int totalFrames =
				obs_output_get_total_frames(recordOutput);
			video_t *video = obs_output_video(recordOutput);
			uint64_t frameTimeNs =
				video_output_get_frame_time(video);
			auto t = QTime::fromMSecsSinceStartOfDay(util_mul_div64(
				totalFrames, frameTimeNs, 1000000ULL));
			recordButton->setText(
				t.toString(t.hour() ? "hh:mm:ss" : "mm:ss"));
		} else if (!recordButton->text().isEmpty()) {
			recordButton->setText("");
		}
		if (obs_output_active(streamOutput)) {
			int totalFrames =
				obs_output_get_total_frames(streamOutput);
			video_t *video = obs_output_video(streamOutput);
			uint64_t frameTimeNs =
				video_output_get_frame_time(video);
			auto t = QTime::fromMSecsSinceStartOfDay(util_mul_div64(
				totalFrames, frameTimeNs, 1000000ULL));
			streamButton->setText(
				t.toString(t.hour() ? "hh:mm:ss" : "mm:ss"));
		} else if (!streamButton->text().isEmpty()) {
			streamButton->setText("");
		}
	});
	recordDurationTimer.start();

	replayStatusResetTimer.setInterval(4000);
	replayStatusResetTimer.setSingleShot(true);
	connect(&replayStatusResetTimer, &QTimer::timeout,
		[this] { statusLabel->setText(""); });

	configButton = new QPushButton(this);
	configButton->setProperty("themeID", "configIconSmall");
	configButton->setFlat(true);
	configButton->setAutoDefault(false);
	configButton->setSizePolicy(sp2);
	configButton->setToolTip(
		QString::fromUtf8(obs_module_text("VerticalSettings")));
	connect(configButton, SIGNAL(clicked()), this,
		SLOT(ConfigButtonClicked()));
	buttonRow->addWidget(configButton);

	auto aitumButton = new QPushButton;
	aitumButton->setSizePolicy(sp2);
	aitumButton->setIcon(QIcon(":/aitum/media/aitum.png"));
	aitumButton->setToolTip(QString::fromUtf8("https://aitum.tv"));
	connect(aitumButton, &QPushButton::clicked,
		[] { QDesktopServices::openUrl(QUrl("https://aitum.tv")); });
	buttonRow->addWidget(aitumButton);

	mainLayout->addLayout(buttonRow);

	obs_enter_graphics();

	gs_render_start(true);
	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(0.0f, 1.0f);
	gs_vertex2f(1.0f, 0.0f);
	gs_vertex2f(1.0f, 1.0f);
	box = gs_render_save();

	obs_leave_graphics();

	currentSceneName = QString::fromUtf8(
		obs_data_get_string(settings, "current_scene"));

	auto sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_rename", source_rename, this);
	signal_handler_connect(sh, "source_remove", source_remove, this);
	signal_handler_connect(sh, "source_destroy", source_remove, this);
	//signal_handler_connect(sh, "source_create", source_create, this);
	//signal_handler_connect(sh, "source_load", source_load, this);
	signal_handler_connect(sh, "source_save", source_save, this);

	virtual_cam_hotkey = obs_hotkey_pair_register_frontend(
		(name + "StartVirtualCam").toUtf8().constData(),
		(title + " " +
		 QString::fromUtf8(obs_frontend_get_locale_string(
			 "Basic.Main.StartVirtualCam")))
			.toUtf8()
			.constData(),
		(name + "StopVirtualCam").toUtf8().constData(),
		(title + " " +
		 QString::fromUtf8(obs_frontend_get_locale_string(
			 "Basic.Main.StopVirtualCam")))
			.toUtf8()
			.constData(),
		start_virtual_cam_hotkey, stop_virtual_cam_hotkey, this, this);

	obs_data_array_t *start_hotkey =
		obs_data_get_array(settings, "start_virtual_cam_hotkey");
	obs_data_array_t *stop_hotkey =
		obs_data_get_array(settings, "stop_virtual_cam_hotkey");
	obs_hotkey_pair_load(virtual_cam_hotkey, start_hotkey, stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);

	record_hotkey = obs_hotkey_pair_register_frontend(
		(name + "StartRecording").toUtf8().constData(),
		(title + " " +
		 QString::fromUtf8(obs_frontend_get_locale_string(
			 "Basic.Main.StartRecording")))
			.toUtf8()
			.constData(),
		(name + "StopRecording").toUtf8().constData(),
		(title + " " +
		 QString::fromUtf8(obs_frontend_get_locale_string(
			 "Basic.Main.StopRecording")))
			.toUtf8()
			.constData(),
		start_recording_hotkey, stop_recording_hotkey, this, this);

	start_hotkey = obs_data_get_array(settings, "start_record_hotkey");
	stop_hotkey = obs_data_get_array(settings, "stop_record_hotkey");
	obs_hotkey_pair_load(record_hotkey, start_hotkey, stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);

	stream_hotkey = obs_hotkey_pair_register_frontend(
		(name + "StartStreaming").toUtf8().constData(),
		(title + " " +
		 QString::fromUtf8(obs_frontend_get_locale_string(
			 "Basic.Main.StartStreaming")))
			.toUtf8()
			.constData(),
		(name + "StopStreaming").toUtf8().constData(),
		(title + " " +
		 QString::fromUtf8(obs_frontend_get_locale_string(
			 "Basic.Main.StopStreaming")))
			.toUtf8()
			.constData(),
		start_streaming_hotkey, stop_streaming_hotkey, this, this);

	start_hotkey = obs_data_get_array(settings, "start_stream_hotkey");
	stop_hotkey = obs_data_get_array(settings, "stop_stream_hotkey");
	obs_hotkey_pair_load(stream_hotkey, start_hotkey, stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);
	if (first_time) {
		obs_data_release(settings);
	}
	hide();

	stream_service = obs_service_create("rtmp_custom",
					    "vertical_canvas_stream_service",
					    nullptr, nullptr);

	transitionAudioWrapper = obs_source_create_private(
		"vertical_audio_wrapper_source",
		"vertical_audio_wrapper_source", nullptr);
	auto aw = (struct audio_wrapper_info *)obs_obj_get_data(
		transitionAudioWrapper);
	aw->param = this;
	aw->target = [](void *param) {
		CanvasDock *dock = reinterpret_cast<CanvasDock *>(param);
		return obs_weak_source_get_source(dock->source);
	};
	StartVideo();
}

CanvasDock::~CanvasDock()
{
	for (auto projector : projectors) {
		delete projector;
	}
	canvas_docks.remove(this);
	obs_hotkey_pair_unregister(virtual_cam_hotkey);
	obs_hotkey_pair_unregister(record_hotkey);
	obs_hotkey_pair_unregister(stream_hotkey);
	obs_display_remove_draw_callback(preview->GetDisplay(), DrawPreview,
					 this);
	for (uint32_t i = MAX_CHANNELS - 1; i > 0; i--) {
		if (obs_get_output_source(i) == transitionAudioWrapper) {
			obs_set_output_source(i, nullptr);
			break;
		}
	}
	obs_source_release(transitionAudioWrapper);
	delete action;
	sourcesDock->deleteLater();
	sourcesDock = nullptr;
	delete sourcesDockAction;
	scenesDock->deleteLater();
	scenesDock = nullptr;
	delete scenesDockAction;
	transitionsDock->deleteLater();
	transitionsDock = nullptr;
	delete transitionsDockAction;
	auto sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_rename", source_rename, this);
	signal_handler_disconnect(sh, "source_remove", source_remove, this);
	signal_handler_disconnect(sh, "source_destroy", source_remove, this);
	//signal_handler_connect(sh, "source_create", source_create, this);
	//signal_handler_disconnect(sh, "source_load", source_load, this);
	signal_handler_disconnect(sh, "source_save", source_save, this);

	if (obs_output_active(recordOutput))
		obs_output_stop(recordOutput);
	obs_output_release(recordOutput);

	if (obs_output_active(replayOutput))
		obs_output_stop(replayOutput);
	obs_output_release(replayOutput);

	if (obs_output_active(virtualCamOutput))
		obs_output_stop(virtualCamOutput);
	obs_output_release(virtualCamOutput);

	if (obs_output_active(streamOutput))
		obs_output_stop(streamOutput);
	obs_output_release(streamOutput);

	obs_data_release(stream_encoder_settings);
	obs_data_release(record_encoder_settings);
	obs_service_release(stream_service);

	if (video) {
		obs_view_remove(view);
		obs_view_set_source(view, 0, nullptr);
		video = nullptr;
	}
	obs_view_destroy(view);

	obs_enter_graphics();

	if (overflow)
		gs_texture_destroy(overflow);
	if (rectFill)
		gs_vertexbuffer_destroy(rectFill);
	if (circleFill)
		gs_vertexbuffer_destroy(circleFill);

	gs_vertexbuffer_destroy(box);
	obs_leave_graphics();

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

void CanvasDock::DrawOverflow(float scale)
{
	if (locked)
		return;

	bool hidden = config_get_bool(obs_frontend_get_global_config(),
				      "BasicWindow", "OverflowHidden");

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

bool CanvasDock::DrawSelectedOverflow(obs_scene_t *scene, obs_sceneitem_t *item,
				      void *param)
{
	if (obs_sceneitem_locked(item))
		return true;

	if (!SceneItemHasVideo(item))
		return true;

	bool select = config_get_bool(obs_frontend_get_global_config(),
				      "BasicWindow", "OverflowSelectionHidden");

	if (!select && !obs_sceneitem_visible(item))
		return true;

	if (obs_sceneitem_is_group(item)) {
		matrix4 mat;
		obs_sceneitem_get_draw_transform(item, &mat);

		gs_matrix_push();
		gs_matrix_mul(&mat);
		obs_sceneitem_group_enum_items(item, DrawSelectedOverflow,
					       param);
		gs_matrix_pop();
	}

	bool always = config_get_bool(obs_frontend_get_global_config(),
				      "BasicWindow", "OverflowAlwaysVisible");

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

	bool visible = std::all_of(
		std::begin(bounds), std::end(bounds), [&](const vec3 &b) {
			vec3 pos;
			vec3_transform(&pos, &b, &boxTransform);
			vec3_transform(&pos, &pos, &invBoxTransform);
			return CloseFloat(pos.x, b.x) && CloseFloat(pos.y, b.y);
		});

	if (!visible)
		return true;

	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "DrawSelectedOverflow");

	obs_transform_info info;
	obs_sceneitem_get_info(item, &info);

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

	if (!window->source)
		return;
	auto source = obs_weak_source_get_source(window->source);
	if (!source)
		return;
	uint32_t sourceCX = obs_source_get_width(source);
	if (sourceCX <= 0)
		sourceCX = 1;
	uint32_t sourceCY = obs_source_get_height(source);
	if (sourceCY <= 0)
		sourceCY = 1;

	int x, y;
	float scale;

	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);
	if (window->previewScale != scale)
		window->previewScale = scale;
	auto newCX = scale * float(sourceCX);
	auto newCY = scale * float(sourceCY);

	/*auto extraCx = (window->zoom - 1.0f) * newCX;
	auto extraCy = (window->zoom - 1.0f) * newCY;
	int newCx = newCX * window->zoom;
	int newCy = newCY * window->zoom;
	x -= extraCx * window->scrollX;
	y -= extraCy * window->scrollY;*/
	gs_viewport_push();
	gs_projection_push();

	gs_ortho(float(-x), newCX + float(x), float(-y), newCY + float(y),
		 -100.0f, 100.0f);
	gs_reset_viewport();

	window->DrawOverflow(scale);

	window->DrawBackdrop(newCX, newCY);

	const bool previous = gs_set_linear_srgb(true);

	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(source);
	obs_source_release(source);

	gs_set_linear_srgb(previous);

	gs_ortho(float(-x), newCX + float(x), float(-y), newCY + float(y),
		 -100.0f, 100.0f);
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

		window->DrawSelectionBox(window->startPos.x * scale,
					 window->startPos.y * scale,
					 window->mousePos.x * scale,
					 window->mousePos.y * scale,
					 window->rectFill);
	}
	gs_load_vertexbuffer(nullptr);

	gs_technique_end_pass(tech);
	gs_technique_end(tech);

	if (window->drawSpacingHelpers)
		window->DrawSpacingHelpers(window->scene, x, y, newCX, newCY,
					   scale, float(sourceCX),
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

	inline SceneFindData(const vec2 &pos_, bool selectBelow_)
		: pos(pos_),
		  selectBelow(selectBelow_)
	{
	}
};

struct SceneFindBoxData {
	const vec2 &startPos;
	const vec2 &pos;
	std::vector<obs_sceneitem_t *> sceneItems;

	SceneFindBoxData(const SceneFindData &) = delete;
	SceneFindBoxData(SceneFindData &&) = delete;
	SceneFindBoxData &operator=(const SceneFindData &) = delete;
	SceneFindBoxData &operator=(SceneFindData &&) = delete;

	inline SceneFindBoxData(const vec2 &startPos_, const vec2 &pos_)
		: startPos(startPos_),
		  pos(pos_)
	{
	}
};

bool CanvasDock::FindSelected(obs_scene_t *scene, obs_sceneitem_t *item,
			      void *param)
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
		size.x = float(obs_source_get_width(source) - crop.left -
			       crop.right) *
			 scale.x;
		size.y = float(obs_source_get_height(source) - crop.top -
			       crop.bottom) *
			 scale.y;
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

static void DrawLine(float x1, float y1, float x2, float y2, float thickness,
		     vec2 scale)
{
	float ySide = (y1 == y2) ? (y1 < 0.5f ? 1.0f : -1.0f) : 0.0f;
	float xSide = (x1 == x2) ? (x1 < 0.5f ? 1.0f : -1.0f) : 0.0f;

	gs_render_start(true);

	gs_vertex2f(x1, y1);
	gs_vertex2f(x1 + (xSide * (thickness / scale.x)),
		    y1 + (ySide * (thickness / scale.y)));
	gs_vertex2f(x2 + (xSide * (thickness / scale.x)),
		    y2 + (ySide * (thickness / scale.y)));
	gs_vertex2f(x2, y2);
	gs_vertex2f(x1, y1);

	gs_vertbuffer_t *line = gs_render_save();

	gs_load_vertexbuffer(line);
	gs_draw(GS_TRISTRIP, 0, 0);
	gs_vertexbuffer_destroy(line);
}

void CanvasDock::DrawSpacingLine(vec3 &start, vec3 &end, vec3 &viewport,
				 float pixelRatio)
{
	matrix4 transform;
	matrix4_identity(&transform);
	transform.x.x = viewport.x;
	transform.y.y = viewport.y;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

	QColor selColor = GetSelectionColor();
	vec4 color;
	vec4_set(&color, selColor.redF(), selColor.greenF(), selColor.blueF(),
		 1.0f);

	gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &color);

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	gs_matrix_push();
	gs_matrix_mul(&transform);

	vec2 scale;
	vec2_set(&scale, viewport.x, viewport.y);

	DrawLine(start.x, start.y, end.x, end.y,
		 pixelRatio * (HANDLE_RADIUS / 2), scale);

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

	obs_source_t *source = spacerLabel[sourceIndex];

	OBSDataAutoRelease settings = obs_source_get_settings(source);
	obs_data_set_string(settings, "text", text.c_str());
	obs_source_update(source, settings);

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

void CanvasDock::RenderSpacingHelper(int sourceIndex, vec3 &start, vec3 &end,
				     vec3 &viewport, float pixelRatio)
{
	bool horizontal = (sourceIndex == 2 || sourceIndex == 3);

	// If outside of preview, don't render
	if (!((horizontal && (end.x >= start.x)) ||
	      (!horizontal && (end.y >= start.y))))
		return;

	float length = vec3_dist(&start, &end);

	float px;

	if (horizontal) {
		px = length * canvas_width;
	} else {
		px = length * canvas_height;
	}

	if (px <= 0.0f)
		return;

	obs_source_t *source = spacerLabel[sourceIndex];
	vec3 labelSize, labelPos;
	vec3_set(&labelSize, obs_source_get_width(source),
		 obs_source_get_height(source), 1.0f);

	vec3_div(&labelSize, &labelSize, &viewport);

	vec3 labelMargin;
	vec3_set(&labelMargin, SPACER_LABEL_MARGIN * pixelRatio,
		 SPACER_LABEL_MARGIN * pixelRatio, 1.0f);
	vec3_div(&labelMargin, &labelMargin, &viewport);

	vec3_set(&labelPos, end.x, end.y, end.z);
	if (horizontal) {
		labelPos.x -= (end.x - start.x) / 2;
		labelPos.x -= labelSize.x / 2;
		labelPos.y -= labelMargin.y + (labelSize.y / 2) +
			      (HANDLE_RADIUS / viewport.y);
	} else {
		labelPos.y -= (end.y - start.y) / 2;
		labelPos.y -= labelSize.y / 2;
		labelPos.x += labelMargin.x;
	}

	DrawSpacingLine(start, end, viewport, pixelRatio);
	SetLabelText(sourceIndex, (int)px);
	DrawLabel(source, labelPos, viewport);
}

static obs_source_t *CreateLabel(float pixelRatio)
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
	obs_data_set_int(font, "size", 16 * pixelRatio);

	obs_data_set_obj(settings, "font", font);
	obs_data_set_bool(settings, "outline", true);

#ifdef _WIN32
	obs_data_set_int(settings, "outline_color", 0x000000);
	obs_data_set_int(settings, "outline_size", 3);
	const char *text_source_id = "text_gdiplus";
#else
	const char *text_source_id = "text_ft2_source";
#endif

	OBSSource txtSource =
		obs_source_create_private(text_source_id, nullptr, settings);

	return txtSource;
}

obs_scene_item *CanvasDock::GetSelectedItem(obs_scene_t *s)
{
	vec2 pos;
	SceneFindBoxData data(pos, pos);

	if (!s)
		s = this->scene;
	obs_scene_enum_items(s, FindSelected, &data);

	if (data.sceneItems.size() != 1)
		return nullptr;

	return data.sceneItems.at(0);
}

void CanvasDock::DrawSpacingHelpers(obs_scene_t *scene, float x, float y,
				    float cx, float cy, float scale,
				    float sourceX, float sourceY)
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

	obs_sceneitem_t *parentGroup = obs_sceneitem_get_group(scene, item);

	if (parentGroup && obs_sceneitem_locked(parentGroup))
		return;

	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	obs_transform_info oti;
	obs_sceneitem_get_info(item, &oti);

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
	float rot = oti.rot;

	if (parentGroup) {
		obs_transform_info groupOti;
		obs_sceneitem_get_info(parentGroup, &groupOti);

		//Correct the scene item rotation angle
		rot = oti.rot + groupOti.rot;

		// Correct the scene item box transform
		// Based on scale, rotation angle, position of parent's group
		matrix4_scale3f(&boxTransform, &boxTransform, groupOti.scale.x,
				groupOti.scale.y, 1.0f);
		matrix4_rotate_aa4f(&boxTransform, &boxTransform, 0.0f, 0.0f,
				    1.0f, RAD(groupOti.rot));
		matrix4_translate3f(&boxTransform, &boxTransform,
				    groupOti.pos.x, groupOti.pos.y, 0.0f);
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
		for (float i = -HELPER_ROT_BREAKPONT; i >= -360.0f;
		     i -= 90.0f) {
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

	// Switch top/bottom or right/left if scale is negative
	if (oti.scale.x < 0.0f) {
		vec3 l = left;
		vec3 r = right;

		vec3_copy(&left, &r);
		vec3_copy(&right, &l);
	}

	if (oti.scale.y < 0.0f) {
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
	for (int i = 0; i < 4; i++) {
		if (!spacerLabel[i])
			spacerLabel[i] = CreateLabel(pixelRatio);
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
	return crop->left > 0 || crop->top > 0 || crop->right > 0 ||
	       crop->bottom > 0;
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

	gs_matrix_translate3f(-HANDLE_RADIUS * pixelRatio,
			      -HANDLE_RADIUS * pixelRatio, 0.0f);
	gs_matrix_scale3f(HANDLE_RADIUS * pixelRatio * 2,
			  HANDLE_RADIUS * pixelRatio * 2, 1.0f);
	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_pop();
}

static void DrawRotationHandle(gs_vertbuffer_t *circle, float rot,
			       float pixelRatio)
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
	gs_matrix_translate3f(-HANDLE_RADIUS * 1.5 * pixelRatio,
			      -HANDLE_RADIUS * 1.5 * pixelRatio, 0.0f);
	gs_matrix_scale3f(HANDLE_RADIUS * 3 * pixelRatio,
			  HANDLE_RADIUS * 3 * pixelRatio, 1.0f);

	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_translate3f(0.0f, -HANDLE_RADIUS * 2 / 3, 0.0f);

	gs_load_vertexbuffer(circle);
	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_pop();
	gs_vertexbuffer_destroy(line);
}

static void DrawStripedLine(float x1, float y1, float x2, float y2,
			    float thickness, vec2 scale)
{
	float ySide = (y1 == y2) ? (y1 < 0.5f ? 1.0f : -1.0f) : 0.0f;
	float xSide = (x1 == x2) ? (x1 < 0.5f ? 1.0f : -1.0f) : 0.0f;

	float dist =
		sqrt(pow((x1 - x2) * scale.x, 2) + pow((y1 - y2) * scale.y, 2));
	float offX = (x2 - x1) / dist;
	float offY = (y2 - y1) / dist;

	for (int i = 0, l = ceil(dist / 15); i < l; i++) {
		gs_render_start(true);

		float xx1 = x1 + i * 15 * offX;
		float yy1 = y1 + i * 15 * offY;

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
		gs_vertex2f(xx1 + (xSide * (thickness / scale.x)),
			    yy1 + (ySide * (thickness / scale.y)));
		gs_vertex2f(dx, dy);
		gs_vertex2f(dx + (xSide * (thickness / scale.x)),
			    dy + (ySide * (thickness / scale.y)));

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

bool CanvasDock::DrawSelectedItem(obs_scene_t *scene, obs_sceneitem_t *item,
				  void *param)
{
	if (obs_sceneitem_locked(item))
		return true;

	if (!SceneItemHasVideo(item))
		return true;

	CanvasDock *window = static_cast<CanvasDock *>(param);

	if (obs_sceneitem_is_group(item)) {
		matrix4 mat;
		obs_transform_info groupInfo;
		obs_sceneitem_get_draw_transform(item, &mat);
		obs_sceneitem_get_info(item, &groupInfo);

		window->groupRot = groupInfo.rot;

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
		for (size_t i = 0; i < window->hoveredPreviewItems.size();
		     i++) {
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

	vec4_set(&red, selColor.redF(), selColor.greenF(), selColor.blueF(),
		 1.0f);
	vec4_set(&green, cropColor.redF(), cropColor.greenF(),
		 cropColor.blueF(), 1.0f);
	vec4_set(&blue, hoverColor.redF(), hoverColor.greenF(),
		 hoverColor.blueF(), 1.0f);

	bool visible = std::all_of(
		std::begin(bounds), std::end(bounds), [&](const vec3 &b) {
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

	obs_transform_info info;
	obs_sceneitem_get_info(item, &info);

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);

	gs_effect_t *eff = gs_get_effect();
	gs_eparam_t *colParam = gs_effect_get_param_by_name(eff, "color");

	gs_effect_set_vec4(colParam, &red);

	if (info.bounds_type == OBS_BOUNDS_NONE && crop_enabled(&crop)) {
#define DRAW_SIDE(side, x1, y1, x2, y2)                                        \
	if (hovered && !selected) {                                            \
		gs_effect_set_vec4(colParam, &blue);                           \
		DrawLine(x1, y1, x2, y2, HANDLE_RADIUS *pixelRatio / 2,        \
			 boxScale);                                            \
	} else if (crop.side > 0) {                                            \
		gs_effect_set_vec4(colParam, &green);                          \
		DrawStripedLine(x1, y1, x2, y2, HANDLE_RADIUS *pixelRatio / 2, \
				boxScale);                                     \
	} else {                                                               \
		DrawLine(x1, y1, x2, y2, HANDLE_RADIUS *pixelRatio / 2,        \
			 boxScale);                                            \
	}                                                                      \
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
				gs_vertex2f(sin(RAD(angle)) / 2 + 0.5f,
					    cos(RAD(angle)) / 2 + 0.5f);
				angle += 360 / l;
				gs_vertex2f(sin(RAD(angle)) / 2 + 0.5f,
					    cos(RAD(angle)) / 2 + 0.5f);
				gs_vertex2f(0.5f, 1.0f);
			}

			window->circleFill = gs_render_save();
		}

		DrawRotationHandle(window->circleFill,
				   info.rot + window->groupRot, pixelRatio);
	}

	gs_matrix_pop();

	GS_DEBUG_MARKER_END();

	UNUSED_PARAMETER(scene);
	return true;
}

static inline QColor color_from_int(long long val)
{
	return QColor(val & 0xff, (val >> 8) & 0xff, (val >> 16) & 0xff,
		      (val >> 24) & 0xff);
}

QColor CanvasDock::GetSelectionColor() const
{
	if (config_get_bool(obs_frontend_get_global_config(), "Accessibility",
			    "OverrideColors")) {
		return color_from_int(
			config_get_int(obs_frontend_get_global_config(),
				       "Accessibility", "SelectRed"));
	}
	return QColor::fromRgb(255, 0, 0);
}

QColor CanvasDock::GetCropColor() const
{
	if (config_get_bool(obs_frontend_get_global_config(), "Accessibility",
			    "OverrideColors")) {
		return color_from_int(
			config_get_int(obs_frontend_get_global_config(),
				       "Accessibility", "SelectGreen"));
	}
	return QColor::fromRgb(0, 255, 0);
}

QColor CanvasDock::GetHoverColor() const
{
	if (config_get_bool(obs_frontend_get_global_config(), "Accessibility",
			    "OverrideColors")) {
		return color_from_int(
			config_get_int(obs_frontend_get_global_config(),
				       "Accessibility", "SelectBlue"));
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
			return this->HandleMousePressEvent(
				static_cast<QMouseEvent *>(event));
		case QEvent::MouseButtonRelease:
			return this->HandleMouseReleaseEvent(
				static_cast<QMouseEvent *>(event));
		//case QEvent::MouseButtonDblClick:			return this->HandleMouseClickEvent(				static_cast<QMouseEvent *>(event));
		case QEvent::MouseMove:
			return this->HandleMouseMoveEvent(
				static_cast<QMouseEvent *>(event));
		//case QEvent::Enter:
		case QEvent::Leave:
			return this->HandleMouseLeaveEvent(
				static_cast<QMouseEvent *>(event));
		case QEvent::Wheel:
			return this->HandleMouseWheelEvent(
				static_cast<QWheelEvent *>(event));
		//case QEvent::FocusIn:
		//case QEvent::FocusOut:
		case QEvent::KeyPress:
			return this->HandleKeyPressEvent(
				static_cast<QKeyEvent *>(event));
		case QEvent::KeyRelease:
			return this->HandleKeyReleaseEvent(
				static_cast<QKeyEvent *>(event));
		default:
			return false;
		}
	});
}

bool CanvasDock::GetSourceRelativeXY(int mouseX, int mouseY, int &relX,
				     int &relY)
{
	float pixelRatio = devicePixelRatioF();

	int mouseXscaled = (int)roundf(mouseX * pixelRatio);
	int mouseYscaled = (int)roundf(mouseY * pixelRatio);

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

	GetScaleAndCenterPos(sourceCX, sourceCY, size.width(), size.height(), x,
			     y, scale);

	auto newCX = scale * float(sourceCX);
	auto newCY = scale * float(sourceCY);

	auto extraCx = /*(zoom - 1.0f) **/ newCX;
	auto extraCy = /*(zoom - 1.0f) **/ newCY;

	//scale *= zoom;
	float scrollX = 0.5f;
	float scrollY = 0.5f;

	if (x > 0) {
		relX = int(float(mouseXscaled - x + extraCx * scrollX) / scale);
		relY = int(float(mouseYscaled + extraCy * scrollY) / scale);
	} else {
		relX = int(float(mouseXscaled + extraCx * scrollX) / scale);
		relY = int(float(mouseYscaled - y + extraCy * scrollY) / scale);
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

	if (scrollMode && IsFixedScaling() &&
	    event->button() == Qt::LeftButton) {
		setCursor(Qt::ClosedHandCursor);
		scrollingFrom.x = pos.x();
		scrollingFrom.y = pos.y();
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

	if (event->button() != Qt::LeftButton &&
	    event->button() != Qt::RightButton)
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
		SceneFindBoxData data(s, s);

		obs_scene_enum_items(scene, FindSelected, &data);

		std::lock_guard<std::mutex> lock(selectMutex);
		selectedItems = data.sceneItems;
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

static bool RotateSelectedSources(obs_scene_t *scene, obs_sceneitem_t *item,
				  void *param)
{
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, RotateSelectedSources,
					       param);
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

static bool MultiplySelectedItemScale(obs_scene_t *scene, obs_sceneitem_t *item,
				      void *param)
{
	vec2 &mul = *reinterpret_cast<vec2 *>(param);

	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, MultiplySelectedItemScale,
					       param);
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

static bool CenterAlignSelectedItems(obs_scene_t *scene, obs_sceneitem_t *item,
				     void *param)
{
	obs_bounds_type boundsType =
		*reinterpret_cast<obs_bounds_type *>(param);

	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, CenterAlignSelectedItems,
					       param);
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

	vec2_set(&itemInfo.bounds,
		 float(obs_source_get_base_width(scene_source)),
		 float(obs_source_get_base_height(scene_source)));
	itemInfo.bounds_type = boundsType;
	itemInfo.bounds_alignment = OBS_ALIGN_CENTER;

	obs_sceneitem_set_info(item, &itemInfo);

	UNUSED_PARAMETER(scene);
	return true;
}

static bool GetSelectedItemsWithSize(obs_scene_t *scene, obs_sceneitem_t *item,
				     void *param)
{
	auto items = static_cast<std::vector<obs_sceneitem_t *> *>(param);

	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, GetSelectedItemsWithSize,
					       param);
	if (!obs_sceneitem_selected(item))
		return true;
	if (obs_sceneitem_locked(item))
		return true;

	obs_transform_info oti;
	obs_sceneitem_get_info(item, &oti);

	obs_source_t *source = obs_sceneitem_get_source(item);
	const float width = float(obs_source_get_width(source)) * oti.scale.x;
	const float height = float(obs_source_get_height(source)) * oti.scale.y;

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
	vec3_set(&screenCenter, float(canvas_width), float(canvas_height),
		 0.0f);

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

	obs_source_t *source = obs_sceneitem_get_source(sceneItem);

	popup->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string("Rename")),
		[this, sceneItem] {
			obs_source_t *source = obs_source_get_ref(
				obs_sceneitem_get_source(sceneItem));
			if (!source)
				return;
			std::string name = obs_source_get_name(source);
			obs_source_t *s = nullptr;
			do {
				obs_source_release(s);
				if (!NameDialog::AskForName(
					    this,
					    QString::fromUtf8(obs_module_text(
						    "SourceName")),
					    name)) {
					break;
				}
				s = obs_get_source_by_name(name.c_str());
				if (s)
					continue;
				obs_source_set_name(source, name.c_str());
			} while (s);
			obs_source_release(source);
		});
	popup->addAction(
		//removeButton->icon(),
		QString::fromUtf8(obs_frontend_get_locale_string("Remove")),
		this, [sceneItem] {
			QMessageBox mb(
				QMessageBox::Question,
				QString::fromUtf8(
					obs_frontend_get_locale_string(
						"ConfirmRemove.Title")),
				QString::fromUtf8(
					obs_frontend_get_locale_string(
						"ConfirmRemove.Text"))
					.arg(QString::fromUtf8(
						obs_source_get_name(
							obs_sceneitem_get_source(
								sceneItem)))),
				QMessageBox::StandardButtons(QMessageBox::Yes |
							     QMessageBox::No));
			mb.setDefaultButton(QMessageBox::NoButton);
			if (mb.exec() == QMessageBox::Yes) {
				obs_sceneitem_remove(sceneItem);
			}
		});

	popup->addSeparator();
	auto orderMenu = popup->addMenu(QString::fromUtf8(
		obs_frontend_get_locale_string("Basic.MainMenu.Edit.Order")));
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string(
				     "Basic.MainMenu.Edit.Order.MoveUp")),
			     this, [sceneItem] {
				     obs_sceneitem_set_order(sceneItem,
							     OBS_ORDER_MOVE_UP);
			     });
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string(
				     "Basic.MainMenu.Edit.Order.MoveDown")),
			     this, [sceneItem] {
				     obs_sceneitem_set_order(
					     sceneItem, OBS_ORDER_MOVE_DOWN);
			     });
	orderMenu->addSeparator();
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string(
				     "Basic.MainMenu.Edit.Order.MoveToTop")),
			     this, [sceneItem] {
				     obs_sceneitem_set_order(
					     sceneItem, OBS_ORDER_MOVE_TOP);
			     });
	orderMenu->addAction(QString::fromUtf8(obs_frontend_get_locale_string(
				     "Basic.MainMenu.Edit.Order.MoveToBottom")),
			     this, [sceneItem] {
				     obs_sceneitem_set_order(
					     sceneItem, OBS_ORDER_MOVE_BOTTOM);
			     });

	auto transformMenu =
		popup->addMenu(QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform")));
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.EditTransform")),
		[this, sceneItem] {
			const auto mainDialog = static_cast<QMainWindow *>(
				obs_frontend_get_main_window());
			auto transformDialog = mainDialog->findChild<QDialog *>(
				"OBSBasicTransform");
			if (!transformDialog) {
				// make sure there is an item selected on the main canvas before starting the transform dialog
				const auto currentScene =
					obs_frontend_preview_program_mode_active()
						? obs_frontend_get_current_preview_scene()
						: obs_frontend_get_current_scene();
				auto selected = GetSelectedItem(
					obs_scene_from_source(currentScene));
				if (!selected) {
					obs_scene_enum_items(
						obs_scene_from_source(
							currentScene),
						[](obs_scene_t *,
						   obs_sceneitem_t *item,
						   void *) {
							obs_sceneitem_select(
								item, true);
							return false;
						},
						nullptr);
				}
				obs_source_release(currentScene);
				QMetaObject::invokeMethod(
					mainDialog,
					"on_actionEditTransform_triggered");
				transformDialog =
					mainDialog->findChild<QDialog *>(
						"OBSBasicTransform");
			}
			if (!transformDialog)
				return;
			QMetaObject::invokeMethod(
				transformDialog, "SetItemQt",
				Q_ARG(OBSSceneItem, OBSSceneItem(sceneItem)));
		});
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.ResetTransform")),
		this, [sceneItem] {
			obs_sceneitem_set_alignment(
				sceneItem, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
			obs_sceneitem_set_bounds_type(sceneItem,
						      OBS_BOUNDS_NONE);
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
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.Rotate90CW")),
		this, [this] {
			float rotation = 90.0f;
			obs_scene_enum_items(scene, RotateSelectedSources,
					     &rotation);
		});
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.Rotate90CCW")),
		this, [this] {
			float rotation = -90.0f;
			obs_scene_enum_items(scene, RotateSelectedSources,
					     &rotation);
		});
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.Rotate180")),
		this, [this] {
			float rotation = 180.0f;
			obs_scene_enum_items(scene, RotateSelectedSources,
					     &rotation);
		});
	transformMenu->addSeparator();
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.FlipHorizontal")),
		this, [this] {
			vec2 scale;
			vec2_set(&scale, -1.0f, 1.0f);
			obs_scene_enum_items(scene, MultiplySelectedItemScale,
					     &scale);
		});
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.FlipVertical")),
		this, [this] {
			vec2 scale;
			vec2_set(&scale, 1.0f, -1.0f);
			obs_scene_enum_items(scene, MultiplySelectedItemScale,
					     &scale);
		});
	transformMenu->addSeparator();
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.FitToScreen")),
		this, [this] {
			obs_bounds_type boundsType = OBS_BOUNDS_SCALE_INNER;
			obs_scene_enum_items(scene, CenterAlignSelectedItems,
					     &boundsType);
		});
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.StretchToScreen")),
		this, [this] {
			obs_bounds_type boundsType = OBS_BOUNDS_STRETCH;
			obs_scene_enum_items(scene, CenterAlignSelectedItems,
					     &boundsType);
		});
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.CenterToScreen")),
		this, [this] { CenterSelectedItems(CenterType::Scene); });
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.VerticalCenter")),
		this, [this] { CenterSelectedItems(CenterType::Vertical); });
	transformMenu->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string(
			"Basic.MainMenu.Edit.Transform.HorizontalCenter")),
		this, [this] { CenterSelectedItems(CenterType::Horizontal); });

	auto projectorMenu = popup->addMenu(QString::fromUtf8(
		obs_frontend_get_locale_string("SourceProjector")));
	AddProjectorMenuMonitors(projectorMenu, this,
				 SLOT(OpenSourceProjector()));
	popup->addAction(QString::fromUtf8(obs_frontend_get_locale_string(
				 "SourceWindow")),
			 [sceneItem] {
				 obs_source_t *source = obs_source_get_ref(
					 obs_sceneitem_get_source(sceneItem));
				 if (!source)
					 return;
				 obs_frontend_open_projector(
					 "Source", -1, nullptr,
					 obs_source_get_name(source));
				 obs_source_release(source);
			 });

	popup->addAction(QString::fromUtf8(obs_frontend_get_locale_string(
				 "Screenshot.Source")),
			 this, [source] {
				 obs_frontend_take_source_screenshot(source);
			 });

	popup->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string("Filters")),
		this, [source] { obs_frontend_open_source_filters(source); });
	auto a = popup->addAction(
		QString::fromUtf8(obs_frontend_get_locale_string("Properties")),
		this,
		[source] { obs_frontend_open_source_properties(source); });
	a->setEnabled(obs_source_configurable(source));
}

bool CanvasDock::HandleMouseReleaseEvent(QMouseEvent *event)
{
	if (scrollMode)
		setCursor(Qt::OpenHandCursor);

	if (!mouseDown && event->button() == Qt::RightButton) {
		QMenu popup(this);
		QAction *action = popup.addAction(
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Basic.Main.Preview.Disable")),
			[this] {
				preview_disabled = !preview_disabled;
				obs_display_set_enabled(preview->GetDisplay(),
							!preview_disabled);
				preview->setVisible(!preview_disabled);
				previewDisabledWidget->setVisible(
					preview_disabled);
			});
		auto projectorMenu = popup.addMenu(QString::fromUtf8(
			obs_frontend_get_locale_string("PreviewProjector")));
		AddProjectorMenuMonitors(projectorMenu, this,
					 SLOT(OpenPreviewProjector()));

		action = popup.addAction(
			QString::fromUtf8(obs_frontend_get_locale_string(
				"PreviewWindow")),
			[this] { OpenProjector(-1); });

		action = popup.addAction(
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Basic.MainMenu.Edit.LockPreview")),
			this, [this] { locked = !locked; });
		action->setCheckable(true);
		action->setChecked(locked);

		popup.addAction(
			GetIconFromType(OBS_ICON_TYPE_IMAGE),
			QString::fromUtf8(
				obs_frontend_get_locale_string("Screenshot")),
			this, [this] {
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
		Qt::KeyboardModifiers modifiers =
			QGuiApplication::keyboardModifiers();

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
		scrollingOffset.x += pixelRatio * (qtPos.x() - scrollingFrom.x);
		scrollingOffset.y += pixelRatio * (qtPos.y() - scrollingFrom.y);
		scrollingFrom.x = qtPos.x();
		scrollingFrom.y = qtPos.y();
		//emit DisplayResized();
		return true;
	}

	if (locked)
		return true;

	bool updateCursor = false;

	if (mouseDown) {
		vec2 pos = GetMouseEventPos(event);

		if (!mouseMoved && !mouseOverItems &&
		    stretchHandle == ItemHandle::None) {
			ProcessClick(startPos);
			mouseOverItems = SelectedAtPos(scene, startPos);
		}

		pos.x = std::round(pos.x);
		pos.y = std::round(pos.y);

		if (stretchHandle != ItemHandle::None) {
			if (obs_sceneitem_locked(stretchItem))
				return true;

			selectionBox = false;

			obs_sceneitem_t *group =
				obs_sceneitem_get_group(scene, stretchItem);
			if (group) {
				vec3 group_pos;
				vec3_set(&group_pos, pos.x, pos.y, 0.0f);
				vec3_transform(&group_pos, &group_pos,
					       &invGroupTransform);
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

static bool CheckItemSelected(obs_scene_t *scene, obs_sceneitem_t *item,
			      void *param)
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
		obs_sceneitem_get_draw_transform(data->group,
						 &parent_transform);
		matrix4_mul(&transform, &transform, &parent_transform);
	}

	matrix4_inv(&transform, &transform);
	vec3_transform(&transformedPos, &pos3, &transform);

	if (transformedPos.x >= 0.0f && transformedPos.x <= 1.0f &&
	    transformedPos.y >= 0.0f && transformedPos.y <= 1.0f) {
		if (obs_sceneitem_selected(item)) {
			data->item = item;
			return false;
		}
	}

	UNUSED_PARAMETER(scene);
	return true;
}

bool CanvasDock::SelectedAtPos(obs_scene_t *scene, const vec2 &pos)
{
	if (!scene)
		return false;

	SceneFindData data(pos, false);
	obs_scene_enum_items(scene, CheckItemSelected, &data);
	return !!data.item;
}

static bool select_one(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	obs_sceneitem_t *selectedItem =
		reinterpret_cast<obs_sceneitem_t *>(param);
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

static bool FindItemAtPos(obs_scene_t *scene, obs_sceneitem_t *item,
			  void *param)
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

	if (CloseFloat(pos3.x, pos3_.x) && CloseFloat(pos3.y, pos3_.y) &&
	    transformedPos.x >= 0.0f && transformedPos.x <= 1.0f &&
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

	SceneFindData data(pos, selectBelow);
	obs_scene_enum_items(scene, FindItemAtPos, &data);
	return data.item;
}

vec2 CanvasDock::GetMouseEventPos(QMouseEvent *event)
{

	auto source = obs_weak_source_get_source(this->source);
	uint32_t sourceCX = obs_source_get_width(source);
	if (sourceCX <= 0)
		sourceCX = 1;
	uint32_t sourceCY = obs_source_get_height(source);
	if (sourceCY <= 0)
		sourceCY = 1;
	obs_source_release(source);

	int x, y;
	float scale;

	auto size = preview->size();

	GetScaleAndCenterPos(sourceCX, sourceCY, size.width(), size.height(), x,
			     y, scale);
	//auto newCX = scale * float(sourceCX);
	//auto newCY = scale * float(sourceCY);
	float pixelRatio = GetDevicePixelRatio();

	QPoint qtPos = event->pos();

	vec2 pos;
	vec2_set(&pos, (qtPos.x() - x / pixelRatio) / scale,
		 (qtPos.y() - y / pixelRatio) / scale);

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

	if ((flags & ITEM_LEFT && flags & ITEM_TOP) ||
	    (flags & ITEM_RIGHT && flags & ITEM_BOTTOM))
		setCursor(Qt::SizeFDiagCursor);
	else if ((flags & ITEM_LEFT && flags & ITEM_BOTTOM) ||
		 (flags & ITEM_RIGHT && flags & ITEM_TOP))
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

	float angle =
		atan2(pos2.y - rotatePoint.y, pos2.x - rotatePoint.x) + RAD(90);

#define ROT_SNAP(rot, thresh)                      \
	if (abs(angle - RAD(rot)) < RAD(thresh)) { \
		angle = RAD(rot);                  \
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

	vec2_set(&max_tl, float(-crop.left) * scale.x,
		 float(-crop.top) * scale.y);
	vec2_set(&max_br, stretchItemSize.x + crop.right * scale.x,
		 stretchItemSize.y + crop.bottom * scale.y);

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
		float maxX = stretchItemSize.x - (2.0 * scale.x);
		pos3.x = tl.x = min_x(pos3.x, maxX);

	} else if (stretchFlags & ITEM_RIGHT) {
		float minX = (2.0 * scale.x);
		pos3.x = br.x = max_x(pos3.x, minX);
	}

	if (stretchFlags & ITEM_TOP) {
		float maxY = stretchItemSize.y - (2.0 * scale.y);
		pos3.y = tl.y = min_y(pos3.y, maxY);

	} else if (stretchFlags & ITEM_BOTTOM) {
		float minY = (2.0 * scale.y);
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
		crop.right +=
			int(std::round((stretchItemSize.x - br.x) / scale.x));

	if (stretchFlags & ITEM_TOP)
		crop.top += int(std::round(tl.y / scale.y));
	else if (stretchFlags & ITEM_BOTTOM)
		crop.bottom +=
			int(std::round((stretchItemSize.y - br.y) / scale.y));

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

	obs_source_t *source = obs_sceneitem_get_source(stretchItem);

	vec2 baseSize;
	vec2_set(&baseSize, float(obs_source_get_width(source)),
		 float(obs_source_get_height(source)));

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

		if (!shiftDown)
			ClampAspect(tl, br, size, baseSize);

		vec2_div(&size, &size, &baseSize);
		obs_sceneitem_set_scale(stretchItem, &size);
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
	screenSize.x = obs_source_get_base_width(s);
	screenSize.y = obs_source_get_base_height(s);
	obs_source_release(s);
	vec3 clampOffset;

	vec3_zero(&clampOffset);

	const bool snap = config_get_bool(obs_frontend_get_global_config(),
					  "BasicWindow", "SnappingEnabled");
	if (snap == false)
		return clampOffset;

	const bool screenSnap =
		config_get_bool(obs_frontend_get_global_config(), "BasicWindow",
				"ScreenSnapping");
	const bool centerSnap =
		config_get_bool(obs_frontend_get_global_config(), "BasicWindow",
				"CenterSnapping");

	const float clampDist =
		config_get_double(obs_frontend_get_global_config(),
				  "BasicWindow", "SnapDistance") /
		previewScale;
	const float centerX = br.x - (br.x - tl.x) / 2.0f;
	const float centerY = br.y - (br.y - tl.y) / 2.0f;

	// Left screen edge.
	if (screenSnap && fabsf(tl.x) < clampDist)
		clampOffset.x = -tl.x;
	// Right screen edge.
	if (screenSnap && fabsf(clampOffset.x) < EPSILON &&
	    fabsf(screenSize.x - br.x) < clampDist)
		clampOffset.x = screenSize.x - br.x;
	// Horizontal center.
	if (centerSnap && fabsf(screenSize.x - (br.x - tl.x)) > clampDist &&
	    fabsf(screenSize.x / 2.0f - centerX) < clampDist)
		clampOffset.x = screenSize.x / 2.0f - centerX;

	// Top screen edge.
	if (screenSnap && fabsf(tl.y) < clampDist)
		clampOffset.y = -tl.y;
	// Bottom screen edge.
	if (screenSnap && fabsf(clampOffset.y) < EPSILON &&
	    fabsf(screenSize.y - br.y) < clampDist)
		clampOffset.y = screenSize.y - br.y;
	// Vertical center.
	if (centerSnap && fabsf(screenSize.y - (br.y - tl.y)) > clampDist &&
	    fabsf(screenSize.y / 2.0f - centerY) < clampDist)
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

static bool AddItemBounds(obs_scene_t *scene, obs_sceneitem_t *item,
			  void *param)
{
	SelectedItemBounds *data =
		reinterpret_cast<SelectedItemBounds *>(param);
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

static bool GetSourceSnapOffset(obs_scene_t *scene, obs_sceneitem_t *item,
				void *param)
{
	OffsetData *data = reinterpret_cast<OffsetData *>(param);

	if (obs_sceneitem_selected(item))
		return true;

	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	vec3 t[4] = {GetTransformedPos(0.0f, 0.0f, boxTransform),
		     GetTransformedPos(1.0f, 0.0f, boxTransform),
		     GetTransformedPos(0.0f, 1.0f, boxTransform),
		     GetTransformedPos(1.0f, 1.0f, boxTransform)};

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
#define EDGE_SNAP(l, r, x, y)                                               \
	do {                                                                \
		double dist = fabsf(l.x - data->r.x);                       \
		if (dist < data->clampDist &&                               \
		    fabsf(data->offset.x) < EPSILON && data->tl.y < br.y && \
		    data->br.y > tl.y &&                                    \
		    (fabsf(data->offset.x) > dist ||                        \
		     data->offset.x < EPSILON))                             \
			data->offset.x = l.x - data->r.x;                   \
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
	SelectedItemBounds data;
	obs_scene_enum_items(scene, AddItemBounds, &data);

	data.tl.x += offset.x;
	data.tl.y += offset.y;
	data.br.x += offset.x;
	data.br.y += offset.y;

	vec3 snapOffset = GetSnapOffset(data.tl, data.br);

	const bool snap = config_get_bool(obs_frontend_get_global_config(),
					  "BasicWindow", "SnappingEnabled");
	const bool sourcesSnap =
		config_get_bool(obs_frontend_get_global_config(), "BasicWindow",
				"SourceSnapping");
	if (snap == false)
		return;
	if (sourcesSnap == false) {
		offset.x += snapOffset.x;
		offset.y += snapOffset.y;
		return;
	}

	const float clampDist =
		config_get_double(obs_frontend_get_global_config(),
				  "BasicWindow", "SnapDistance") /
		previewScale;

	OffsetData offsetData;
	offsetData.clampDist = clampDist;
	offsetData.tl = data.tl;
	offsetData.br = data.br;
	vec3_copy(&offsetData.offset, &snapOffset);

	obs_scene_enum_items(scene, GetSourceSnapOffset, &offsetData);

	if (fabsf(offsetData.offset.x) > EPSILON ||
	    fabsf(offsetData.offset.y) > EPSILON) {
		offset.x += offsetData.offset.x;
		offset.y += offsetData.offset.y;
	} else {
		offset.x += snapOffset.x;
		offset.y += snapOffset.y;
	}
}

static bool CounterClockwise(float x1, float x2, float x3, float y1, float y2,
			     float y3)
{
	return (y3 - y1) * (x2 - x1) > (y2 - y1) * (x3 - x1);
}

static bool IntersectLine(float x1, float x2, float x3, float x4, float y1,
			  float y2, float y3, float y4)
{
	bool a = CounterClockwise(x1, x2, x3, y1, y2, y3);
	bool b = CounterClockwise(x1, x2, x4, y1, y2, y4);
	bool c = CounterClockwise(x3, x4, x1, y3, y4, y1);
	bool d = CounterClockwise(x3, x4, x2, y3, y4, y2);

	return (a != b) && (c != d);
}

static bool IntersectBox(matrix4 transform, float x1, float x2, float y1,
			 float y2)
{
	float x3, x4, y3, y4;

	x3 = transform.t.x;
	y3 = transform.t.y;
	x4 = x3 + transform.x.x;
	y4 = y3 + transform.x.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) ||
	    IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) ||
	    IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x4 = x3 + transform.y.x;
	y4 = y3 + transform.y.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) ||
	    IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) ||
	    IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x3 = transform.t.x + transform.x.x;
	y3 = transform.t.y + transform.x.y;
	x4 = x3 + transform.y.x;
	y4 = y3 + transform.y.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) ||
	    IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) ||
	    IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x3 = transform.t.x + transform.y.x;
	y3 = transform.t.y + transform.y.y;
	x4 = x3 + transform.x.x;
	y4 = y3 + transform.x.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) ||
	    IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) ||
	    IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	return false;
}

static bool FindItemsInBox(obs_scene_t *scene, obs_sceneitem_t *item,
			   void *param)
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

	if (CloseFloat(pos3.x, pos3_.x) && CloseFloat(pos3.y, pos3_.y) &&
	    transformedPos.x >= 0.0f && transformedPos.x <= 1.0f &&
	    transformedPos.y >= 0.0f && transformedPos.y <= 1.0f) {

		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x > x1 && transform.t.x < x2 && transform.t.y > y1 &&
	    transform.t.y < y2) {

		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x + transform.x.x > x1 &&
	    transform.t.x + transform.x.x < x2 &&
	    transform.t.y + transform.x.y > y1 &&
	    transform.t.y + transform.x.y < y2) {

		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x + transform.y.x > x1 &&
	    transform.t.x + transform.y.x < x2 &&
	    transform.t.y + transform.y.y > y1 &&
	    transform.t.y + transform.y.y < y2) {

		data->sceneItems.push_back(item);
		return true;
	}

	if (transform.t.x + transform.x.x + transform.y.x > x1 &&
	    transform.t.x + transform.x.x + transform.y.x < x2 &&
	    transform.t.y + transform.x.y + transform.y.y > y1 &&
	    transform.t.y + transform.x.y + transform.y.y < y2) {

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

void CanvasDock::BoxItems(const vec2 &startPos, const vec2 &pos)
{
	if (!scene)
		return;

	if (cursor().shape() != Qt::CrossCursor)
		setCursor(Qt::CrossCursor);

	SceneFindBoxData data(startPos, pos);
	obs_scene_enum_items(scene, FindItemsInBox, &data);

	std::lock_guard<std::mutex> lock(selectMutex);
	hoveredPreviewItems = data.sceneItems;
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

	inline HandleFindData(const vec2 &pos_, float scale)
		: pos(pos_),
		  radius(HANDLE_SEL_RADIUS / scale)
	{
		matrix4_identity(&parent_xform);
	}

	inline HandleFindData(const HandleFindData &hfd,
			      obs_sceneitem_t *parent)
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

static bool FindHandleAtPos(obs_scene_t *scene, obs_sceneitem_t *item,
			    void *param)
{
	HandleFindData &data = *reinterpret_cast<HandleFindData *>(param);

	if (!obs_sceneitem_selected(item)) {
		if (obs_sceneitem_is_group(item)) {
			HandleFindData newData(data, item);
			newData.angleOffset = obs_sceneitem_get_rot(item);

			obs_sceneitem_group_enum_items(item, FindHandleAtPos,
						       &newData);

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
	vec2_set(&rotHandleOffset, 0.0f,
		 HANDLE_RADIUS * data.radius * 1.5 - data.radius);
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

			vec2_set(&data.rotatePoint,
				 transform.t.x + transform.x.x / 2 +
					 transform.y.x / 2,
				 transform.t.y + transform.x.y / 2 +
					 transform.y.y / 2);

			obs_sceneitem_get_pos(item, &data.offsetPoint);
			data.offsetPoint.x -= data.rotatePoint.x;
			data.offsetPoint.y -= data.rotatePoint.y;

			RotatePos(&data.offsetPoint,
				  -RAD(obs_sceneitem_get_rot(item)));
		}
	}

	UNUSED_PARAMETER(scene);
	return true;
}

void CanvasDock::GetStretchHandleData(const vec2 &pos, bool ignoreGroup)
{
	if (!scene)
		return;

	HandleFindData data(pos, previewScale);
	obs_scene_enum_items(scene, FindHandleAtPos, &data);

	stretchItem = std::move(data.item);
	stretchHandle = data.handle;

	rotateAngle = data.angle;
	rotatePoint = data.rotatePoint;
	offsetPoint = data.offsetPoint;

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
		matrix4_rotate_aa4f(&itemToScreen, &itemToScreen, 0.0f, 0.0f,
				    1.0f, RAD(itemRot));
		matrix4_translate3f(&itemToScreen, &itemToScreen, itemUL.x,
				    itemUL.y, 0.0f);

		matrix4_identity(&screenToItem);
		matrix4_translate3f(&screenToItem, &screenToItem, -itemUL.x,
				    -itemUL.y, 0.0f);
		matrix4_rotate_aa4f(&screenToItem, &screenToItem, 0.0f, 0.0f,
				    1.0f, RAD(-itemRot));

		obs_sceneitem_get_crop(stretchItem, &startCrop);
		obs_sceneitem_get_pos(stretchItem, &startItemPos);

		obs_source_t *source = obs_sceneitem_get_source(stretchItem);
		cropSize.x = float(obs_source_get_width(source) -
				   startCrop.left - startCrop.right);
		cropSize.y = float(obs_source_get_height(source) -
				   startCrop.top - startCrop.bottom);

		stretchGroup = obs_sceneitem_get_group(scene, stretchItem);
		if (stretchGroup && !ignoreGroup) {
			obs_sceneitem_get_draw_transform(stretchGroup,
							 &invGroupTransform);
			matrix4_inv(&invGroupTransform, &invGroupTransform);
			obs_sceneitem_defer_group_resize_begin(stretchGroup);
		}
	}
}

void CanvasDock::ClampAspect(vec3 &tl, vec3 &br, vec2 &size,
			     const vec2 &baseSize)
{
	float baseAspect = baseSize.x / baseSize.y;
	float aspect = size.x / size.y;
	uint32_t stretchFlags = (uint32_t)stretchHandle;

	if (stretchHandle == ItemHandle::TopLeft ||
	    stretchHandle == ItemHandle::TopRight ||
	    stretchHandle == ItemHandle::BottomLeft ||
	    stretchHandle == ItemHandle::BottomRight) {
		if (aspect < baseAspect) {
			if ((size.y >= 0.0f && size.x >= 0.0f) ||
			    (size.y <= 0.0f && size.x <= 0.0f))
				size.x = size.y * baseAspect;
			else
				size.x = size.y * baseAspect * -1.0f;
		} else {
			if ((size.y >= 0.0f && size.x >= 0.0f) ||
			    (size.y <= 0.0f && size.x <= 0.0f))
				size.y = size.x / baseAspect;
			else
				size.y = size.x / baseAspect * -1.0f;
		}

	} else if (stretchHandle == ItemHandle::TopCenter ||
		   stretchHandle == ItemHandle::BottomCenter) {
		if ((size.y >= 0.0f && size.x >= 0.0f) ||
		    (size.y <= 0.0f && size.x <= 0.0f))
			size.x = size.y * baseAspect;
		else
			size.x = size.y * baseAspect * -1.0f;

	} else if (stretchHandle == ItemHandle::CenterLeft ||
		   stretchHandle == ItemHandle::CenterRight) {
		if ((size.y >= 0.0f && size.x >= 0.0f) ||
		    (size.y <= 0.0f && size.x <= 0.0f))
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

bool CanvasDock::DrawSelectionBox(float x1, float y1, float x2, float y2,
				  gs_vertbuffer_t *rectFill)
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
	gs_load_vertexbuffer(rectFill);
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

static void check_descendant(obs_source_t *parent, obs_source_t *child,
			     void *param)
{
	auto *info = (struct descendant_info *)param;
	if (parent == info->target2 || child == info->target2 ||
	    obs_weak_source_references_source(info->target, child) ||
	    obs_weak_source_references_source(info->target, parent))
		info->exists = true;
}

bool CanvasDock::add_sources_of_type_to_menu(void *param, obs_source_t *source)
{
	QMenu *menu = static_cast<QMenu *>(param);
	CanvasDock *cd = static_cast<CanvasDock *>(menu->parent());
	auto a = menu->menuAction();
	auto t = a->data().toString();
	auto idUtf8 = t.toUtf8();
	const char *id = idUtf8.constData();
	if (strcmp(obs_source_get_unversioned_id(source), id) == 0) {
		auto name = QString::fromUtf8(obs_source_get_name(source));
		QList<QAction *> actions = menu->actions();
		QAction *before = nullptr;
		for (QAction *menuAction : actions) {
			if (menuAction->text().compare(name) >= 0)
				before = menuAction;
		}
		auto na = new QAction(name, menu);
		connect(na, &QAction::triggered,
			[cd, source] { cd->AddSourceToScene(source); });
		menu->insertAction(before, na);
		struct descendant_info info = {false, cd->source,
					       obs_scene_get_source(cd->scene)};
		obs_source_enum_full_tree(source, check_descendant, &info);
		na->setEnabled(!info.exists);
	}
	return true;
}

void CanvasDock::LoadSourceTypeMenu(QMenu *menu, const char *type)
{
	menu->clear();
	if (strcmp(type, "scene") == 0) {
		obs_enum_scenes(add_sources_of_type_to_menu, menu);
	} else {
		obs_enum_sources(add_sources_of_type_to_menu, menu);

		auto popupItem = new QAction(
			QString::fromUtf8(
				obs_frontend_get_locale_string("New")),
			menu);
		popupItem->setData(QString::fromUtf8(type));
		connect(popupItem, SIGNAL(triggered(bool)), this,
			SLOT(AddSourceFromAction()));

		QList<QAction *> actions = menu->actions();
		QAction *first = actions.size() ? actions.first() : nullptr;
		menu->insertAction(first, popupItem);
		menu->insertSeparator(first);
	}
}

void CanvasDock::AddSourceToScene(obs_source_t *source)
{
	obs_scene_add(scene, source);
}

QMenu *CanvasDock::CreateAddSourcePopupMenu()
{
	const char *unversioned_type;
	const char *type;
	bool foundValues = false;
	bool foundDeprecated = false;
	size_t idx = 0;

	QMenu *popup = new QMenu(
		QString::fromUtf8(obs_frontend_get_locale_string("Add")), this);
	QMenu *deprecated = new QMenu(
		QString::fromUtf8(obs_frontend_get_locale_string("Deprecated")),
		popup);

	auto getActionAfter = [](QMenu *menu, const QString &name) {
		QList<QAction *> actions = menu->actions();

		for (QAction *menuAction : actions) {
			if (menuAction->text().compare(name) >= 0)
				return menuAction;
		}

		return (QAction *)nullptr;
	};

	auto addSource = [this, getActionAfter](QMenu *popup, const char *type,
						const char *name) {
		QString qname = QString::fromUtf8(name);
		QAction *popupItem = new QAction(qname, this);
		if (strcmp(type, "scene") == 0) {
			popupItem->setIcon(GetSceneIcon());
		} else if (strcmp(type, "group") == 0) {
			popupItem->setIcon(GetGroupIcon());
		} else {
			popupItem->setIcon(GetIconFromType(
				obs_source_get_icon_type(type)));
		}
		popupItem->setData(QString::fromUtf8(type));
		QMenu *menu = new QMenu(this);
		popupItem->setMenu(menu);
		QObject::connect(menu, &QMenu::aboutToShow, [this, menu, type] {
			LoadSourceTypeMenu(menu, type);
		});

		QAction *after = getActionAfter(popup, qname);
		popup->insertAction(after, popupItem);
	};

	while (obs_enum_input_types2(idx++, &type, &unversioned_type)) {
		const char *name = obs_source_get_display_name(type);
		uint32_t caps = obs_get_source_output_flags(type);

		if ((caps & OBS_SOURCE_CAP_DISABLED) != 0)
			continue;

		if ((caps & OBS_SOURCE_DEPRECATED) == 0) {
			addSource(popup, unversioned_type, name);
		} else {
			addSource(deprecated, unversioned_type, name);
			foundDeprecated = true;
		}
		foundValues = true;
	}

	addSource(popup, "scene",
		  obs_frontend_get_locale_string("Basic.Scene"));
	addSource(popup, "group", obs_frontend_get_locale_string("Group"));

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
	QAction *action = qobject_cast<QAction *>(sender());
	if (!action)
		return;

	auto t = action->data().toString();
	auto idUtf8 = t.toUtf8();
	const char *id = idUtf8.constData();
	if (id && *id && strlen(id)) {
		const char *v_id = obs_get_latest_input_type_id(id);
		QString placeHolderText =
			QString::fromUtf8(obs_source_get_display_name(v_id));
		QString text = placeHolderText;
		int i = 2;
		OBSSourceAutoRelease s = nullptr;
		while ((s = obs_get_source_by_name(text.toUtf8().constData()))) {
			text = QString("%1 %2").arg(placeHolderText).arg(i++);
		}
		obs_source_t *source = obs_source_create(
			id, text.toUtf8().constData(), nullptr, nullptr);
		obs_scene_add(scene, source);
		if (obs_source_configurable(source)) {
			obs_frontend_open_source_properties(source);
		}
		obs_source_release(source);
	}
}

bool CanvasDock::StartVideo()
{
	if (!view)
		view = obs_view_create();

	auto s = obs_weak_source_get_source(source);
	obs_view_set_source(view, 0, s);
	obs_source_release(s);
	bool started_video = false;
	if (!video) {
		obs_video_info ovi;
		obs_get_video_info(&ovi);
		ovi.base_width = canvas_width;
		ovi.base_height = canvas_height;
		ovi.output_width = canvas_width;
		ovi.output_height = canvas_height;
		video = obs_view_add2(view, &ovi);
		started_video = true;
	}
	return started_video;
}

void CanvasDock::virtual_cam_output_start(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("virtual_camera_started");
	d->StartReplayBuffer();
	QMetaObject::invokeMethod(d, "OnVirtualCamStart");
}

void CanvasDock::virtual_cam_output_stop(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("virtual_camera_stopped");
	QMetaObject::invokeMethod(d, "OnVirtualCamStop");
	signal_handler_t *signal =
		obs_output_get_signal_handler(d->virtualCamOutput);
	signal_handler_disconnect(signal, "start", virtual_cam_output_start, d);
	signal_handler_disconnect(signal, "stop", virtual_cam_output_stop, d);
	obs_output_release(d->virtualCamOutput);
	d->virtualCamOutput = nullptr;
}

void CanvasDock::OnVirtualCamStart()
{
	virtualCamButton->setIcon(virtualCamActiveIcon);
	virtualCamButton->setStyleSheet(
		QString::fromUtf8("background: rgb(192,128,0);"));
	virtualCamButton->setChecked(true);
}

void CanvasDock::OnVirtualCamStop()
{
	virtualCamButton->setIcon(virtualCamInactiveIcon);
	virtualCamButton->setStyleSheet(QString::fromUtf8(""));
	virtualCamButton->setChecked(false);
	CheckReplayBuffer();
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
	StartReplayBuffer();
	const auto output = obs_frontend_get_virtualcam_output();
	if (obs_output_active(output)) {
		if (!virtualCamOutput)
			virtualCamButton->setChecked(false);
		obs_output_release(output);
		return;
	}

	if (!virtual_cam_warned && isVisible()) {
		QMessageBox::warning(this,
				     QString::fromUtf8(obs_module_text(
					     "VirtualCameraVertical")),
				     QString::fromUtf8(obs_module_text(
					     "VirtualCameraWarning")));
		virtual_cam_warned = true;
	}

	virtualCamOutput = output;

	const bool started_video = StartVideo();
	signal_handler_t *signal = obs_output_get_signal_handler(output);
	signal_handler_disconnect(signal, "start", virtual_cam_output_start,
				  this);
	signal_handler_disconnect(signal, "stop", virtual_cam_output_stop,
				  this);
	signal_handler_connect(signal, "start", virtual_cam_output_start, this);
	signal_handler_connect(signal, "stop", virtual_cam_output_stop, this);

	obs_output_set_media(output, video, obs_get_audio());
	SendVendorEvent("virtual_camera_starting");
	const bool success = obs_output_start(output);
	if (!success) {
		QMetaObject::invokeMethod(this, "OnVirtualCamStop");
		if (started_video) {
			obs_view_remove(view);
			obs_view_set_source(view, 0, nullptr);
			video = nullptr;
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
	obs_output_set_media(virtualCamOutput, nullptr, nullptr);
	obs_output_stop(virtualCamOutput);
}

void CanvasDock::ConfigButtonClicked()
{
	if (!configDialog) {
		configDialog = new OBSBasicSettings(
			this, (QMainWindow *)obs_frontend_get_main_window());
	}
	configDialog->LoadSettings();
	configDialog->exec();
	save_canvas();
}

void CanvasDock::ReplayButtonClicked(QString filename)
{
	if (!obs_output_active(replayOutput))
		return;
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
	const char *profile = config_get_string(
		obs_frontend_get_global_config(), "Basic", "ProfileDir");
	int ret;

	if (!profile)
		return -1;
	if (!path)
		return -1;
	if (!file)
		file = "";

	ret = GetConfigPath(profiles_path, 512, "obs-studio/basic/profiles");
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

	if (record_advanced_settings) {
		if (!recordOutput)
			recordOutput = obs_output_create(
				"ffmpeg_muxer", "vertical_canvas_record",
				nullptr, nullptr);
	} else {
		obs_output_t *output = obs_frontend_get_recording_output();
		if (!output) {
			obs_output_t *replay_output =
				obs_frontend_get_replay_buffer_output();
			if (!replay_output) {
				ShowNoReplayOutputError();
				return;
			}
			obs_output_release(replay_output);
		}
		if (!recordOutput)
			recordOutput = obs_output_create(
				obs_output_get_id(output),
				"vertical_canvas_record", nullptr, nullptr);

		obs_data_t *settings = obs_output_get_settings(output);
		obs_output_update(recordOutput, settings);
		obs_data_release(settings);
		obs_output_release(output);
	}

	SetRecordAudioEncoders(recordOutput);

	config_t *config = obs_frontend_get_profile_config();
	const char *mode = config_get_string(config, "Output", "Mode");
	const char *dir = nullptr;
	const char *format = nullptr;
	bool ffmpegOutput = false;

	if (record_advanced_settings) {
		if (file_format.empty())
			file_format = "mkv";
		format = file_format.c_str();
		dir = recordPath.c_str();
	} else if (strcmp(mode, "Advanced") == 0) {
		const char *recType =
			config_get_string(config, "AdvOut", "RecType");

		if (strcmp(recType, "FFmpeg") == 0) {
			ffmpegOutput = true;
			dir = config_get_string(config, "AdvOut", "FFFilePath");
		} else {
			dir = config_get_string(config, "AdvOut",
						"RecFilePath");
		}
		bool ffmpegRecording =
			ffmpegOutput &&
			config_get_bool(config, "AdvOut", "FFOutputToFile");
		if (ffmpegRecording) {
			format = config_get_string(config, "AdvOut",
						   "FFExtension");
		} else if (!config_has_user_value(config, "AdvOut",
						  "RecFormat2") &&
			   config_has_user_value(config, "AdvOut",
						 "RecFormat")) {
			format = config_get_string(config, "AdvOut",
						   "RecFormat");
		} else {
			format = config_get_string(config, "AdvOut",
						   "RecFormat2");
		}
	} else {
		dir = config_get_string(config, "SimpleOutput", "FilePath");
		if (!config_has_user_value(config, "SimpleOutput",
					   "RecFormat2") &&
		    config_has_user_value(config, "SimpleOutput",
					  "RecFormat")) {
			format = config_get_string(config, "SimpleOutput",
						   "RecFormat");
		} else {
			format = config_get_string(config, "SimpleOutput",
						   "RecFormat2");
		}
		const char *quality =
			config_get_string(config, "SimpleOutput", "RecQuality");
		if (strcmp(quality, "Lossless") == 0) {
			ffmpegOutput = true;
		}
	}
	if (recordPath.empty() && (!dir || !strlen(dir))) {
		if (isVisible()) {
			QMessageBox::warning(
				this,
				QString::fromUtf8(
					obs_frontend_get_locale_string(
						"Output.BadPath.Title")),
				QString::fromUtf8(
					obs_module_text("RecordPathError")));
		}
		return;
	}

	const bool started_video = StartVideo();

	obs_output_set_video_encoder(recordOutput, GetRecordVideoEncoder());

	SetRecordAudioEncoders(recordOutput);

	signal_handler_t *signal = obs_output_get_signal_handler(recordOutput);
	signal_handler_disconnect(signal, "start", record_output_start, this);
	signal_handler_disconnect(signal, "stop", record_output_stop, this);
	signal_handler_disconnect(signal, "stopping", record_output_stopping,
				  this);
	signal_handler_connect(signal, "start", record_output_start, this);
	signal_handler_connect(signal, "stop", record_output_stop, this);
	signal_handler_connect(signal, "stopping", record_output_stopping,
			       this);

	std::string filenameFormat;
	if (record_advanced_settings) {
		if (filename_formatting.empty()) {
			filename_formatting = config_get_string(
				config, "Output", "FilenameFormatting");
			filename_formatting += "-vertical";
		}
		if (filename_formatting.empty()) {
			filename_formatting =
				"%CCYY-%MM-%DD %hh-%mm-%ss-vertical";
		}
		filenameFormat = filename_formatting;
	} else {
		filenameFormat = config_get_string(config, "Output",
						   "FilenameFormatting");
		filenameFormat += "-vertical";
	}
	if (!format || !strlen(format))
		format = "mkv";
	std::string ext = format;
	if (ffmpegOutput)
		ext = "avi";
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
	char *filename = os_generate_formatted_filename(ext.c_str(), true,
							filenameFormat.c_str());
	if (recordPath.empty() && dir) {
		recordPath = dir;
	} else {
		dir = recordPath.c_str();
	}
	snprintf(path, 512, "%s/%s", dir, filename);
	bfree(filename);
	ensure_directory(path);
	obs_data_set_string(ps, ffmpegOutput ? "url" : "path", path);
	obs_output_update(recordOutput, ps);
	obs_data_release(ps);

	obs_output_set_media(recordOutput, video, obs_get_audio());

	SendVendorEvent("recording_starting");
	const bool success = obs_output_start(recordOutput);
	if (!success) {
		QMetaObject::invokeMethod(
			this, "OnRecordStop", Q_ARG(int, OBS_OUTPUT_ERROR),
			Q_ARG(QString,
			      QString::fromUtf8(obs_output_get_last_error(
				      recordOutput))));
		if (started_video) {
			obs_view_remove(view);
			obs_view_set_source(view, 0, nullptr);
			video = nullptr;
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
	d->StartReplayBuffer();
	QMetaObject::invokeMethod(d, "OnRecordStart");
}

void CanvasDock::record_output_stop(void *data, calldata_t *calldata)
{
	const char *last_error =
		(const char *)calldata_ptr(calldata, "last_error");
	QString arg_last_error = QString::fromUtf8(last_error);
	const int code = (int)calldata_int(calldata, "code");
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("recording_stopped");
	QMetaObject::invokeMethod(d, "OnRecordStop", Q_ARG(int, code),
				  Q_ARG(QString, arg_last_error));
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
		obs_output_set_mixers(output, record_audio_tracks);
		for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
			if ((record_audio_tracks & (1ll << i)) == 0)
				continue;
			obs_encoder_t *aet =
				obs_output_get_audio_encoder(replayOutput, idx);
			if (!aet && recordOutput)
				aet = obs_output_get_audio_encoder(recordOutput,
								   idx);
			if (aet &&
			    strcmp(obs_encoder_get_id(aet), "ffmpeg_aac") != 0)
				aet = nullptr;
			if (!aet) {
				std::string name = "vertical";
				name += std::to_string(idx);
				aet = obs_audio_encoder_create("ffmpeg_aac",
							       name.c_str(),
							       nullptr, i,
							       nullptr);
				obs_encoder_set_audio(aet, obs_get_audio());
			}
			obs_output_set_audio_encoder(output, aet, idx);
			idx++;
		}
	} else {
		bool r = false;
		obs_output_t *main_output =
			obs_frontend_get_replay_buffer_output();
		if (!main_output) {
			r = true;
			main_output = obs_frontend_get_recording_output();
		}
		size_t mixers = obs_output_get_mixers(main_output);
		if (!mixers) {
			obs_output_t *record_output =
				obs_frontend_get_recording_output();
			if (record_output) {
				mixers = obs_output_get_mixers(record_output);
				obs_output_release(record_output);
			}
			if (!mixers) {
				config_t *config =
					obs_frontend_get_profile_config();
				const char *mode = config_get_string(
					config, "Output", "Mode");
				if (astrcmpi(mode, "Advanced") == 0) {
					const char *recType = config_get_string(
						config, "AdvOut", "RecType");
					if (astrcmpi(recType, "FFmpeg") == 0) {
						mixers = config_get_int(
							config, "AdvOut",
							"FFAudioMixes");
					} else {
						mixers = config_get_int(
							config, "AdvOut",
							"RecTracks");
					}

				} else {
					const char *quality = config_get_string(
						config, "SimpleOutput",
						"RecQuality");
					if (strcmp(quality, "Stream") == 0) {
						mixers = 1;
					} else {
						mixers = config_get_int(
							config, "SimpleOutput",
							"RecTracks");
					}
				}
				if (!mixers)
					mixers = 1;
			}
		}
		obs_output_set_mixers(output, mixers);
		for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
			if ((mixers & (1ll << i)) == 0)
				continue;
			obs_encoder_t *aef =
				obs_output_get_audio_encoder(main_output, idx);
			if (!aef && idx == 0) {
				if (r) {
					obs_frontend_recording_start();
					obs_frontend_recording_stop();
				} else {
					obs_frontend_replay_buffer_start();
					obs_frontend_replay_buffer_stop();
				}
				aef = obs_output_get_audio_encoder(main_output,
								   idx);
			}
			if (aef) {
				obs_encoder_t *aet =
					obs_output_get_audio_encoder(
						replayOutput, idx);
				if (!aet && recordOutput)
					aet = obs_output_get_audio_encoder(
						recordOutput, idx);
				if (aet && strcmp(obs_encoder_get_id(aef),
						  obs_encoder_get_id(aet)) != 0)
					aet = nullptr;
				if (!aet) {
					std::string name =
						obs_encoder_get_name(aef);
					name += "_vertical";
					aet = obs_audio_encoder_create(
						obs_encoder_get_id(aef),
						name.c_str(), nullptr, i,
						nullptr);
					obs_encoder_set_audio(aet,
							      obs_get_audio());
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
		const char *recType =
			config_get_string(config, "AdvOut", "RecType");
		if (astrcmpi(recType, "FFmpeg") == 0) {
			blog(LOG_WARNING,
			     "[vertical-canvas] error starting backtrack: custom ffmpeg");
			if (isVisible()) {
				QMessageBox::warning(
					this,
					QString::fromUtf8(obs_module_text(
						"backtrackStartFail")),
					QString::fromUtf8(obs_module_text(
						"backtrackCustomFfmpeg")));
			}
			return;
		}
	}
	blog(LOG_WARNING,
	     "[vertical-canvas] error starting backtrack: no replay buffer found");
	if (isVisible()) {
		QMessageBox::warning(this,
				     QString::fromUtf8(obs_module_text(
					     "backtrackStartFail")),
				     QString::fromUtf8(obs_module_text(
					     "backtrackNoReplayBuffer")));
	}
}

void CanvasDock::StartReplayBuffer()
{
	if ((!startReplay && !replayAlwaysOn) ||
	    obs_output_active(replayOutput))
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
		obs_data_set_string(s, "extension", file_format.c_str());
		//allow_spaces
		obs_data_set_string(s, "directory", replayPath.c_str());
		obs_output_update(replayOutput, s);
		obs_data_release(s);
	} else {
		obs_output_t *replay_output =
			obs_frontend_get_replay_buffer_output();
		if (!replay_output) {
			ShowNoReplayOutputError();
			return;
		}
		obs_encoder_t *enc =
			obs_output_get_video_encoder(replay_output);
		if (!enc) {
			obs_frontend_replay_buffer_start();
			obs_frontend_replay_buffer_stop();
			enc = obs_output_get_video_encoder(replay_output);
		}
		if (!enc) {
			obs_output_release(replay_output);
			blog(LOG_WARNING,
			     "[vertical-canvas] error starting backtrack: no video encoder found");
			return;
		}

		auto settings = obs_output_get_settings(replay_output);
		obs_output_release(replay_output);
		if (!strlen(obs_data_get_string(settings, "directory"))) {
			obs_frontend_replay_buffer_start();
			obs_frontend_replay_buffer_stop();
		}
		if (!replayDuration) {
			replayDuration =
				obs_data_get_int(settings, "max_time_sec");
			if (!replayDuration)
				replayDuration = 5;
		}
		if (replayPath.empty())
			replayPath = obs_data_get_string(settings, "directory");
		obs_output_update(replayOutput, settings);
		if (obs_data_get_int(settings, "max_time_sec") !=
		    replayDuration) {
			const auto s = obs_output_get_settings(replayOutput);
			obs_data_set_int(s, "max_time_sec", replayDuration);
			obs_data_release(s);
		}
		if (obs_data_get_int(settings, "max_size_mb")) {
			const auto s = obs_output_get_settings(replayOutput);
			obs_data_set_int(s, "max_size_mb", 0);
			obs_data_release(s);
		}
		if (strcmp(replayPath.c_str(),
			   obs_data_get_string(settings, "directory")) != 0) {
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

	obs_output_set_video_encoder(replayOutput, GetRecordVideoEncoder());

	signal_handler_t *signal = obs_output_get_signal_handler(replayOutput);
	signal_handler_disconnect(signal, "start", replay_output_start, this);
	signal_handler_disconnect(signal, "stop", replay_output_stop, this);
	signal_handler_connect(signal, "start", replay_output_start, this);
	signal_handler_connect(signal, "stop", replay_output_stop, this);

	obs_output_set_media(replayOutput, video, obs_get_audio());
	SendVendorEvent("backtrack_starting");

	const bool success = obs_output_start(replayOutput);
	if (!success) {
		QMetaObject::invokeMethod(
			this, "OnReplayBufferStop",
			Q_ARG(int, OBS_OUTPUT_ERROR),
			Q_ARG(QString,
			      QString::fromUtf8(obs_output_get_last_error(
				      replayOutput))));
		if (started_video) {
			obs_view_remove(view);
			obs_view_set_source(view, 0, nullptr);
			video = nullptr;
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
		return EncoderAvailable("jim_nvenc") ? "jim_nvenc"
						     : "ffmpeg_nvenc";
	if (strcmp(encoder, SIMPLE_ENCODER_NVENC_HEVC) == 0)
		return EncoderAvailable("jim_hevc_nvenc") ? "jim_hevc_nvenc"
							  : "ffmpeg_hevc_nvenc";
	if (strcmp(encoder, SIMPLE_ENCODER_NVENC_AV1) == 0)
		return "jim_av1_nvenc";
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
			const char *recordEncoder = config_get_string(
				config, "AdvOut", "RecEncoder");
			useRecordEncoder = astrcmpi(recordEncoder, "none") == 0;
		} else {
			const char *quality = config_get_string(
				config, "SimpleOutput", "RecQuality");
			if (strcmp(quality, "Stream") == 0) {
				useRecordEncoder = true;
			}
		}
	} else if (strcmp(mode, "Advanced") == 0) {
		video_settings = GetDataFromJsonFile("streamEncoder.json");
		enc_id = config_get_string(config, "AdvOut", "Encoder");
		const char *recordEncoder =
			config_get_string(config, "AdvOut", "RecEncoder");
		useRecordEncoder = astrcmpi(recordEncoder, "none") == 0;
		if (!videoBitrate) {
			videoBitrate = (uint32_t)obs_data_get_int(
				video_settings, "bitrate");
		} else {
			obs_data_set_int(video_settings, "bitrate",
					 videoBitrate);
		}
	} else {
		video_settings = obs_data_create();
		bool advanced =
			config_get_bool(config, "SimpleOutput", "UseAdvanced");
		enc_id = get_simple_output_encoder(config_get_string(
			config, "SimpleOutput", "StreamEncoder"));
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
		obs_data_set_string(video_settings,
				    (strcmp(presetType, "NVENCPreset2") == 0)
					    ? "preset2"
					    : "preset",
				    preset);

		obs_data_set_string(video_settings, "rate_control", "CBR");
		if (!videoBitrate) {
			const int sVideoBitrate = config_get_uint(
				config, "SimpleOutput", "VBitrate");
			obs_data_set_int(video_settings, "bitrate",
					 sVideoBitrate);
			videoBitrate = sVideoBitrate;
		} else {
			obs_data_set_int(video_settings, "bitrate",
					 videoBitrate);
		}

		if (advanced) {
			const char *custom = config_get_string(
				config, "SimpleOutput", "x264Settings");
			obs_data_set_string(video_settings, "x264opts", custom);
		}

		const char *quality =
			config_get_string(config, "SimpleOutput", "RecQuality");
		if (strcmp(quality, "Stream") == 0) {
			useRecordEncoder = true;
		}
	}

	obs_encoder_t *se = obs_output_get_video_encoder(streamOutput);
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
		video_encoder = obs_video_encoder_create(
			enc_id, "vertical_canvas_video_encoder", nullptr,
			nullptr);
	}

	obs_encoder_update(video_encoder, video_settings);
	obs_data_release(video_settings);

	switch (video_output_get_format(video)) {
	case VIDEO_FORMAT_I420:
	case VIDEO_FORMAT_NV12:
	case VIDEO_FORMAT_I010:
	case VIDEO_FORMAT_P010:
		break;
	default:
		obs_encoder_set_preferred_video_format(video_encoder,
						       VIDEO_FORMAT_NV12);
	}
	if (!obs_encoder_active(video_encoder))
		obs_encoder_set_video(video_encoder, video);
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
			if (astrcmpi(config_get_string(config, "AdvOut",
						       "RecEncoder"),
				     "none") == 0)
				return GetStreamVideoEncoder();
			enc_id = config_get_string(config, "AdvOut",
						   "RecEncoder");
		} else {
			if (strcmp(config_get_string(config, "SimpleOutput",
						     "RecQuality"),
				   "Stream") == 0)
				return GetStreamVideoEncoder();
			enc_id = get_simple_output_encoder(config_get_string(
				config, "SimpleOutput", "RecEncoder"));
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
		video_encoder = obs_video_encoder_create(
			enc_id, "vertical_canvas_record_video_encoder", nullptr,
			nullptr);
	}

	obs_encoder_update(video_encoder, settings);
	obs_data_release(settings);

	if (!record_advanced_settings) {
		obs_output_t *main_output =
			obs_frontend_get_replay_buffer_output();
		if (!main_output)
			main_output = obs_frontend_get_recording_output();
		auto enc = obs_output_get_video_encoder(main_output);
		obs_output_release(main_output);
		obs_data_t *d = obs_encoder_get_settings(enc);
		obs_encoder_update(video_encoder, d);
		if (!videoBitrate) {
			videoBitrate = (uint32_t)obs_data_get_int(d, "bitrate");
		} else {
			auto s = obs_encoder_get_settings(video_encoder);
			if (videoBitrate != obs_data_get_int(s, "bitrate")) {
				obs_data_set_int(s, "bitrate", videoBitrate);
				obs_encoder_update(video_encoder, nullptr);
			}
			obs_data_release(s);
		}
		obs_data_release(d);
	}

	switch (video_output_get_format(video)) {
	case VIDEO_FORMAT_I420:
	case VIDEO_FORMAT_NV12:
	case VIDEO_FORMAT_I010:
	case VIDEO_FORMAT_P010:
		break;
	default:
		obs_encoder_set_preferred_video_format(video_encoder,
						       VIDEO_FORMAT_NV12);
	}
	if (!obs_encoder_active(video_encoder))
		obs_encoder_set_video(video_encoder, video);
	return video_encoder;
}

void CanvasDock::StopReplayBuffer()
{
	QMetaObject::invokeMethod(this, "OnReplayBufferStop",
				  Q_ARG(int, OBS_OUTPUT_SUCCESS),
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
	const char *last_error =
		(const char *)calldata_ptr(calldata, "last_error");
	QString arg_last_error = QString::fromUtf8(last_error);
	const int code = (int)calldata_int(calldata, "code");
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("backtrack_stopped");
	QMetaObject::invokeMethod(d, "OnReplayBufferStop", Q_ARG(int, code),
				  Q_ARG(QString, arg_last_error));
}

void CanvasDock::StreamButtonClicked()
{
	if (obs_output_active(streamOutput)) {
		StopStream();
	} else {
		StartStream();
	}
}

void CanvasDock::StartStream()
{
	if (obs_output_active(streamOutput))
		return;

	const bool started_video = StartVideo();

	auto s = obs_data_create();
	obs_data_set_string(s, "server", stream_server.c_str());
	obs_data_set_string(s, "key", stream_key.c_str());
	//use_auth
	//username
	//password
	obs_service_update(stream_service, s);
	obs_data_release(s);

#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(29, 1, 0)
	const char *type = obs_service_get_output_type(stream_service);
#else
	const char *type =
		obs_service_get_preferred_output_type(stream_service);
#endif
	if (!type) {
		type = "rtmp_output";
#if LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(29, 1, 0)
		const char *url = obs_service_get_url(stream_service);
#else
		const char *url = obs_service_get_connect_info(
			stream_service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
#endif
		if (url != NULL && strncmp(url, "ftl", 3) == 0) {
			type = "ftl_output";
		} else if (url != NULL && strncmp(url, "rtmp", 4) != 0) {
			type = "ffmpeg_mpegts_muxer";
		}
	}
	if (!streamOutput ||
	    strcmp(type, obs_output_get_id(streamOutput)) != 0) {
		if (streamOutput) {
			if (obs_output_active(streamOutput))
				obs_output_stop(streamOutput);
			obs_output_release(streamOutput);
		}
		streamOutput = obs_output_create(type, "vertical_canvas_stream",
						 nullptr, nullptr);
		obs_output_set_service(streamOutput, stream_service);
	}

	signal_handler_t *signal = obs_output_get_signal_handler(streamOutput);
	signal_handler_disconnect(signal, "start", stream_output_start, this);
	signal_handler_disconnect(signal, "stop", stream_output_stop, this);
	signal_handler_connect(signal, "start", stream_output_start, this);
	signal_handler_connect(signal, "stop", stream_output_stop, this);

	const auto audio_settings = obs_data_create();

	config_t *config = obs_frontend_get_profile_config();
	const char *mode = config_get_string(config, "Output", "Mode");
	size_t mix_idx = 0;

	if (stream_advanced_settings) {
		if (stream_audio_track > 0)
			mix_idx = stream_audio_track - 1;

		if (record_advanced_settings) {
			obs_data_set_int(audio_settings, "bitrate",
					 audioBitrate);
		} else if (strcmp(mode, "Advanced") == 0) {
			if (!audioBitrate) {
				uint64_t streamTrack = config_get_uint(
					config, "AdvOut", "TrackIndex");
				static const char *trackNames[] = {
					"Track1Bitrate", "Track2Bitrate",
					"Track3Bitrate", "Track4Bitrate",
					"Track5Bitrate", "Track6Bitrate",
				};
				const int advAudioBitrate =
					(int)config_get_uint(
						config, "AdvOut",
						trackNames[streamTrack - 1]);
				obs_data_set_int(audio_settings, "bitrate",
						 advAudioBitrate);
				audioBitrate = advAudioBitrate;
			} else {
				obs_data_set_int(audio_settings, "bitrate",
						 audioBitrate);
			}
		} else {
			if (!audioBitrate) {
				const int sAudioBitrate = (int)config_get_uint(
					config, "SimpleOutput", "ABitrate");
				obs_data_set_int(audio_settings, "bitrate",
						 sAudioBitrate);
				audioBitrate = sAudioBitrate;
			} else {
				obs_data_set_int(audio_settings, "bitrate",
						 audioBitrate);
			}
		}
	} else if (strcmp(mode, "Advanced") == 0) {
		mix_idx = config_get_uint(config, "AdvOut", "TrackIndex") - 1;
		if (!audioBitrate) {
			static const char *trackNames[] = {
				"Track1Bitrate", "Track2Bitrate",
				"Track3Bitrate", "Track4Bitrate",
				"Track5Bitrate", "Track6Bitrate",
			};
			const int advAudioBitrate = (int)config_get_uint(
				config, "AdvOut", trackNames[mix_idx]);
			obs_data_set_int(audio_settings, "bitrate",
					 advAudioBitrate);
			audioBitrate = advAudioBitrate;
		} else {
			obs_data_set_int(audio_settings, "bitrate",
					 audioBitrate);
		}
	} else {
		obs_data_set_string(audio_settings, "rate_control", "CBR");
		if (!audioBitrate) {
			const int sAudioBitrate = (int)config_get_uint(
				config, "SimpleOutput", "ABitrate");
			obs_data_set_int(audio_settings, "bitrate",
					 sAudioBitrate);
			audioBitrate = sAudioBitrate;
		} else {
			obs_data_set_int(audio_settings, "bitrate",
					 audioBitrate);
		}
	}

	obs_output_set_video_encoder(streamOutput, GetStreamVideoEncoder());

	obs_encoder_t *audio_encoder =
		obs_output_get_audio_encoder(streamOutput, 0);
	if (!audio_encoder) {
		audio_encoder = obs_audio_encoder_create(
			"ffmpeg_aac", "vertical_canvas_audio_encoder",
			audio_settings, mix_idx, nullptr);
		obs_encoder_set_audio(audio_encoder, obs_get_audio());
		obs_output_set_audio_encoder(streamOutput, audio_encoder, 0);
	} else {
		obs_encoder_update(audio_encoder, audio_settings);
	}
	obs_data_release(audio_settings);

	SendVendorEvent("streaming_starting");
	const bool success = obs_output_start(streamOutput);
	if (!success) {
		QMetaObject::invokeMethod(
			this, "OnStreamStop", Q_ARG(int, OBS_OUTPUT_ERROR),
			Q_ARG(QString,
			      QString::fromUtf8(obs_output_get_last_error(
				      streamOutput))));
		if (started_video) {
			obs_view_remove(view);
			obs_view_set_source(view, 0, nullptr);
			video = nullptr;
		}
	}
}

void CanvasDock::StopStream()
{
	streamButton->setChecked(false);
	if (obs_output_active(streamOutput)) {
		SendVendorEvent("streaming_stopping");
		obs_output_stop(streamOutput);
	}
	CheckReplayBuffer();
}

void CanvasDock::stream_output_start(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("streaming_started");
	d->StartReplayBuffer();
	QMetaObject::invokeMethod(d, "OnStreamStart");
}

void CanvasDock::stream_output_stop(void *data, calldata_t *calldata)
{
	const char *last_error =
		(const char *)calldata_ptr(calldata, "last_error");
	QString arg_last_error = QString::fromUtf8(last_error);
	const int code = (int)calldata_int(calldata, "code");
	auto d = static_cast<CanvasDock *>(data);
	d->SendVendorEvent("streaming_stopped");
	QMetaObject::invokeMethod(d, "OnStreamStop", Q_ARG(int, code),
				  Q_ARG(QString, arg_last_error));
}

void CanvasDock::DestroyVideo()
{
	if (!video)
		return;
	obs_view_remove(view);
	obs_view_set_source(view, 0, nullptr);
	video = nullptr;
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
			scenes.push_back(
				scenesDock->sceneList->item(i)->text());
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
	return obs_output_active(streamOutput);
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
	auto data = obs_data_create();
	if (!currentSceneName.isEmpty()) {
		auto cs = currentSceneName.toUtf8();
		obs_data_set_string(data, "current_scene", cs.constData());
	}
	if (scenesDock)
		obs_data_set_bool(data, "grid_mode", scenesDock->IsGridMode());

	obs_data_set_int(data, "width", canvas_width);
	obs_data_set_int(data, "height", canvas_height);
	obs_data_set_bool(data, "show_scenes", !hideScenes);
	obs_data_set_bool(data, "preview_disabled", preview_disabled);
	obs_data_set_bool(data, "virtual_cam_warned", virtual_cam_warned);
	obs_data_set_int(data, "video_bitrate", videoBitrate);
	obs_data_set_int(data, "audio_bitrate", audioBitrate);
	obs_data_set_bool(data, "backtrack", startReplay);
	obs_data_set_bool(data, "backtrack_always", replayAlwaysOn);
	obs_data_set_int(data, "backtrack_seconds", replayDuration);
	obs_data_set_string(data, "backtrack_path", replayPath.c_str());
	if (replayOutput) {
		auto hotkeys = obs_hotkeys_save_output(replayOutput);
		obs_data_set_obj(data, "backtrack_hotkeys", hotkeys);
		obs_data_release(hotkeys);
	}

	obs_data_set_string(data, "stream_server", stream_server.c_str());
	obs_data_set_string(data, "stream_key", stream_key.c_str());

	obs_data_set_bool(data, "stream_advanced_settings",
			  stream_advanced_settings);
	obs_data_set_int(data, "stream_audio_track", stream_audio_track);
	obs_data_set_string(data, "stream_encoder", stream_encoder.c_str());
	obs_data_set_obj(data, "stream_encoder_settings",
			 stream_encoder_settings);

	obs_data_set_string(data, "record_path", recordPath.c_str());
	obs_data_set_bool(data, "record_advanced_settings",
			  record_advanced_settings);
	obs_data_set_string(data, "filename_formatting",
			    filename_formatting.c_str());
	obs_data_set_string(data, "file_format", file_format.c_str());
	obs_data_set_int(data, "record_audio_tracks", record_audio_tracks);
	obs_data_set_string(data, "record_encoder", record_encoder.c_str());
	obs_data_set_obj(data, "record_encoder_settings",
			 record_encoder_settings);

	obs_data_array_t *start_hotkey = nullptr;
	obs_data_array_t *stop_hotkey = nullptr;
	obs_hotkey_pair_save(virtual_cam_hotkey, &start_hotkey, &stop_hotkey);
	obs_data_set_array(data, "start_virtual_cam_hotkey", start_hotkey);
	obs_data_set_array(data, "stop_virtual_cam_hotkey", stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);
	start_hotkey = nullptr;
	stop_hotkey = nullptr;
	obs_hotkey_pair_save(record_hotkey, &start_hotkey, &stop_hotkey);
	obs_data_set_array(data, "start_record_hotkey", start_hotkey);
	obs_data_set_array(data, "stop_record_hotkey", stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);
	start_hotkey = nullptr;
	stop_hotkey = nullptr;
	obs_hotkey_pair_save(stream_hotkey, &start_hotkey, &stop_hotkey);
	obs_data_set_array(data, "start_stream_hotkey", start_hotkey);
	obs_data_set_array(data, "stop_stream_hotkey", stop_hotkey);
	obs_data_array_release(start_hotkey);
	obs_data_array_release(stop_hotkey);

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
	obs_data_set_array(data, "transitions", transition_array);
	obs_data_array_release(transition_array);

	obs_data_set_string(
		data, "transition",
		transitionsDock->transition->currentText().toUtf8().constData());

	return data;
}

void CanvasDock::ClearScenes()
{
	if (scenesCombo)
		scenesCombo->clear();
	if (scenesDock && scenesDock->sceneList->count())
		scenesDock->sceneList->clear();
	SwitchScene("", false);
}

void CanvasDock::LoadScenes()
{
	for (uint32_t i = MAX_CHANNELS - 1; i > 0; i--) {
		if (obs_get_output_source(i) == nullptr) {
			obs_set_output_source(i, transitionAudioWrapper);
			break;
		}
	}
	auto sl = GetGlobalScenesList();
	if (scenesCombo)
		scenesCombo->clear();

	if (scenesDock)
		scenesDock->sceneList->clear();

	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		const obs_source_t *src = scenes.sources.array[i];
		obs_data_t *settings = obs_source_get_settings(src);
		if (obs_data_get_bool(settings, "custom_size") &&
		    obs_data_get_int(settings, "cx") == canvas_width &&
		    obs_data_get_int(settings, "cy") == canvas_height) {
			QString name =
				QString::fromUtf8(obs_source_get_name(src));
			if (hideScenes) {
				for (int j = 0; j < sl->count(); j++) {
					auto item = sl->item(j);
					if (item->text() == name) {
						item->setHidden(true);
					}
				}
			}
			if (scenesCombo)
				scenesCombo->addItem(name);
			if (scenesDock)
				scenesDock->sceneList->addItem(name);
			if ((currentSceneName.isEmpty() &&
			     obs_data_get_bool(settings, "canvas_active")) ||
			    name == currentSceneName) {
				if (scenesCombo)
					scenesCombo->setCurrentText(name);
				if (scenesDock) {
					for (int i = 0;
					     i < scenesDock->sceneList->count();
					     i++) {
						auto item =
							scenesDock->sceneList
								->item(i);
						if (item->text() != name)
							continue;
						scenesDock->sceneList
							->setCurrentItem(item);
					}
				}
			}
		}
		obs_data_release(settings);
	}
	obs_frontend_source_list_free(&scenes);
	if ((scenesDock && scenesDock->sceneList->count() == 0) ||
	    (scenesCombo && scenesCombo->count() == 0)) {
		AddScene("", false);
	}

	if (scenesDock && scenesDock->sceneList->currentRow() < 0)
		scenesDock->sceneList->setCurrentRow(0);
}

void CanvasDock::SwitchScene(const QString &scene_name, bool transition)
{
	auto s = scene_name.isEmpty()
			 ? nullptr
			 : obs_get_source_by_name(
				   scene_name.toUtf8().constData());
	if (s == obs_scene_get_source(scene) ||
	    (!obs_source_is_scene(s) && !scene_name.isEmpty())) {
		obs_source_release(s);
		return;
	}
	auto oldSource = obs_scene_get_source(scene);
	auto sh = obs_source_get_signal_handler(oldSource);
	if (sh) {
		signal_handler_disconnect(sh, "item_add", SceneItemAdded, this);
		signal_handler_disconnect(sh, "reorder", SceneReordered, this);
		signal_handler_disconnect(sh, "refresh", SceneRefreshed, this);
	}
	if (!source || obs_weak_source_references_source(source, oldSource)) {
		obs_weak_source_release(source);
		source = obs_source_get_weak_source(s);
		if (video && view)
			obs_view_set_source(view, 0, s);
	} else {
		oldSource = obs_weak_source_get_source(source);
		if (oldSource) {
			auto ost = obs_source_get_type(oldSource);
			if (ost == OBS_SOURCE_TYPE_TRANSITION) {
				auto data = obs_source_get_private_settings(
					obs_scene_get_source(scene));
				if (SwapTransition(
					    GetTransition(obs_data_get_string(
						    data, "transition")))) {
					obs_source_release(oldSource);
					oldSource = obs_weak_source_get_source(
						source);
					signal_handler_t *handler =
						obs_source_get_signal_handler(
							oldSource);
					signal_handler_connect(
						handler, "transition_stop",
						tranistion_override_stop, this);
				}
				obs_data_release(data);

				auto sourceA = obs_transition_get_source(
					oldSource, OBS_TRANSITION_SOURCE_A);
				if (sourceA != obs_scene_get_source(scene))
					obs_transition_set(
						oldSource,
						obs_scene_get_source(scene));
				obs_source_release(sourceA);
				if (transition) {
					obs_transition_start(
						oldSource,
						OBS_TRANSITION_MODE_AUTO,
						obs_frontend_get_transition_duration(),
						s);
				} else {
					obs_transition_set(oldSource, s);
				}
			} else {
				obs_weak_source_release(source);
				source = obs_source_get_weak_source(s);
				if (video && view)
					obs_view_set_source(view, 0, s);
			}
			obs_source_release(oldSource);
		} else {
			obs_weak_source_release(source);
			source = obs_source_get_weak_source(s);
			if (video && view)
				obs_view_set_source(view, 0, s);
		}
	}
	scene = obs_scene_from_source(s);
	if (scene) {
		sh = obs_source_get_signal_handler(s);
		if (sh) {
			signal_handler_connect(sh, "item_add", SceneItemAdded,
					       this);
			signal_handler_connect(sh, "reorder", SceneReordered,
					       this);
			signal_handler_connect(sh, "refresh", SceneRefreshed,
					       this);
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
			for (int i = 0; i < scenesDock->sceneList->count();
			     i++) {
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
		obs_data_set_string(d, "old_scene",
				    oldName.toUtf8().constData());
		obs_data_set_string(d, "new_scene",
				    currentSceneName.toUtf8().constData());
		obs_websocket_vendor_emit_event(vendor, "switch_scene", d);
		obs_data_release(d);
	}
}

void CanvasDock::tranistion_override_stop(void *data, calldata_t *)
{
	auto dock = (CanvasDock *)data;
	auto tn = dock->transitionsDock->transition->currentText().toUtf8();
	dock->SwapTransition(dock->GetTransition(tn.constData()));
}

obs_source_t *CanvasDock::GetTransition(const char *transition_name)
{
	if (!transition_name || !strlen(transition_name))
		return nullptr;
	for (auto transition : transitions) {
		if (strcmp(transition_name, obs_source_get_name(transition)) ==
		    0) {
			return transition;
		}
	}
	return nullptr;
}

bool CanvasDock::SwapTransition(obs_source_t *newTransition)
{
	if (!newTransition ||
	    obs_weak_source_references_source(source, newTransition))
		return false;

	obs_transition_set_size(newTransition, canvas_width, canvas_height);

	obs_source_t *oldTransition = obs_weak_source_get_source(source);
	if (!oldTransition ||
	    obs_source_get_type(oldTransition) != OBS_SOURCE_TYPE_TRANSITION) {
		obs_source_release(oldTransition);
		obs_weak_source_release(source);
		source = obs_source_get_weak_source(newTransition);
		if (video && view)
			obs_view_set_source(view, 0, newTransition);
		obs_source_inc_showing(newTransition);
		obs_source_inc_active(newTransition);
		return true;
	}
	signal_handler_t *handler =
		obs_source_get_signal_handler(oldTransition);
	signal_handler_disconnect(handler, "transition_stop",
				  tranistion_override_stop, this);
	obs_source_inc_showing(newTransition);
	obs_source_inc_active(newTransition);
	obs_transition_swap_begin(newTransition, oldTransition);
	obs_weak_source_release(source);
	source = obs_source_get_weak_source(newTransition);
	if (video && view)
		obs_view_set_source(view, 0, newTransition);
	obs_transition_swap_end(newTransition, oldTransition);
	obs_source_dec_showing(oldTransition);
	obs_source_dec_active(oldTransition);
	obs_source_release(oldTransition);
	return true;
}

void CanvasDock::source_rename(void *data, calldata_t *calldata)
{
	const auto d = static_cast<CanvasDock *>(data);
	const auto prev_name =
		QString::fromUtf8(calldata_string(calldata, "prev_name"));
	const auto new_name =
		QString::fromUtf8(calldata_string(calldata, "new_name"));
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
		for (size_t i = 0; i < count; i++) {
			auto item = obs_data_array_item(c, i);
			auto n = QString::fromUtf8(
				obs_data_get_string(item, "scene"));
			if (n == prev_name) {
				obs_data_set_string(
					item, "scene",
					calldata_string(calldata, "new_name"));
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
			const bool selected = d->scenesCombo->currentText() ==
					      prev_name;
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
	if (obs_weak_source_references_source(d->source, source) ||
	    source == obs_scene_get_source(d->scene)) {
		d->SwitchScene("", false);
	}
	const auto name = QString::fromUtf8(obs_source_get_name(source));
	if (name.isEmpty())
		return;
	if (d->scenesDock) {
		for (int i = 0; i < d->scenesDock->sceneList->count(); i++) {
			auto item = d->scenesDock->sceneList->item(i);
			if (item->text() != name)
				continue;
			d->scenesDock->sceneList->takeItem(i);
		}
		auto r = d->scenesDock->sceneList->currentRow();
		auto c = d->scenesDock->sceneList->count();
		if ((r < 0 && c > 0) || r >= c) {
			d->scenesDock->sceneList->setCurrentRow(0);
		}
	}
	if (d->scenesCombo) {
		for (int i = 0; i < d->scenesCombo->count(); i++) {
			if (d->scenesCombo->itemText(i) != name)
				continue;
			d->scenesCombo->removeItem(i);
		}
		if (d->scenesCombo->currentIndex() < 0 &&
		    d->scenesCombo->count()) {
			d->scenesCombo->setCurrentIndex(0);
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
	if (obs_data_get_bool(settings, "custom_size") &&
	    obs_data_get_int(settings, "cx") == d->canvas_width &&
	    obs_data_get_int(settings, "cy") == d->canvas_height) {
		const QString name =
			QString::fromUtf8(obs_source_get_name(source));
		if (d->scenesCombo)
			obs_data_set_bool(settings, "canvas_active",
					  d->scenesCombo->currentText() ==
						  name);
	}
	obs_data_release(settings);
}

void CanvasDock::FinishLoading()
{
	CheckReplayBuffer(true);
	if (!first_time)
		return;
	if (!action->isChecked())
		action->trigger();
	auto main = ((QMainWindow *)parentWidget());

	main->addDockWidget(Qt::RightDockWidgetArea, this);
	setFloating(false);

	if (!scenesDockAction->isChecked())
		scenesDockAction->trigger();
	auto sd = main->findChild<QDockWidget *>(QStringLiteral("scenesDock"));
	if (sd) {
		auto area = main->dockWidgetArea(sd);
		if (area == Qt::NoDockWidgetArea) {
			main->addDockWidget(Qt::RightDockWidgetArea,
					    scenesDock);
			main->splitDockWidget(this, scenesDock, Qt::Horizontal);
		} else {
			main->addDockWidget(area, scenesDock);
			main->splitDockWidget(sd, scenesDock, Qt::Vertical);
		}
	} else {
		main->addDockWidget(Qt::RightDockWidgetArea, scenesDock);
		main->splitDockWidget(this, scenesDock, Qt::Horizontal);
	}
	scenesDock->setFloating(false);

	if (!sourcesDockAction->isChecked())
		sourcesDockAction->trigger();
	sd = main->findChild<QDockWidget *>(QStringLiteral("sourcesDock"));
	if (sd) {
		auto area = main->dockWidgetArea(sd);
		if (area == Qt::NoDockWidgetArea) {
			main->addDockWidget(Qt::RightDockWidgetArea,
					    sourcesDock);
			main->splitDockWidget(this, sourcesDock,
					      Qt::Horizontal);
		} else {
			main->addDockWidget(area, sourcesDock);
			main->splitDockWidget(sd, sourcesDock, Qt::Vertical);
		}
	} else {
		main->addDockWidget(Qt::RightDockWidgetArea, sourcesDock);
		main->splitDockWidget(this, sourcesDock, Qt::Horizontal);
	}
	sourcesDock->setFloating(false);

	if (!transitionsDockAction->isChecked())
		transitionsDockAction->trigger();
	sd = main->findChild<QDockWidget *>(QStringLiteral("transitionsDock"));
	if (sd) {
		auto area = main->dockWidgetArea(sd);
		if (area == Qt::NoDockWidgetArea) {
			main->addDockWidget(Qt::RightDockWidgetArea,
					    transitionsDock);
			main->splitDockWidget(this, transitionsDock,
					      Qt::Horizontal);
		} else {
			main->addDockWidget(area, transitionsDock);
			main->splitDockWidget(sd, transitionsDock,
					      Qt::Vertical);
		}
	} else {
		main->addDockWidget(Qt::RightDockWidgetArea, transitionsDock);
		main->splitDockWidget(this, transitionsDock, Qt::Horizontal);
	}
	transitionsDock->setFloating(false);
	save_canvas();
}

void CanvasDock::OnRecordStart()
{
	recordButton->setChecked(true);
	recordButton->setIcon(recordActiveIcon);
	recordButton->setText("00:00");
	recordButton->setStyleSheet(
		QString::fromUtf8("background: rgb(255,0,0);"));
	StartReplayBuffer();
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
		if (!config_get_bool(obs_frontend_get_profile_config(), "Video",
				     "AutoRemux"))
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
		const auto main_window = static_cast<QMainWindow *>(
			obs_frontend_get_main_window());
		QMetaObject::invokeMethod(main_window, "RecordingFileChanged",
					  Q_ARG(QString, path));
	}
}

void CanvasDock::OnRecordStop(int code, QString last_error)
{
	recordButton->setChecked(false);
	recordButton->setIcon(recordInactiveIcon);
	recordButton->setText("");
	recordButton->setStyleSheet(QString::fromUtf8(""));
	HandleRecordError(code, last_error);
	CheckReplayBuffer();

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
			blog(LOG_WARNING,
			     "[vertical-canvas] record stop error %s",
			     last_error.toUtf8().constData());
		} else {
			blog(LOG_WARNING,
			     "[vertical-canvas] record stop error %i", code);
		}
	}
	if (code == OBS_OUTPUT_UNSUPPORTED && isVisible()) {
		QMessageBox::critical(
			this,
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Output.RecordFail.Title")),
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Output.RecordFail.Unsupported")));

	} else if (code == OBS_OUTPUT_ENCODE_ERROR && isVisible()) {
		QString msg =
			last_error.isEmpty()
				? QString::fromUtf8(obs_frontend_get_locale_string(
					  "Output.RecordError.EncodeErrorMsg"))
				: QString::fromUtf8(
					  obs_frontend_get_locale_string(
						  "Output.RecordError.EncodeErrorMsg.LastError"))
					  .arg(last_error);
		QMessageBox::warning(
			this,
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Output.RecordError.Title")),
			msg);

	} else if (code == OBS_OUTPUT_NO_SPACE && isVisible()) {
		QMessageBox::warning(
			this,
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Output.RecordNoSpace.Title")),
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Output.RecordNoSpace.Msg")));

	} else if (code != OBS_OUTPUT_SUCCESS && isVisible()) {
		QMessageBox::critical(
			this,
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Output.RecordError.Title")),
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Output.RecordError.Msg")) +
				(!last_error.isEmpty()
					 ? QString::fromUtf8("\n\n") +
						   last_error
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
	streamButton->setStyleSheet(
		QString::fromUtf8("background: rgb(0,210,153);"));
	StartReplayBuffer();
}

void CanvasDock::OnStreamStop(int code, QString last_error)
{
	streamButton->setChecked(false);
	streamButton->setIcon(streamInactiveIcon);
	streamButton->setText("");
	streamButton->setStyleSheet(QString::fromUtf8(""));
	const char *errorDescription = "";

	bool use_last_error = false;
	bool encode_error = false;

	switch (code) {
	case OBS_OUTPUT_BAD_PATH:
		errorDescription = obs_frontend_get_locale_string(
			"Output.ConnectFail.BadPath");
		break;

	case OBS_OUTPUT_CONNECT_FAILED:
		use_last_error = true;
		if (stream_server.find("tiktok") != std::string::npos) {
			last_error = QString::fromUtf8(
				obs_module_text("tiktokError"));
		}
		errorDescription = obs_frontend_get_locale_string(
			"Output.ConnectFail.ConnectFailed");
		break;

	case OBS_OUTPUT_INVALID_STREAM:
		if (stream_server.find("tiktok") != std::string::npos) {
			use_last_error = true;
			last_error = QString::fromUtf8(
				obs_module_text("tiktokError"));
		}
		errorDescription = obs_frontend_get_locale_string(
			"Output.ConnectFail.InvalidStream");
		break;

	case OBS_OUTPUT_ENCODE_ERROR:
		encode_error = true;
		break;

	default:
	case OBS_OUTPUT_ERROR:
		use_last_error = true;
		errorDescription = obs_frontend_get_locale_string(
			"Output.ConnectFail.Error");
		break;

	case OBS_OUTPUT_DISCONNECTED:
		/* doesn't happen if output is set to reconnect.  note that
		 * reconnects are handled in the output, not in the UI */
		use_last_error = true;
		errorDescription = obs_frontend_get_locale_string(
			"Output.ConnectFail.Disconnected");
	}
	if (code != OBS_OUTPUT_SUCCESS) {
		if (use_last_error && !last_error.isEmpty()) {
			blog(LOG_WARNING,
			     "[vertical-canvas] stream stop error %s",
			     last_error.toUtf8().constData());
		} else {
			blog(LOG_WARNING,
			     "[vertical-canvas] stream stop error %i", code);
		}
	}
	if (encode_error) {
		QString msg =
			last_error.isEmpty()
				? QString::fromUtf8(
					  obs_frontend_get_locale_string(
						  "Output.StreamEncodeError.Msg"))
				: QString::fromUtf8(
					  obs_frontend_get_locale_string(
						  "Output.StreamEncodeError.Msg.LastError"))
					  .arg(last_error);
		QMessageBox::information(
			this,
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Output.StreamEncodeError.Title")),
			msg);

	} else if (code != OBS_OUTPUT_SUCCESS && isVisible()) {
		QMessageBox::information(
			this,
			QString::fromUtf8(obs_frontend_get_locale_string(
				"Output.ConnectFail.Title")),
			QString::fromUtf8(errorDescription) +
				(use_last_error && !last_error.isEmpty()
					 ? QString::fromUtf8("\n\n") +
						   last_error
					 : QString::fromUtf8("")));
	}
	CheckReplayBuffer();
}

void CanvasDock::OnReplayBufferStart()
{
	replayButton->setIcon(replayActiveIcon);
	replayButton->setStyleSheet(
		QString::fromUtf8("background: rgb(26,87,255);"));
}

void CanvasDock::OnReplayBufferStop(int code, QString last_error)
{
	replayButton->setIcon(replayInactiveIcon);
	replayButton->setStyleSheet(QString::fromUtf8(""));
	if (!replayStatusResetTimer.isActive())
		replayStatusResetTimer.start(4000);
	if (restart_video)
		ProfileChanged();
	HandleRecordError(code, last_error);
	if (code == OBS_OUTPUT_SUCCESS) {
		CheckReplayBuffer(true);
		QTimer::singleShot(500, this,
				   [this] { CheckReplayBuffer(true); });
	}
}

void CanvasDock::MainSceneChanged()
{
	auto scene = obs_frontend_get_current_scene();
	if (!scene) {
		if (linkedButton)
			linkedButton->setChecked(false);
		return;
	}

	auto ss = obs_source_get_settings(scene);
	obs_source_release(scene);
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
		if (obs_data_get_int(item, "width") == canvas_width &&
		    obs_data_get_int(item, "height") == canvas_height) {
			found = item;
			break;
		}
		obs_data_release(item);
	}
	if (found) {
		auto sn =
			QString::fromUtf8(obs_data_get_string(found, "scene"));
		SwitchScene(sn);
		if (linkedButton)
			linkedButton->setChecked(true);
	} else if (linkedButton) {
		linkedButton->setChecked(false);
	}
	obs_data_release(found);
	obs_data_array_release(c);
}

bool CanvasDock::start_virtual_cam_hotkey(void *data, obs_hotkey_pair_id id,
					  obs_hotkey_t *hotkey, bool pressed)
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

bool CanvasDock::stop_virtual_cam_hotkey(void *data, obs_hotkey_pair_id id,
					 obs_hotkey_t *hotkey, bool pressed)
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

bool CanvasDock::start_recording_hotkey(void *data, obs_hotkey_pair_id id,
					obs_hotkey_t *hotkey, bool pressed)
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

bool CanvasDock::stop_recording_hotkey(void *data, obs_hotkey_pair_id id,
				       obs_hotkey_t *hotkey, bool pressed)
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

bool CanvasDock::start_streaming_hotkey(void *data, obs_hotkey_pair_id id,
					obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto d = static_cast<CanvasDock *>(data);
	if (obs_output_active(d->streamOutput))
		return false;
	QMetaObject::invokeMethod(d, "StreamButtonClicked");
	return true;
}

bool CanvasDock::stop_streaming_hotkey(void *data, obs_hotkey_pair_id id,
				       obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto d = static_cast<CanvasDock *>(data);
	if (!obs_output_active(d->streamOutput))
		return false;
	QMetaObject::invokeMethod(d, "StreamButtonClicked");
	return true;
}

QIcon CanvasDock::GetIconFromType(enum obs_icon_type icon_type) const
{
	const auto main_window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());

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
		return main_window->property("audioProcessOutputIcon")
			.value<QIcon>();
	default:
		return main_window->property("defaultIcon").value<QIcon>();
	}
}

QIcon CanvasDock::GetSceneIcon() const
{
	const auto main_window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	return main_window->property("sceneIcon").value<QIcon>();
}

QIcon CanvasDock::GetGroupIcon() const
{
	const auto main_window =
		static_cast<QMainWindow *>(obs_frontend_get_main_window());
	return main_window->property("groupIcon").value<QIcon>();
}

void CanvasDock::MainStreamStart()
{
	StartReplayBuffer();
}

void CanvasDock::MainStreamStop()
{
	CheckReplayBuffer();
}

void CanvasDock::MainRecordStart()
{
	StartReplayBuffer();
}

void CanvasDock::MainRecordStop()
{
	CheckReplayBuffer();
}

void CanvasDock::MainReplayBufferStart()
{
	StartReplayBuffer();
}

void CanvasDock::MainReplayBufferStop()
{
	CheckReplayBuffer();
}

void CanvasDock::MainVirtualCamStart()
{
	StartReplayBuffer();
}

void CanvasDock::MainVirtualCamStop()
{
	CheckReplayBuffer();
}

void CanvasDock::SceneReordered(void *data, calldata_t *params)
{
	CanvasDock *window = static_cast<CanvasDock *>(data);

	obs_scene_t *scene = (obs_scene_t *)calldata_ptr(params, "scene");

	QMetaObject::invokeMethod(window, "ReorderSources",
				  Q_ARG(OBSScene, OBSScene(scene)));
}

void CanvasDock::ReorderSources(OBSScene scene)
{
	if (scene != this->scene || sourcesDock->sourceList->IgnoreReorder())
		return;

	sourcesDock->sourceList->ReorderItems();
}

void CanvasDock::SceneRefreshed(void *data, calldata_t *params)
{
	CanvasDock *window = static_cast<CanvasDock *>(data);

	obs_scene_t *scene = (obs_scene_t *)calldata_ptr(params, "scene");

	QMetaObject::invokeMethod(window, "RefreshSources",
				  Q_ARG(OBSScene, OBSScene(scene)));
}

void CanvasDock::RefreshSources(OBSScene scene)
{
	if (scene != this->scene || sourcesDock->sourceList->IgnoreReorder())
		return;

	sourcesDock->sourceList->RefreshItems();
}

void CanvasDock::SceneItemAdded(void *data, calldata_t *params)
{
	CanvasDock *window = static_cast<CanvasDock *>(data);

	obs_sceneitem_t *item = (obs_sceneitem_t *)calldata_ptr(params, "item");

	QMetaObject::invokeMethod(window, "AddSceneItem",
				  Q_ARG(OBSSceneItem, OBSSceneItem(item)));
}

void CanvasDock::AddSceneItem(OBSSceneItem item)
{
	obs_scene_t *scene = obs_sceneitem_get_scene(item);

	if (sourcesDock && this->scene == scene)
		sourcesDock->sourceList->Add(item);

	obs_scene_enum_items(scene, select_one, (obs_sceneitem_t *)item);
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

void CanvasDock::ResizeScenes()
{
	if (scenesCombo) {
		for (int i = 0; i < scenesCombo->count(); i++) {
			ResizeScene(scenesCombo->itemText(i));
		}
	}
	if (scenesDock) {
		for (int i = 0; i < scenesDock->sceneList->count(); i++) {
			ResizeScene(scenesDock->sceneList->item(i)->text());
		}
	}
}

void CanvasDock::ResizeScene(QString scene_name)
{
	if (scene_name.isEmpty())
		return;
	auto s = obs_get_source_by_name(scene_name.toUtf8().constData());
	if (!s)
		return;
	auto scene = obs_scene_from_source(s);
	if (!scene) {
		obs_source_release(s);
		return;
	}
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *) {
			obs_source_get_ref(obs_sceneitem_get_source(item));
			return true;
		},
		nullptr);
	obs_source_save(s);
	auto data = obs_source_get_settings(s);
	obs_data_set_int(data, "cx", canvas_width);
	obs_data_set_int(data, "cy", canvas_height);
	obs_source_load(s);
	obs_data_release(data);
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *) {
			obs_source_release(obs_sceneitem_get_source(item));
			return true;
		},
		nullptr);
	obs_source_release(s);
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
			obs_sceneitem_group_enum_items(item, nudge_callback,
						       &new_offset);
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

void CanvasDock::NewerVersionAvailable(QString version)
{
	newer_version_available = version;
	configButton->setStyleSheet(
		QString::fromUtf8("background: rgb(192,128,0);"));
}

void CanvasDock::ProfileChanged()
{
	if (obs_output_active(streamOutput) || obs_output_active(recordOutput))
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
		StartReplayBuffer();

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

	bool closeProjectors = config_get_bool(obs_frontend_get_global_config(),
					       "BasicWindow",
					       "CloseExistingProjectors");

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

void CanvasDock::AddProjectorMenuMonitors(QMenu *parent, QObject *target,
					  const char *slot)
{
	QAction *action;
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
				       .arg(QString::fromUtf8(
					       obs_frontend_get_locale_string(
						       "Display")))
				       .arg(QString::number(i + 1));
		}
		QString str =
			QString("%1: %2x%3 @ %4,%5")
				.arg(name,
				     QString::number(screenGeometry.width() *
						     ratio),
				     QString::number(screenGeometry.height() *
						     ratio),
				     QString::number(screenGeometry.x()),
				     QString::number(screenGeometry.y()));

		action = parent->addAction(str, target, slot);
		action->setProperty("monitor", i);
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

	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return;
	obs_frontend_open_projector("Source", monitor, nullptr,
				    obs_source_get_name(source));
}

void CanvasDock::updateStreamKey(const QString &newStreamKey)
{
	// Your code to update the stream_key, assuming stream_key is a member variable
	this->stream_key = newStreamKey.toStdString();
	// any additional actions needed to apply the new stream key
}

void CanvasDock::updateStreamServer(const QString& newStreamServer) {
    // Your code to update the stream_server, assuming stream_server is a member variable
    this->stream_server = newStreamServer.toStdString();
    // any additional actions needed to apply the new stream server
}

LockedCheckBox::LockedCheckBox() {}

LockedCheckBox::LockedCheckBox(QWidget *parent) : QCheckBox(parent) {}

VisibilityCheckBox::VisibilityCheckBox() {}

VisibilityCheckBox::VisibilityCheckBox(QWidget *parent) : QCheckBox(parent) {}
