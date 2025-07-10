#include <curl/curl.h>
#include <util/threading.h>
#include <util/platform.h>
#include <util/darray.h>
#include <util/dstr.h>
#include <obs-data.h>
#include "file-updater.h"

#define warn(msg, ...) blog(LOG_WARNING, "%s" msg, info->log_prefix, ##__VA_ARGS__)
#define info(msg, ...) blog(LOG_WARNING, "%s" msg, info->log_prefix, ##__VA_ARGS__)

struct update_info {
	char error[CURL_ERROR_SIZE];
	struct curl_slist *header;
	DARRAY(uint8_t) file_data;
	char *user_agent;
	CURL *curl;
	char *url;

	confirm_file_callback_t callback;
	void *param;

	pthread_t thread;
	bool thread_created;
	char *log_prefix;
};

void update_info_destroy(struct update_info *info)
{
	if (!info)
		return;

	if (info->thread_created)
		pthread_join(info->thread, NULL);

	da_free(info->file_data);
	bfree(info->log_prefix);
	bfree(info->user_agent);
	bfree(info->url);

	if (info->header)
		curl_slist_free_all(info->header);
	if (info->curl)
		curl_easy_cleanup(info->curl);
	bfree(info);
}

static size_t http_write(void *ptr, size_t size, size_t nmemb, void *uinfo)
{
	size_t total = size * nmemb;
	struct update_info *info = (struct update_info *)uinfo;

	if (total)
		da_push_back_array(info->file_data, ptr, total);

	return total;
}

static bool do_http_request(struct update_info *info, const char *url, long *response_code)
{
	CURLcode code;
	uint8_t null_terminator = 0;

	da_resize(info->file_data, 0);
	curl_easy_setopt(info->curl, CURLOPT_URL, url);
	curl_easy_setopt(info->curl, CURLOPT_HTTPHEADER, info->header);
	curl_easy_setopt(info->curl, CURLOPT_ERRORBUFFER, info->error);
	curl_easy_setopt(info->curl, CURLOPT_WRITEFUNCTION, http_write);
	curl_easy_setopt(info->curl, CURLOPT_WRITEDATA, info);
	curl_easy_setopt(info->curl, CURLOPT_FAILONERROR, true);
	curl_easy_setopt(info->curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(info->curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(info->curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_REVOKE_BEST_EFFORT);

	code = curl_easy_perform(info->curl);
	if (code != CURLE_OK) {
		warn("Remote update of URL \"%s\" failed: %s", url, info->error);
		return false;
	}

	if (curl_easy_getinfo(info->curl, CURLINFO_RESPONSE_CODE, response_code) != CURLE_OK)
		return false;

	if (*response_code >= 400) {
		warn("Remote update of URL \"%s\" failed: HTTP/%ld", url, *response_code);
		return false;
	}

	da_push_back(info->file_data, &null_terminator);

	return true;
}

struct file_update_data {
	const char *name;
	int version;
	bool newer;
	bool found;
};

static void *single_file_thread(void *data)
{
	struct update_info *info = data;
	struct file_download_data download_data;
	long response_code;

	info->curl = curl_easy_init();
	if (!info->curl) {
		warn("Could not initialize Curl");
		return NULL;
	}

	if (!do_http_request(info, info->url, &response_code))
		return NULL;
	if (!info->file_data.array || !info->file_data.array[0])
		return NULL;

	download_data.name = info->url;
	download_data.version = 0;
	download_data.buffer.da = info->file_data.da;
	info->callback(info->param, &download_data);
	return NULL;
}

update_info_t *update_info_create_single(const char *log_prefix, const char *user_agent, const char *file_url,
					 confirm_file_callback_t confirm_callback, void *param)
{
	struct update_info *info;

	if (!log_prefix)
		log_prefix = "";

	info = bzalloc(sizeof(*info));
	info->log_prefix = bstrdup(log_prefix);
	info->user_agent = bstrdup(user_agent);
	info->url = bstrdup(file_url);
	info->callback = confirm_callback;
	info->param = param;

	if (pthread_create(&info->thread, NULL, single_file_thread, info) == 0)
		info->thread_created = true;

	return info;
}
