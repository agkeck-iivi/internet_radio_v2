
# How to find url/codec for  radio streams
## Identifying url

https://fmstream.org

## Identifying codec

```{bash}
ffprobe -v error -select_streams a:0 -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 <url>
```




## Measuring bitrate
```{bash}
ffprobe -v error -select_streams a:0 -show_entries stream=bit_rate -of default=noprint_wrappers=1:nokey=1 <url>
```

