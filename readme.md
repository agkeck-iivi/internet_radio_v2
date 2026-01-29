# Internet radio v2

## hardware

* ESP32s3 DevkitC N16R8
* ES8388 audio board
  * both clock and data lines require low pass filters with caps close to es8388
* 2 rotary encoders with push buttons and hardware debounce
  * encoder 1: volume + mute + hold down at boot to enter provisioning mode
  * encoder 2: station selection + display ip address + reboot

* 1 IR LED
* 1 OLED display spi interface

### prototype

### header wiring

![internet radio version 2 header wiring](header_wiring.png)

![internet radio version 2 prototype](v2_prototype.jpg)

### schematic

![internet radio version 2 schematic](v2_schematic.png)

## software

### audio pipeline

The audio pipeline is virtually the same as in version 1.  We added an accumulator to count the bytes read from the http stream and a periodic task to calculate/update the bitrate display on the screen.  This task calculates a 10 second weighted average of one second bitrates.  When this weighted average is 0 we know that we have not received data for 10 seconds.  We use this signal along with a delay of 15 seconds to determine if we need to reboot the device.  If we have not received data for 10 seconds and we are at least 15 seconds since last boot we reboot the device.

### audio board

Version 1 used the LyraT sdkconfig option to identify the audio board.  In version 2 we attempt to create a custom audio board with only the necessary components.  This attempt is partially successful. We can initialize and utilize the board but there is still a lot of cruft in the custom board definition.  We will need to clean this up.

The es8388 driver that ships with esp-adf sets the gain on output 2 to -30 dB.  In order to use channel 2 I moved the driver into the es8388 board component and initialized the registers appropriately.  I think that the only linkage is through a global variable `MY_AUDIO_CODEC_ES8388_DEFAULT_HANDLE` defined in `es8388_driver.c`.  This variable contains pointers to functions that initilize and control the es8388 and is used to initialize the audio_hal.  We need to keep an eye out for other linkage since the old es8388 driver code is still in the path.

### encoders

The hardware pulse counters track the position of the encoders. This device has interupts for pulse thresholds but not for changes in pulse counts.  We use a polling task to check the pulse counts and update the encoder position.  For the volume encoder we clamp the value to the range [0, 100] and arrange the relationship between the pulse count and the [0, 100] range to saturate at the endpoints.  In this way, even if the user turns well past an endpoint the reverse movement will immediately affect the value.

### lvgl

LVGL is used for the display interface. Since LVGL is not thread-safe, we implement a **Queue-based Producer/Consumer pattern** to manage all UI updates safely.

#### Thread Safety & Queue

* **Producer**: Any task (e.g., Wi-Fi events, Encoder Logic) that wants to update the UI creates a `ui_update_message_t` struct. This struct contains an event `type` (enum) and optional `data` (int or string pointer). This message is sent to `g_ui_queue`.
* **Consumer**: The `process_ui_updates()` function runs in the main loop (or a dedicated UI task). It consumes messages from `g_ui_queue` and performs the actual LVGL API calls (e.g., `lv_label_set_text`, `lv_screen_load`). This ensures all LVGL operations happen in a single context.

#### Screens

The application uses three primary screens:

1. **Home Screen**: The main dashboard showing the Volume Slider, Station Call Sign, Origin, and Bitrate.
2. **Station Selection**: Displays a "Roller" widget allowing the user to scroll through the list of stations.
3. **Message Screen**: A generic, reusable screen with a centered label used for notifications (e.g., "Rebooting", "Provisioning", "IP Address").

#### Messaging

To display a message on the **Message Screen**:

1. **Fixed Messages**: Helper functions like `switch_to_reboot_screen()` send a message type (e.g., `SWITCH_TO_REBOOT_SCREEN`) that triggers the consumer to load the Message Screen and set a hardcoded string (e.g., "Rebooting").
2. **Arbitrary Messages**: To display dynamic text (like an IP address), the system uses the `UPDATE_IP_LABEL` message type.
    * The producer sends a message with `type = UPDATE_IP_LABEL` and `data.str_value = "My String"`.
    * The consumer reads specific string pointer and updates the label on the Message Screen.
    * *Note: Ensure the string pointer remains valid until processed, or use static/global buffers.*

### wifi

The device relies on Wi-Fi for internet connectivity. If no Wi-Fi credentials are provided (e.g., typically on the first boot), the device enters **Provisioning Mode**.

**Provisioning Mode**:

* The device broadcasts a Bluetooth LE (BLE) service.
* Use the **Espressif BLE Provisioning** app (available on iOS and Android) to connect to the device.
* Follow the app instructions to scan for Wi-Fi networks and provide the SSID and password.
* Once credentials are received, the device will connect to the Wi-Fi network and save the credentials to NVS for future boots.
* **Forced Reprovisioning**: To reset the Wi-Fi credentials and force the device back into provisioning mode, **press and hold the Volume Encoder button** while powering on (or rebooting) the device.

### ir

The Bose Wave radio uses an IR remote control for all functions. Version 2 is designed for this particular unit so we generate our own IR signals to control the radio.  After sniffing the IR signals with ir_nec_transciever project we see that the radio does not use the NEC protocal.  Using 10 samples of the on/off button and 10 samples of the aux button we create a concensus signal for each message. The colab: `bose_sig_analysis.ipynb` shows the analysis of the IR signals.

The bose IR protocal is stateful: the on/off button performs different functions based on the state of the radio.  The aux button is stateless, this signal will always result in the radio being on with input from the aux jack.  Therefore we use aux as the on signal and aux + on/off as the off signal.  We send aux during the boot sequence to ensure that the radio is on when we boot.  We expect the user to double click the volume encoder to turn the radio off.

### nvs

### station data

Initial station data is stored in a constant array.  On first boot, if the spiffs is not initialized we create a default station json file based on the constant array.  Thereafter, on boot the spiffs should be found and the json file serves as the source of truth for station data.  We provide an API to load the station data from the json file and save the station data to the json file.

```{bash}
curl -X POST -H "Content-Type: application/json" -d '[{"call_sign":"TestRadio","origin":"Home","uri":"http://test.com","codec":1}]' http://<ESP32_IP_ADDRESS>/api/stations
```

where the codec enum uses these values:
0: MP3
1: AAC
2: OGG
3: FLAC

### web update to station data

We provide a web interface to update the station data at <ESP32_IP_ADDRESS>/api/stations (or just <ESP_IP_ADDRESS> where there is a link to station data.)  From the web interface we can add, remove, and update station data as well a reorder the list of stations.  The station data is saved to the spiffs and a reboot will apply the changes.

## construction

Phase 1: Solder the esp32 and es8388 boards with low pass filters and pullup resistors for I2C bus to build the audio pipeline.  Connectors for the jumpers to controls, display, ir, audio, and power are shown but not yet finalized.
![internet radio version 2 construction phase 1](v2_audio_pipeline.jpg)

## operation

The radio's user interface is driven by two rotary encoders, each equipped with an integrated push button (switch).

### Control Summary Table

| Control | Action | Function |
| :--- | :--- | :--- |
| **Volume Encoder** | Rotation | Adjust volume (0-100); Auto-unmutes if turned |
| | Single Click | Toggle Mute/Unmute |
| | Double Click | Send **Bose ON/OFF** IR command |
| | **Hold at Boot** | **Force Reprovisioning** (Wipe Wi-Fi credentials) |
| **Station Encoder** | Rotation | Scroll through stations; Selects after 2s inactivity |
| | Short Press | Display **IP Address** (3 seconds) |
| | Long Press (>1.5s) | **Reboot** device |

---

### Detailed Usage

#### Volume Encoder (Left)

* **Rotation**: Sets the volume level between 0 and 100.
  * The volume is saved to NVS and persists across reboots.
  * If the device is muted, any rotation will automatically restore the previous volume plus or minus the rotation delta.
* **Single Click**: Toggles the mute state. Muting sets output to 0 while remembering the previous level for restoration.
* **Double Click**: Triggers the IR transmitter to send a power toggle command to the connected Bose system.
* **Boot Action**: Holding this button during power-on or reset will trigger `wifi_prov_mgr_reset_provisioning()`, allowing you to connect a new Wi-Fi network via BLE.

#### Station Encoder (Right)

* **Rotation**: Navigates the station list.
  * Turning the knob switches the display to the **Station Selection** screen.
  * **Auto-Selection**: Once you stop turning, the radio waits for **2 seconds** before committing the change, switching the stream, and returning to the Home screen.
* **Short Press**: Briefly switches the screen to show the current IP address. This is useful for accessing the web configuration interface.
* **Long Press**: Holding for more than 1.5 seconds triggers an immediate system reboot. A "Rebooting" message will appear on the display as confirmation.
