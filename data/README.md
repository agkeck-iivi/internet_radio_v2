# Radio Station Data

This directory contains the radio station list used by the internet radio.

## File structure

### [stations.json](file:///home/agkeck/project/mc_projects/esp32/internet_radio_v2/data/stations.json)

The station list is an array of objects, where each object represents a radio station.

**Station Properties:**
- `call_sign`: The name or ID of the station.
- `origin`: The station's location or source.
- `uri`: The streaming URL.
- `codec`: The audio protocol/format used by the stream (see mapping below).

### Codec Mapping

The `codec` value in the JSON file maps to the following protocols:

| Value | Protocol |
|-------|----------|
| 0     | MP3      |
| 1     | AAC      |
| 2     | OGG      |
| 3     | FLAC     |

## Updating Stations via HTTP

You can update the station list remotely by sending a POST request to the radio's API.

### Using curl

From your terminal, run the following command (replace `<IP_ADDRESS>` with your radio's actual IP):

```bash
curl -X POST -H "Content-Type: application/json" --data-binary @stations.json http://<IP_ADDRESS>/api/stations
```

**Note:** The radio will automatically save the new station list to its internal flash memory (SPIFFS) upon a successful update.
