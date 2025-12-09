#include "station_data.h"

station_t radio_stations[] = {
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
    {"KXLU", "Loyola Marymount", "http://kxlu.streamguys1.com:80/kxlu-lo",
     CODEC_TYPE_AAC},
    {"WPRB", "Princeton", "https://wprb.streamguys1.com/listen.mp3",
     CODEC_TYPE_AAC},
    {"WMBR", "MIT", "https://wmbr.org:8002/hi", CODEC_TYPE_MP3},
    {"KALX", "Berkeley", "https://stream.kalx.berkeley.edu:8443/kalx-128.mp3",
     CODEC_TYPE_MP3},
    {"WFUV", "Fordham", "https://onair.wfuv.org/onair-hi", CODEC_TYPE_MP3},
    {"KUFM", "Missoula",
     "https://playerservices.streamtheworld.com/api/livestream-redirect/KUFMFM.mp3",
     CODEC_TYPE_MP3},
    {"KRCL", "Salt Lake City", "http://stream.xmission.com:8000/krcl-low",
     CODEC_TYPE_AAC},
    // {"KRRC", "Reed College", "https://stream.radiojar.com/3wg5hpdkfkeuv",
    // CODEC_TYPE_MP3}
    // {"inet", "Radio Paradise", "http://stream.radioparadise.com/flac",
    // CODEC_TYPE_FLAC},  // works (poorly) after xxx seconds
    // {"test", "netherlands",
    // "http://stream.haarlem105.nl:8000/haarlem105DAB.flac", CODEC_TYPE_FLAC},
};

int station_count = sizeof(radio_stations) / sizeof(radio_stations[0]);