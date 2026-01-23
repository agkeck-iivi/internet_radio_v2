
# How to find url/codec for  radio streams, current song, and bitrate

## Identifying url

<https://fmstream.org> or ask gemini

## Identifying codec

```{bash}
ffprobe -v error -select_streams a:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 <url>
```

## Measuring bitrate

```{bash}
ffprobe -v error -select_streams a:0 -show_entries stream=bit_rate -of default=noprint_wrappers=1:nokey=1 <url>
```

## getting currently playing song

### kexp

```{bash}
curl -s "https://api.kexp.org/v2/plays/?limit=1" | jq '.results[0] | {artist, song, album}'
```
