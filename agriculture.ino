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

#define PIN_MASTER 0 // masterswitch

#define NET_IP_SRC WiFi.localIP()
#define NET_IP_DST IPAddress(224, 0, 0, 1)
#define NET_PORT   4321

#define SLEEP_TTL 10E6 // uS
#define MPRINTF Serial.printf

typedef struct {
	union {
		struct {
			bool parsed;
			size_t size;
			short rssi;
			float snr;
		} data;
		unsigned char pad[16];
	} meta;
	union {
		data_package_t pkg;
		void *ptr;
	} data;
} __attribute__((packed)) lora_pkg_t;

static WiFiUDP udp;
static bool isr_state;
static lora_pkg_t lora_last_pkg;
const SPIClass hspi(HSPI);
const unsigned long device_identifier = (uint32_t) (ESP.getEfuseMac() >> 16);
static unsigned long packet_counter = 0;
RTC_DATA_ATTR unsigned long boot_count = 0UL;

void lora_receive_handler(int size) {
	lora_pkg_t pkg = {
		.meta = {
			.data = {
				.parsed = false,
				.size   = (unsigned) size,
				.rssi   = (short) LoRa.packetRssi(),
				.snr    = (float) LoRa.packetSnr(),
			},
		},
	};
	debug("lora", "received: %d (rssi:%d snr:%f)", size, pkg.meta.data.rssi, pkg.meta.data.snr);
	if(size != sizeof(pkg.data.pkg)) {
		debug("lora", "size mismatch: actual:%d, expected:%d", size, sizeof(pkg.data.pkg));
		return;
	}
	LoRa.readBytes((char*) &(pkg.data.pkg), sizeof(pkg.data.pkg));
	//__hexdump(&pkg.data.pkg, sizeof(pkg.data.pkg));
	lora_last_pkg = pkg; // copy back
	return;
}

void debug(const char *mod, const char *fmt, ...) {
	char *buffer = (char*) malloc(strlen(fmt) + 32);
	sprintf(buffer, "[ %8ld ] %8s | %s\r\n", millis(), mod, fmt, NULL);
	va_list args;
	va_start(args, fmt);
	char large[256];
	vsprintf(large, buffer, args);
	MPRINTF(large);
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

void handle_wake(esp_sleep_wakeup_cause_t cause) {
	boot_count += 1;
	switch(cause) {
		case ESP_SLEEP_WAKEUP_EXT0:
			break;
		case ESP_SLEEP_WAKEUP_EXT1:
			break;
		case ESP_SLEEP_WAKEUP_TIMER:
			break;
		case ESP_SLEEP_WAKEUP_TOUCHPAD:
			break;
		case ESP_SLEEP_WAKEUP_ULP:
			break;
		default:
			break;
	}
	return;
}

void mode_master() {
	char ssid[64];
	sprintf(ssid, "agr-%08x", device_identifier);
	WiFi.disconnect(true);
	WiFi.softAP(ssid);
	udp.begin(NET_IP_SRC, NET_PORT);
	return yield();
}

void mode_slave() {
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
	debug("core", "sleeping... (%d)", (SLEEP_TTL/1E3));
	esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
	esp_sleep_enable_timer_wakeup(SLEEP_TTL);
	esp_deep_sleep_start();
	return yield();
}

void IRAM_ATTR isr(void) {
	MPRINTF("isr handled");
	while(isr_state); // hacky restart
	isr_state = 0x1;
	return;
}

void setup(void) {
	delay(999);
	Serial.begin(115200);
	MPRINTF("# %16llx\r\n", device_identifier);
	handle_wake(esp_sleep_get_wakeup_cause());
	pinMode(PIN_MASTER,  INPUT_PULLUP);
	pinMode(LORA_DIO,    INPUT);
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
	attachInterrupt(PIN_MASTER, isr, RISING);
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
	LoRa.onReceive(lora_receive_handler);
	LoRa.receive();
	for(int i=0; i<10; i++) {
		debug("core", "waiting for masterswitch");
		if(isr_state || !digitalRead(PIN_MASTER)) {
			debug("core", "masterswitch found!");
			return mode_master();
		}
		delay(333);
		continue;
	}
	// slave mode begins here
	debug("core", "thats slavery with extra steps");
	return mode_slave();
}

void __hexdump(void *data, size_t size) {
	static const int width = 8;
	for(int i=0; i<size; i++) {
		if(!(i%width)) MPRINTF("\r\n%8x |", i);
		MPRINTF(" % 2hhx", *(char*)(data+i));
		continue;
	}
	MPRINTF("\r\n");
	return;
}

void loop(void) {
	if(lora_last_pkg.meta.data.parsed) {
		return yield();
	}
	udp.beginPacket(NET_IP_DST, NET_PORT);
	udp.write((unsigned char*) &lora_last_pkg, sizeof(lora_last_pkg));
	udp.endPacket();
	lora_last_pkg.meta.data.parsed = true;
	return yield();
}
