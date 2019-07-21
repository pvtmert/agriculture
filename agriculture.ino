//

#include <ESP.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <LoRa.h>
#include <SSD1306.h>
#include <Preferences.h>

#include <assert.h>
#include <rom/crc.h>

//#include "font.h"

#include "data.h"

#define PWR_LORA    21
#define PWR_SENSOR1 19
#define PWR_SENSOR2 18
#define PWR_SENSOR3 39 // input only

#define LORA_SS   18 //23
#define LORA_RST  14 //25
#define LORA_DIO  26
#define LORA_SYNC 0xF3
#define LORA_FREQ 433E6

#define PIN_SENSOR0 0 // 35 // temp(air)
#define PIN_SENSOR1 0 // 27 // 30
#define PIN_SENSOR2 0 // 4  // 60
#define PIN_SENSOR3 0 // 5  // 90

#define PIN_SPI1 5
#define PIN_SPI2 19
#define PIN_SPI3 27
#define PIN_SPI4 18

typedef struct __attribute__((packed)) {
	size_t size;
	void *data;
	short rssi;
	float snr;
} lora_t;

const SPIClass hspi(HSPI);
const unsigned long device_identifier = (uint32_t) (ESP.getEfuseMac() >> 16);
static unsigned long packet_counter = 0;

void onReceive(int size) {
	debug("lora", "received: %d", size);
	if(size != sizeof(data_package_t)) {
		return;
	}
	data_package_t pkg;
	LoRa.readBytes((char*) &pkg, sizeof(pkg));
	debug("lora", "rssi: %d, snr: %f", LoRa.packetRssi(), LoRa.packetSnr());
	__hexdump(&pkg, sizeof(pkg));
	return;
}

void debug(const char *mod, const char *fmt, ...) {
	char *buffer = (char*) malloc(strlen(fmt) + 32);
	sprintf(buffer, "[ %8ld ] %8s | %s\r\n", millis(), mod, fmt, NULL);
	va_list args;
	va_start(args, fmt);
	char large[256];
	vsprintf(large, buffer, args);
	ets_printf(large);
	//vprintf(buffer, args);
	va_end(args);
	return;
}

void lora_send(void *data, size_t size) {
	LoRa.beginPacket();
	LoRa.write((unsigned char*) data, size);
	LoRa.endPacket(/*false*/);
	LoRa.receive();
	return;
}

/*
void init(unsigned long freq) {
	pinMode(26, INPUT);
	SPI.begin(5, 19, 27, 18);
	LoRa.crc();
	LoRa.setPins(18, 14, 26);
	LoRa.setSPIFrequency(8E6);
	LoRa.begin(freq * 1000000L);
	//LoRa.setTxPower(17, PA_OUTPUT_PA_BOOST_PIN);
	//LoRa.setSpreadingFactor(12);
	//LoRa.setSignalBandwidth(125E3);
	//LoRa.setCodingRate4(5);
	//LoRa.setSyncWord(0xF3);
	//LoRa.setPreambleLength(6);
	LoRa.dumpRegisters(Serial);
	LoRa.onReceive(onReceive);
	LoRa.receive();
	return delay(99);
}
*/

void setup(void) {
	delay(999);
	Serial.begin(115200);
	Serial.printf("# %16llx\r\n", device_identifier);

	pinMode(LORA_DIO, INPUT);
	pinMode(LORA_SS,     OUTPUT);
	pinMode(PWR_LORA,    OUTPUT);
	pinMode(PWR_SENSOR1, OUTPUT);
	pinMode(PWR_SENSOR2, OUTPUT);
	pinMode(PIN_SENSOR0, INPUT);
	pinMode(PIN_SENSOR1, INPUT);
	pinMode(PIN_SENSOR2, INPUT);
	pinMode(PIN_SENSOR3, INPUT);
	digitalWrite(PWR_LORA,    HIGH);
	digitalWrite(PWR_SENSOR1, HIGH);
	digitalWrite(PWR_SENSOR2, HIGH);

	//hspi.begin();
	SPI.begin(PIN_SPI1, PIN_SPI2, PIN_SPI3, PIN_SPI4);
	LoRa.crc();
	//LoRa.setSPI(hspi);
	//LoRa.setSPIFrequency(8E6);
	LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO);
	while(!LoRa.begin(LORA_FREQ)) {
		debug("lora", "trying...");
		delay(999);
	}
	LoRa.setSyncWord(LORA_SYNC);
	//LoRa.dumpRegisters(Serial);
	LoRa.onReceive(onReceive);
	LoRa.receive();
	return yield();
}

void __hexdump(void *data, size_t size) {
	static const int width = 8;
	for(int i=0; i<size; i++) {
		if(!(i%width)) Serial.printf("\r\n%8x |", i);
		Serial.printf(" % 2hhx", *(char*)(data+i));
		continue;
	}
	Serial.printf("\r\n");
	return;
}

void loop(void) {
	data_package_t pkg = make_data(
		++packet_counter, device_identifier,
		0xFFFFFFFFUL, 0xFFFFU,
		analogRead(PIN_SENSOR1),
		analogRead(PIN_SENSOR2),
		analogRead(PIN_SENSOR3),
		analogRead(PIN_SENSOR0),
		0U, NULL
	);
	debug("lora", "sending: %d", sizeof(pkg));
	__hexdump(&pkg, sizeof(pkg));
	lora_send(&pkg, sizeof(pkg));
	return delay(9999);
}
