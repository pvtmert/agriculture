#!/usr/bin/env -S docker build --compress -t pvtmert/agriculture -f

FROM pvtmert/arduino:esp32

ENV LIBRARIES "LoRa ArduinoOTA"

RUN arduino-cli -v lib install $LIBRARIES
RUN arduino-cli -v lib install "ESP8266 and ESP32 OLED driver for SSD1306 displays"

WORKDIR /sketch/agriculture
COPY ./ ./
