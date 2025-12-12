# Version 2 build details
![internet radio version 1 build](v1_build.jpg)
## hardware
* ESP32s3 DevkitC N16R8
* ES8388 audio board
  * both clock and data lines require low pass filters with caps close to es8388
* 2 rotary encoders with push buttons and hardware debounce
  * encoder 1: volume + mute
  * encoder 2: station selection + display ip address + reboot
* 2 pushbuttons
  * button 1: send bose aux signal on IR led
  * button 2: send bose aux followed by on/off signals on IR led
  * holding button 2 during boot enters provisioning mode

* 1 IR LED
* 1 OLED? display spi interface

![internet radio version 2 prototype](v2_prototype.jpg)

## software

### audio pipeline
The audio pipeline is virtually the same as in version 1.  We added an accumulator to count the bytes read from the http stream and a periodic task to calculate/update the bitrate display on the screen.  This task calculates a 10 second weighted average of one second bitrates.  When this weighted average is 0 we know that we have not received data for 10 seconds.  We use this signal along with a delay of 15 seconds to determine if we need to reboot the device.  If we have not received data for 10 seconds and we are at least 15 seconds since last boot we reboot the device.
### audio board
Version 1 used the LyraT sdkconfig option to identify the audio board.  In version 2 we attempt to create a custom audio board with only the necessary components.  This attempt is partially successful. We can initialize and utilize the board but there is still a lot of cruft in the custom board definition.  We will need to clean this up.
### encoders
The hardware pulse counters track the position of the encoders. This device has interupts for pulse thresholds but not for changes in pulse counts.  We use a polling task to check the pulse counts and update the encoder position.  For the volume encoder we clamp the value to the range [0, 100] and arrange the relationship between the pulse count and the [0, 100] range to saturate at the endpoints.  In this way, even if the user turns well past an endpoint the reverse movement will immediately affect the value.
### lvgl
### wifi
### ir
The Bose Wave radio uses an IR remote control for all functions. Version 2 is designed for this particular unit so we generate our own IR signals to control the radio.  After sniffing the IR signals with ir_nec_transciever project we see that the radio does not use the NEC protocal.  Using 10 samples of the on/off button and 10 samples of the aux button we create a concensus signal for each message. The colab: `bose_sig_analysis.ipynb` shows the analysis of the IR signals.

The bose IR protocal is stateful: the on/off button performs different functions based on the state of the radio.  The aux button is stateless, this signal will always result in the radio being on with input from the aux jack.  Therefore we use aux as the on signal and aux + on/off as the off signal.  We send aux during the boot sequence to ensure that the radio is on when we boot.  We expect the user to press the off button to turn the radio off.
### nvs
### station data
## construction
