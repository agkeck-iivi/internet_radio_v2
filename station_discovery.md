# Finding Radio Station URIs and Codecs

This guide explains how to find the streaming URL (URI) and format (Codec) for your favorite radio stations to use with the Internet Radio v2.

## For Novice Users: Using Radio Directories

The easiest way to find station information is to use curated directories that provide direct streaming links.

### 1. TuneIn (Web)

* Search for your station on [TuneIn](https://tunein.com/).
* While TuneIn often uses a web player, many stations listed there have official websites linked in their profile which provide "Direct Stream" links.

### 2. Radio-Browser.info

* Go to [Radio-Browser.info](https://www.radio-browser.info/).
* Search for the station name or call sign.
* The results will clearly show the **Stream URL** (URI) and the **Codec** (MP3, AAC, etc.).
* **Tip:** Look for URLs ending in `.mp3`, `.aac`, or `.pls`.

---

## For Advanced Users: Using Browser DevTools

If a station only provides a web player and doesn't list a direct link, you can find it using your browser's Inspect tool.

1. Open the station's web player in Chrome, Firefox, or Edge.
2. Right-click anywhere on the page and select **Inspect** (or press `F12`).
3. Go to the **Network** tab.
4. Type `media` or `audio` in the filter box.
5. Start the radio stream.
6. Look for a request that has a large "Time" or "Size" that keeps growing. This is the stream.
7. Right-click the name of that request and select **Copy > Copy link address**.
8. Check the **Headers** tab for `Content-Type`:
    * `audio/mpeg` = **MP3** (Codec 0)
    * `audio/aac` or `audio/aacp` = **AAC** (Codec 1)
    * `audio/ogg` = **OGG** (Codec 2)
    * `audio/flac` = **FLAC** (Codec 3)

---

## AI-Assisted Discovery (Gemini Prompt)

You can use an AI like Gemini to do the research for you. Use the following prompt template for the best results:

### Gemini Prompt Example
>
> "I need the direct streaming URI and the audio codec for the following radio station: **[STATION NAME / CALL SIGN]**.
>
> Please provide the response in this JSON format:
>
> ```json
> {
>   "call_sign": "[Call Sign]",
>   "origin": "[City, State/Country]",
>   "uri": "[Direct Streaming URL]",
>   "codec": [0 for MP3, 1 for AAC, 2 for OGG, 3 for FLAC]
> }
> ```
>
> Ensure the URI is a direct stream (not a website URL) and identify the correct codec integer based on the mapping provided."

---

## Codec Mapping Reference

| Value | Codec |
| :--- | :--- |
| **0** | MP3 |
| **1** | AAC |
| **2** | OGG |
| **3** | FLAC |
