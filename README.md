# internet radio v2

# changes to v1
* moved to esp32-s3 in order to access more IRAM, psram, and flash
* added wifi provisioning via BLE
* created custom board definition for es8388 board (v1 used existing Lyrat board definition which sufficed for the esp32 used there)
  

# es8388 board to esp32-s3 mapping

  
# loading a bunch of stations:
```{bash}
curl -X POST -H "Content-Type: application/json" -d '[{"call_sign":"TestRadio","origin":"Home","uri":"http://test.com","codec":1}]' http://<ESP32_IP_ADDRESS>/api/stations
'''