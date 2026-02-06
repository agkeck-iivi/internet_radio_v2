# Station Metadata Research Report (Rev 3)

This report outlines the verified strategies for implementing "Now Playing" displays, incorporating user feedback and final validation.

## Executive Summary

We have moved away from the assumption of a single metadata standard. The 16 stations will be handled by three distinct "Metadata Drivers":

1. **KEXP Driver**: Uses the verified KEXP V2 JSON API.
2. **Spinitron Driver**: Scrapes public Spinitron pages. This is the **primary solution** for 6+ stations (KXLU, KSUT, KOTO, KDUR, KBUT, WMBR) that lack direct stream metadata.
3. **Standard Driver**: Uses `status-json.xsl` or in-stream ICY metadata for the remaining stations.

## Detailed Station Analysis

### 1. KEXP (Custom API)

* **Status**: **VERIFIED**
* **Endpoint**: `https://api.kexp.org/v2/plays/?format=json&limit=1`
* **Format**: JSON. High reliability.

### 2. Spinitron Stations

These stations rely on Spinitron for playlist management. We will scrape their public "current song" display.

* **Status**: **VERIFIED** (for most)
* **Target Pattern**: `https://spinitron.com/[STATION_NAME]/`
* **Parsing**: Find `data-current-song` attribute or `.current-song` class in HTML.

| Station | Verified URL | Notes |
| :--- | :--- | :--- |
| **KXLU** | `https://spinitron.com/KXLU/` | Confirmed reachable. |
| **KDUR** | `https://spinitron.com/KDUR/` | Confirmed by user. |
| **KOTO** | `https://spinitron.com/KOTO/` | Confirmed by user. |
| **KBUT** | `https://spinitron.com/KBUT/` | Confirmed reachable. |
| **WMBR** | `https://spinitron.com/WMBR/` | Confirmed reachable. |
| **KSUT** | *Dynamic / TBD* | Direct URL is elusive (likely `KSUTFM` or `Tribal`). **Strategy**: Scrape `https://www.ksut.org/current-playlist` to find the correct `spinitron.com` link dynamically at runtime or during config. |

### 3. StreamTheWorld & Generic

* **StreamTheWorld**: KBUT and KOTO are now handled by **Spinitron** (see above), solving the "stub" XML issue. KUFM likely follows this pattern or uses the standard driver.
* **Generic Stations** (WPRB, KFFP, KBOO, KALX, WFUV, KRCL): Continue to use `status-json.xsl`. If that fails, fallback to standard ICY Metadata (audio stream headers) which requires no extra HTTP calls.

## Implementation Plan

### 1. Data Structures

Update `station_config_t` to support the driver model:

```c
typedef enum {
    META_DRIVER_ICECAST_JSON,   // Default
    META_DRIVER_KEXP_V2,        // Custom
    META_DRIVER_SPINITRON,      // Scraper
} metadata_driver_t;

typedef struct {
    // ...
    metadata_driver_t meta_driver;
    char *meta_uri; // e.g. "https://spinitron.com/KXLU/"
} station_config_t;
```

### 2. Spinitron Scraper Logic

Since Spinitron pages are HTML, we need a lightweight scraper:
* HTTP GET the `meta_uri`.
* Search for specific tokens: `<tr class="current-song">` or `data-current-song="`.
* Extract text between tags.
* *Optimization*: Use `Range` headers to fetch only the first ~15KB of the page if the metadata is near the top.

### 3. Next Steps

1. Implement the **KEXP Driver** (Easiest, high value).
2. Implement the **Spinitron Driver** (Unlocks 6+ stations).
3. Refine **KSUT** handling during the Spinitron implementation phase.
