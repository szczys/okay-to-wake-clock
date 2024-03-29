# Deprecated

Active development has migrated this to a PlatformIO-based development environment. Please see the [Okay to Wake Clock 2 repo](https://github.com/szczys/okay-to-wake-clock-2).

# Okay to Wake Clock

An ESP8266-based light clock using WS2812 LEDs for:

* Red during sleep time (overnight)
* Blue when wake time is approaching
* Green when it's time to get up
* Off during the day

Project log: https://hackaday.io/project/171671-improved-okay-to-wake-clock

## Implementation

This uses OTA updates so that it can be install inside of an existing okay to wake clock (robust case acts as diffuser, power suppy, all out of sight)
while still being possible to update

At power-up this will connect to WiFi, download time, and wait 10 minutes for an OTA update before turing WiFi off until next power cycle

## Power Considerations

I've measured the following power usage:

* LEDs on (yellow) and WiFi active: 120 mA
* LEDs off but WiFi active: 74 mA
* LEDs and WiFi off: 18 mA

Much of its life this will sit idle so it would be nice to implement further power-saving while anticipating a scheduled event. This is a task saved for future development.
