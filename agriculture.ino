//

#include <ESP.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <LoRa.h>
#include <SSD1306.h>
#include <ESPmDNS.h>
#include <AsyncUDP.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

#include <assert.h>
#include <rom/crc.h>

//#include "font.h"

#include "debug.h"
#include "data.h"

#define PWR_LORA    21
#define PWR_SENSOR0 22
#define PWR_SENSOR1 19
#define PWR_SENSOR2 18
#define PWR_SENSOR3 39 // input only

#define LORA_SS   23 //18 //23
#define LORA_RST  25 //14 //25
#define LORA_DIO  26
#define LORA_SYNC 0xF3
#define LORA_FREQ 433E6

#define PIN_SENSOR1  27 // 27 // 30
#define PIN_SENSOR2   4 // 4  // 60
#define PIN_SENSOR3   5 // 5  // 90
#define PIN_SENSOR0  35 // 35 // temp(air)
#define PIN_SENSORVP 36

#define PIN_SPI1 //  5
#define PIN_SPI2 // 19
#define PIN_SPI3 // 27
#define PIN_SPI4 // 18

#define PIN_MASTER 0 // masterswitch

#define NET_IP_SRC WiFi.localIP()
#define NET_IP_DST IPAddress(224, 0, 0, 1)
#define NET_PORT   4321

#define SLEEP_TTL 10E6 // uS

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

typedef enum DeviceMode {
	DEVICE_MODE_NONE,
	DEVICE_MODE_MESH,
	DEVICE_MODE_SLAVE,
	DEVICE_MODE_MASTER,
	DEVICE_MODE_COUNT,
} device_mode_t;

static WiFiUDP udp;
static Preferences prefs;


static lora_pkg_t    lora_last_pkg = {
	.meta = {
		.data = {
			.parsed = true,
			.size   = 0,
		},
	},
};
static data_config_t cfg_runtime;
static volatile device_mode_t device_mode;
static volatile bool isr_state;

SPIClass hspi(HSPI);
const unsigned long device_identifier = (uint32_t) (ESP.getEfuseMac() >> 16);

RTC_DATA_ATTR static unsigned long packet_counter = 0UL;
RTC_DATA_ATTR unsigned long boot_count = 0UL;

void hexdump(void *data, size_t size, int width = 8) {
	for(int i=0; i<size; i++) {
		if(!(i%width)) MPRINTF("\r\n%8x |", i);
		MPRINTF(" % 2hhx", *(char*)(data + i));
		continue;
	}
	MPRINTF("\r\n");
	return;
}

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
	hexdump(&pkg.data.pkg, sizeof(pkg.data.pkg));
	if(true
		&& DATA_MAGIC == pkg.data.pkg.container.header.container.magic
		&& data_verify(&pkg.data.pkg)
		&& (false
			|| device_identifier == pkg.data.pkg.container.header.container.values.v1.destination
			|| 0xFFFFFFFFUL == pkg.data.pkg.container.header.container.values.v1.destination
			|| !pkg.data.pkg.container.header.container.values.v1.destination
		)
	) {
		lora_last_pkg = pkg; // copy back
	} else {
		debug("lora", "wrong checksum or target!");
	}
	return;
}

void lora_send(void *data, size_t size) {
	LoRa.beginPacket();
	LoRa.write((unsigned char*) data, size);
	LoRa.endPacket(/*false*/);
	LoRa.receive();
	return;
}

void mode_master() {
	char ssid[64];
	sprintf(ssid, "agr-%08x", device_identifier);
	WiFi.disconnect(true);
	WiFi.softAP(ssid);
	udp.begin(NET_PORT);
	ArduinoOTA.setPort(3232);
	ArduinoOTA.setHostname(ssid);
	ArduinoOTA.setPasswordHash("5d41402abc4b2a76b9719d911017c592");
	ArduinoOTA.begin();
	MDNS.addService("lora", "udp", NET_PORT);
	device_mode = DEVICE_MODE_MASTER;
	return yield();
}

void mode_slave() {
	data_package_t pkg = make_package_wpayload(
		make_header(
			++packet_counter, device_identifier,
			0xFFFFFFFFUL, 0xFF, sizeof(data_payload_t),
			DATA_HEADER_FLAG_NONE, NULL
		),
		make_payload(
			analogRead(PIN_SENSOR1),
			analogRead(PIN_SENSOR2),
			analogRead(PIN_SENSOR3),
			analogRead(PIN_SENSOR0),
			analogRead(PIN_SENSORVP),
			0xABCD, NULL
		),
		NULL
	);
	digitalWrite(PWR_SENSOR0, LOW);
	digitalWrite(PWR_SENSOR1, LOW);
	digitalWrite(PWR_SENSOR2, LOW);
	digitalWrite(PWR_SENSOR3, LOW);
	debug("lora", "sending: %d", sizeof(pkg));
	hexdump(&pkg, sizeof(pkg));
	lora_send(&pkg, sizeof(pkg));
	device_mode = DEVICE_MODE_SLAVE;
	return yield();
}

void IRAM_ATTR isr(void) {
	debug("core", "isr handling: %c", isr_state ? 'T' : 'F');
	while(DEVICE_MODE_MASTER == device_mode); // hacky restart
	isr_state = true;
	return;
}

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

void prefs_load() {
	prefs.begin("default");
	if(prefs.getULong("id") != device_identifier) {
		debug("pref", "identity did not match! using defaults");
		prefs.clear();
	}
	cfg_runtime.v1.save  = prefs.getULong ("save",  0x00000000);
	cfg_runtime.v1.mode  = prefs.getULong ("mode",  0x00000000);
	cfg_runtime.v1.mesh  = prefs.getULong ("mesh",  0x00000000);
	cfg_runtime.v1.sleep = prefs.getULong ("sleep", SLEEP_TTL);
	prefs.end();
	return;
}

void prefs_save() {
	prefs.begin("default");
	prefs.putULong ("id",    device_identifier   );
	prefs.putULong ("save",  cfg_runtime.v1.save );
	prefs.putULong ("mode",  cfg_runtime.v1.mode );
	prefs.putULong ("mesh",  cfg_runtime.v1.mesh );
	prefs.putULong ("sleep", cfg_runtime.v1.sleep);
	prefs.end();
	return;
}

void setup(void) {
	delay(999);
	Serial.begin(115200);
	MPRINTF("# %08llx\r\n", device_identifier);
	handle_wake(esp_sleep_get_wakeup_cause());
	pinMode(PIN_MASTER,   INPUT_PULLUP);
	pinMode(LORA_DIO,     INPUT);
	pinMode(LORA_SS,      OUTPUT);
	pinMode(PWR_LORA,     OUTPUT);
	pinMode(PWR_SENSOR0,  OUTPUT);
	pinMode(PWR_SENSOR1,  OUTPUT);
	pinMode(PWR_SENSOR2,  OUTPUT);
	pinMode(PWR_SENSOR3,  OUTPUT);
	//pinMode(PWR_SENSORVP, OUTPUT);
	pinMode(PIN_SENSOR0,  INPUT);
	pinMode(PIN_SENSOR1,  INPUT);
	pinMode(PIN_SENSOR2,  INPUT);
	pinMode(PIN_SENSOR3,  INPUT);
	pinMode(PIN_SENSORVP, INPUT);
	digitalWrite(PWR_LORA,    HIGH);
	digitalWrite(PWR_SENSOR0, HIGH);
	digitalWrite(PWR_SENSOR1, HIGH);
	digitalWrite(PWR_SENSOR2, HIGH);
	digitalWrite(PWR_SENSOR3, HIGH);
	//digitalWrite(PWR_SENSORVP, HIGH);
	attachInterrupt(PIN_MASTER, isr, RISING);
	prefs_load();
	hspi.begin();
	//SPI.begin(PIN_SPI1, PIN_SPI2, PIN_SPI3, PIN_SPI4);
	LoRa.crc();
	LoRa.setSPI(hspi);
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
	for(int i=0; i<4; i++) {
		debug("core", "waiting for masterswitch");
		if(isr_state || !digitalRead(PIN_MASTER)) {
			debug("core", "masterswitch found!");
			while(!digitalRead(PIN_MASTER)) yield();
			return mode_master();
		}
		delay(333);
		continue;
	}
	// slave mode begins here
	debug("core", "thats slavery with extra steps");
	return mode_slave();
}

void udp_receive_handler(const int size) {
	if(!size) {
		return;
	}
	if(sizeof(data_config_t) != size) {
		debug("udp", "wrong size to publish!");
		udp.flush();
		return;
	}
	debug("udp", "size:%d avail:%d", size, udp.available());
	data_config_t config;
	udp.read((char*) &config, sizeof(config));
	unsigned long target = config.target;
	config.target = 0;
	data_package_t pkg = make_package_wconfig(
		make_header(
			++packet_counter, device_identifier,
			target, 0xFF, sizeof(config),
			DATA_HEADER_FLAG_ALL, NULL
		), config
	);
	lora_send(&pkg, sizeof(pkg));
	return;
}

void udp_send(IPAddress dst, uint16_t port, void *data, size_t size) {
	udp.beginPacket(NET_IP_DST, NET_PORT);
	udp.write((unsigned char*) data, size);
	udp.endPacket();
	return;
}

void loop(void) {
	ArduinoOTA.handle();
	if(isr_state) {
		isr_state = false;
	}
	if(DEVICE_MODE_SLAVE == device_mode) {
		delay(1111);
		if(true
			&&  lora_last_pkg.meta.data.size
			&& !lora_last_pkg.meta.data.parsed
			&& !lora_last_pkg.data.pkg.container.config.ver
			&& (DATA_HEADER_FLAG_ALL == lora_last_pkg.data.
				pkg.container.header.container.values.v1.flags
			)
		) {
			debug("core", "saving settings...");
			cfg_runtime = lora_last_pkg.data.pkg.container.config;
			lora_last_pkg.meta.data.parsed = true;
			prefs_save();
		}
		debug("core", "sleeping... (%lu)", cfg_runtime.v1.sleep);
		esp_sleep_enable_timer_wakeup(cfg_runtime.v1.sleep);
		esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
		esp_deep_sleep_start();
		return yield();
	}
	udp_receive_handler(udp.parsePacket());
	if(lora_last_pkg.meta.data.parsed) {
		return yield();
	}
	udp_send(NET_IP_DST, NET_PORT,  &lora_last_pkg, sizeof(lora_last_pkg));
	lora_last_pkg.meta.data.parsed = true;
	return yield();
}
