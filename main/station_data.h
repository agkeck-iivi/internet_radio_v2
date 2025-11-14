#ifndef STATION_DATA_H
#define STATION_DATA_H

#include "audio_pipeline_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief Structure to define a radio station's properties.
     */
    typedef struct
    {
        const char* call_sign; // Station's call sign or name
        const char* city;      // Station's city
        const char* uri;       // Stream URI
        codec_type_t codec;    // Codec type for the stream
    } station_t;


    extern station_t radio_stations[];
    extern int station_count;

#ifdef __cplusplus
}
#endif

#endif // STATION_DATA_H