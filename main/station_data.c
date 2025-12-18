#include "station_data.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "STATION_DATA";
#define STORAGE_BASE_PATH "/spiffs"
#define STATION_FILE "/spiffs/stations.json"

station_t *radio_stations = NULL;
int station_count = 0;

// Temporary structure for defaults to avoid const warnings with the main struct
typedef struct {
  const char *call_sign;
  const char *origin;
  const char *uri;
  codec_type_t codec;
} default_station_t;

static const default_station_t default_stations[] = {
    {"KEXP", "Seattle", "https://kexp.streamguys1.com/kexp160.aac",
     CODEC_TYPE_AAC},
    {"KBUT", "Crested Butte",
     "http://playerservices.streamtheworld.com/api/livestream-redirect/"
     "KBUTFM.mp3",
     CODEC_TYPE_MP3},
    {"KSUT", "4 Corners", "https://ksut.streamguys1.com/kute", CODEC_TYPE_AAC},
    {"KDUR", "Durango", "https://kdurradio.fortlewis.edu/stream",
     CODEC_TYPE_MP3},
    {"KOTO", "Telluride", "http://26193.live.streamtheworld.com/KOTOFM.mp3",
     CODEC_TYPE_MP3},
    {"KHEN", "Salida", "https://stream.pacificaservice.org:9000/khen_128",
     CODEC_TYPE_MP3},
    {"KWSB", "Gunnison", "https://kwsb.streamguys1.com/live", CODEC_TYPE_MP3},
    {"KFFP", "Portland", "http://listen.freeformportland.org:8000/stream",
     CODEC_TYPE_MP3},
    {"KBOO", "Portland", "https://live.kboo.fm:8443/high", CODEC_TYPE_MP3},
    {"KXLU", "Loyola Marymnt", "http://kxlu.streamguys1.com:80/kxlu-lo",
     CODEC_TYPE_AAC},
    {"WPRB", "Princeton", "https://wprb.streamguys1.com/listen.mp3",
     CODEC_TYPE_AAC},
    {"WMBR", "MIT", "https://wmbr.org:8002/hi", CODEC_TYPE_MP3},
    {"KALX", "Berkeley", "https://stream.kalx.berkeley.edu:8443/kalx-128.mp3",
     CODEC_TYPE_MP3},
    {"WFUV", "Fordham", "https://onair.wfuv.org/onair-hi", CODEC_TYPE_MP3},
    {"KUFM", "Missoula",
     "https://playerservices.streamtheworld.com/api/livestream-redirect/"
     "KUFMFM.mp3",
     CODEC_TYPE_MP3},
    {"KRCL", "Salt Lake City", "http://stream.xmission.com:8000/krcl-low",
     CODEC_TYPE_AAC},
     // {"KRRC", "Reed College", "https://stream.radiojar.com/3wg5hpdkfkeuv",
     // CODEC_TYPE_MP3}
 
};
static const int default_station_count =
    sizeof(default_stations) / sizeof(default_stations[0]);

static void load_stations_from_file(void);
static void create_default_station_file(void);

void init_station_data(void) {
  ESP_LOGI(TAG, "Initializing SPIFFS");

  esp_vfs_spiffs_conf_t conf = {.base_path = STORAGE_BASE_PATH,
                                .partition_label = NULL,
                                .max_files = 5,
                                .format_if_mount_failed = true};

  esp_err_t ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "Failed to find SPIFFS partition");
    } else {
      ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
    }
    return;
  }

  size_t total = 0, used = 0;
  ret = esp_spiffs_info(NULL, &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
  }

  // Check if file exists
  struct stat st;
  if (stat(STATION_FILE, &st) == 0) {
    ESP_LOGI(TAG, "Station file found, loading...");
    load_stations_from_file();
  } else {
    ESP_LOGI(TAG, "Station file not found, creating defaults...");
    create_default_station_file();
    load_stations_from_file(); // Load back what we just wrote
  }
}

void free_station_data(void) {
  if (radio_stations != NULL) {
    for (int i = 0; i < station_count; i++) {
      free(radio_stations[i].call_sign);
      free(radio_stations[i].origin);
      free(radio_stations[i].uri);
    }
    free(radio_stations);
    radio_stations = NULL;
  }
  station_count = 0;
}

static void create_default_station_file(void) {
  // Populate global array temporarily from defaults to use save function?
  // Or just construct JSON directly. Let's construct JSON directly to be safe
  // and clean.

  cJSON *root = cJSON_CreateArray();
  for (int i = 0; i < default_station_count; i++) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "call_sign", default_stations[i].call_sign);
    cJSON_AddStringToObject(item, "origin", default_stations[i].origin);
    cJSON_AddStringToObject(item, "uri", default_stations[i].uri);
    cJSON_AddNumberToObject(item, "codec", default_stations[i].codec);
    cJSON_AddItemToArray(root, item);
  }

  char *json_str = cJSON_Print(root);
  FILE *f = fopen(STATION_FILE, "w");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open file for writing defaults");
    cJSON_Delete(root);
    free(json_str);
    return;
  }
  fprintf(f, "%s", json_str);
  fclose(f);

  ESP_LOGI(TAG, "Created default stations file");

  free(json_str);
  cJSON_Delete(root);
}

int save_station_data(void) {
  char *json_str = get_stations_json();
  if (json_str == NULL) {
    return -1;
  }

  FILE *f = fopen(STATION_FILE, "w");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open file for writing");
    free(json_str);
    return -1;
  }
  fprintf(f, "%s", json_str);
  fclose(f);

  ESP_LOGI(TAG, "Saved stations to file");
  free(json_str);
  return 0;
}

char *get_stations_json(void) {
  cJSON *root = cJSON_CreateArray();
  for (int i = 0; i < station_count; i++) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "call_sign", radio_stations[i].call_sign);
    cJSON_AddStringToObject(item, "origin", radio_stations[i].origin);
    cJSON_AddStringToObject(item, "uri", radio_stations[i].uri);
    cJSON_AddNumberToObject(item, "codec", radio_stations[i].codec);
    cJSON_AddItemToArray(root, item);
  }
  char *out = cJSON_Print(root);
  cJSON_Delete(root);
  return out;
}

static void load_stations_from_file(void) {
  FILE *f = fopen(STATION_FILE, "r");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open station data file");
    return;
  }

  fseek(f, 0, SEEK_END);
  long length = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *data = (char *)malloc(length + 1);
  if (!data) {
    ESP_LOGE(TAG, "Failed to allocate memory for reading file");
    fclose(f);
    return;
  }

  fread(data, 1, length, f);
  data[length] = '\0';
  fclose(f);

  int ret = update_stations_from_json(data);
  free(data);

  if (ret == 0) {
    ESP_LOGI(TAG, "Loaded %d stations from file", station_count);
  }
}

int update_stations_from_json(const char *json_str) {
  cJSON *json = cJSON_Parse(json_str);
  if (json == NULL) {
    ESP_LOGE(TAG, "Failed to parse JSON");
    return -1;
  }

  if (!cJSON_IsArray(json)) {
    ESP_LOGE(TAG, "JSON is not an array");
    cJSON_Delete(json);
    return -1;
  }

  int new_count = cJSON_GetArraySize(json);

  // Allocate new array first
  station_t *new_stations = (station_t *)malloc(sizeof(station_t) * new_count);
  if (!new_stations) {
    ESP_LOGE(TAG, "Failed to allocate memory for new stations");
    cJSON_Delete(json);
    return -1;
  }

  memset(new_stations, 0, sizeof(station_t) * new_count);

  int idx = 0;
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, json) {
    cJSON *call_sign = cJSON_GetObjectItem(item, "call_sign");
    cJSON *origin = cJSON_GetObjectItem(item, "origin");
    cJSON *uri = cJSON_GetObjectItem(item, "uri");
    cJSON *codec = cJSON_GetObjectItem(item, "codec");

    if (cJSON_IsString(call_sign) && cJSON_IsString(origin) &&
        cJSON_IsString(uri) && cJSON_IsNumber(codec)) {

      new_stations[idx].call_sign = strdup(call_sign->valuestring);
      new_stations[idx].origin = strdup(origin->valuestring);
      new_stations[idx].uri = strdup(uri->valuestring);
      new_stations[idx].codec = (codec_type_t)codec->valueint;
      idx++;
    }
  }

  // Now replace the global data
  free_station_data();
  radio_stations = new_stations;
  station_count = idx; // Use actual read count, in case of partial failures

  cJSON_Delete(json);
  return 0;
}