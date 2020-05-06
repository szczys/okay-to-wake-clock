# Okay to Wake Clock

An ESP8266-based light clock using WS2812 LEDs for:

* Red during sleep time (overnight)
* Yellow when wake time is approaching
* Green when it's time to get up
* Off during the day

## Implementation

This uses OTA updates so that it can be install inside of an existing okay to wake clock (robust case acts as diffuser, power suppy, all out of sight)
while still being possible to update

At power-up this will connect to WiFi, download time, and wait 10 minutes for an OTA update before turing WiFi off until next power cycle
