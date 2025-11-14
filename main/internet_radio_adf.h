#ifndef INTERNET_RADIO_ADF_H
#define INTERNET_RADIO_ADF_H

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief Changes the radio station, handling pipeline destruction and creation.
     * @param new_station_index The index of the new station to play.
     */
    void change_station(int new_station_index);

#endif // INTERNET_RADIO_ADF_H