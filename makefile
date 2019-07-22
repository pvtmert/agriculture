#!/usr/bin/env make -f

#ARDUINO := open -Wnjgb cc.arduino.Arduino --args
ARDUINO := /Volumes/data/Applications/Arduino.app/Contents/MacOS/Arduino
BUILD   := $(wildcard /Volumes/tempramdisk*)/build
BOARD   := esp32:esp32:esp32thing
PORT    := /dev/cu.usbmodem002324124
BAUD    := 115200
ARGS    := --verbose --preserve-temp-files \
           --pref upload.speed=921600 \
           --pref build.path=$(BUILD) \
           --pref build.flash_freq=80m \

default: $(BUILD) u m
	# nothing

$(BUILD):
	mkdir -p $(BUILD)

wait:
	until test -e $(PORT); do echo "waiting for: $(PORT)"; sleep 1; done

c compile: $(BUILD)
	$(ARDUINO) $(ARGS) --port $(PORT) --board $(BOARD) --verify $(wildcard *.ino)

u install: $(BUILD) wait
	$(ARDUINO) $(ARGS) --port $(PORT) --board $(BOARD) --upload $(wildcard *.ino)

m monitor: wait
	trap 'kill %1' 0 1 2 3 6 9 15; cat $(PORT) & stty -f $(PORT) raw $(BAUD); wait

clean:
	rm -rf $(BUILD)
