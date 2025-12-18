#ifndef STATION_DATA_H
#define STATION_DATA_H

#include "audio_pipeline_manager.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Structure to define a radio station's properties.
 * Note: Members are now non-const to allow dynamic allocation.
 */
typedef struct {
  char *call_sign;    // Station's call sign or name
  char *origin;       // Station's origin (city or school)
  char *uri;          // Stream URI
  codec_type_t codec; // Codec type for the stream
} station_t;

/**
 * @brief Pointer to the array of station data.
 */
extern station_t *radio_stations;

/**
 * @brief Current number of available stations.
 */
extern int station_count;

/**
 * @brief Initialize station data subsystem.
 * Mounts filesystem, loads stations.json. If missing, creates defaults.
 */
void init_station_data(void);

/**
 * @brief Save current station list to filesystem.
 * @return 0 on success, < 0 on failure.
 */
int save_station_data(void);

/**
 * @brief Free all allocated station memory.
 */
void free_station_data(void);

/**
 * @brief Get the stations list as a JSON string.
 * Caller must free the returned string.
 * @return JSON string or NULL on error.
 */
char *get_stations_json(void);

/**
 * @brief Update stations from a JSON string.
 * @param json_str JSON string containing array of stations.
 * @return 0 on success, < 0 on failure.
 */
int update_stations_from_json(const char *json_str);

#ifdef __cplusplus
}
#endif

#endif // STATION_DATA_H