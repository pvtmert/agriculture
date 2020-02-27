//

//#include <ESP.h> // is no more
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
#include <sys/time.h>

#define DEVICE_TYPE_HELTEC 0x101
#define DEVICE_TYPE_VSPI 0x991
#define DEVICE_TYPE_HSPI 0x992

//#include "font.h"

#define DEVICE DEVICE_TYPE_HELTEC
#ifndef DEVICE
#error CHANGE YOUR DEVICE TYPE ABOVE !
#endif

#include "debug.h"
#include "data.h"

#define PWR_LORA    21
#define PWR_SENSOR0 22
#define PWR_SENSOR1 19
#define PWR_SENSOR2 18
#define PWR_SENSOR3 39 // input only

#if DEVICE == DEVICE_TYPE_HELTEC
#define LORA_SS  18
#define LORA_RST 14
#endif

#if DEVICE == DEVICE_TYPE_VSPI
SPIClass vspi(VSPI);
#define LORA_SPI vspi
#endif

#if DEVICE == DEVICE_TYPE_HSPI
#define LORA_SS  23
#define LORA_RST 25
SPIClass hspi(HSPI);
#define LORA_SPI hspi
#endif

#define LORA_DIO  26
#define LORA_SYNC 0xF3
#define LORA_FREQ 433E6

#define PIN_SENSOR1  27 // 27 // 30
#define PIN_SENSOR2   4 // 4  // 60
#define PIN_SENSOR3   5 // 5  // 90
#define PIN_SENSOR0  35 // 35 // temp(air)
#define PIN_SENSORVP 36

// #define PIN_SPI1 //  5
// #define PIN_SPI2 // 19
// #define PIN_SPI3 // 27
// #define PIN_SPI4 // 18 // LORA_SS

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
			long freq;
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


static lora_pkg_t lora_last_pkg = {
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
				.freq   = (long) LoRa.packetFrequencyError(),
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
			|| 0xFFFFFFFFUL      == pkg.data.pkg.container.header.container.values.v1.destination
			|| !pkg.data.pkg.container.header.container.values.v1.destination // TODO: .target ?
		)
		&& DATA_CONFIG_VER_MAJOR == pkg.data.pkg.container.config.meta.ver.maj
		&& DATA_CONFIG_VER_MINOR == pkg.data.pkg.container.config.meta.ver.min
	) {
		lora_last_pkg = pkg; // copy back
	} else {
		debug("lora", "wrong checksum / version / destination!");
	}
	return;
}

void lora_send(void *data, size_t size) {
	while(!LoRa.beginPacket(false)) {
		debug("lora/send", "waiting for radio...");
		delay(99);
		continue;
	}
	LoRa.write((unsigned char*) data, size);
	LoRa.endPacket(/*false*/);
	LoRa.receive();
	return;
}

void mode_master(const char *clssid) {
	char apssid[64];
	sprintf(apssid, "agr-%08x", device_identifier);
	WiFi.disconnect(false, true);
	WiFi.mode(WIFI_AP_STA);
	//WiFi.enableAP(true);
	WiFi.softAP(apssid);
	udp.begin(NET_PORT);
	ArduinoOTA.setPort(23232);
	ArduinoOTA.setHostname(apssid);
	ArduinoOTA.setPasswordHash("5d41402abc4b2a76b9719d911017c592"); // 'hello'
	ArduinoOTA
		.onStart([]() {
			String type;
			if (ArduinoOTA.getCommand() == U_FLASH)
				type = "sketch";
			else // U_SPIFFS
				type = "filesystem";
			Serial.println("Start updating " + type);
		})
		.onEnd([]() {
			Serial.println("\nEnd");
		})
		.onProgress([](unsigned int progress, unsigned int total) {
			Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
		})
		.onError([](ota_error_t error) {
			Serial.printf("Error[%u]: ", error);
			switch(error) {
				case OTA_AUTH_ERROR:
					Serial.println("Auth Failed");
					break;
				case OTA_BEGIN_ERROR:
					Serial.println("Begin Failed");
					break;
				case OTA_CONNECT_ERROR:
					Serial.println("Connect Failed");
					break;
				case OTA_RECEIVE_ERROR:
					Serial.println("Receive Failed");
					break;
				case OTA_END_ERROR:
					Serial.println("End Failed");
					break;
			}
		});
	ArduinoOTA.begin();
	MDNS.addService("lora", "udp", NET_PORT);
	device_mode = DEVICE_MODE_MASTER;
	if(clssid) {
		//WiFi.enableSTA(true);
		WiFi.begin(" WiSpotter");
		WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
			udp.begin(NET_PORT);
		}, WiFiEvent_t::SYSTEM_EVENT_STA_GOT_IP);
		while(WiFi.status() != WL_CONNECTED) {
			delay(99);
			yield();
			continue;
		}
	}
	return yield();
}

void mode_slave(void) {
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
	//LoRa.dumpRegisters(Serial);
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

void prefs_load(void) {
	prefs.begin("default");
	if(prefs.getULong("id") != device_identifier) {
		debug("pref", "identity did not match! using defaults");
		prefs.clear();
	}
	cfg_runtime.v1.save      = prefs.getULong ("save",      0x00000000);
	cfg_runtime.v1.mode      = prefs.getULong ("mode",      0x00000000);
	cfg_runtime.v1.mesh      = prefs.getULong ("mesh",      0x00000000);
	cfg_runtime.v1.sleep     = prefs.getULong ("sleep",     SLEEP_TTL );
	cfg_runtime.v1.timestamp = prefs.getULong ("timestamp", 1262304000); // 1.1.2010 issue: #1
	prefs.end();
	return;
}

void prefs_save(void) {
	prefs.begin("default");
	prefs.putULong ("id",        device_identifier        );
	prefs.putULong ("save",      cfg_runtime.v1.save      );
	prefs.putULong ("mode",      cfg_runtime.v1.mode      );
	prefs.putULong ("mesh",      cfg_runtime.v1.mesh      );
	prefs.putULong ("sleep",     cfg_runtime.v1.sleep     );
	prefs.putULong ("timestamp", cfg_runtime.v1.timestamp );
	prefs.end();
	return;
}

void lora_tx_handler(void) {
	debug("lora", "tx is done!");
	return;
}

void setup(void) {
	WiFi.mode(WIFI_OFF);
	Serial.begin(115200);
	MPRINTF("# %08llx\r\n", device_identifier);
	handle_wake(esp_sleep_get_wakeup_cause());
	pinMode(PIN_MASTER,   INPUT_PULLUP);
	#if DEVICE != DEVICE_TYPE_HELTEC
	pinMode(LORA_DIO,     INPUT );
	pinMode(LORA_SS,      OUTPUT);
	pinMode(PWR_LORA,     OUTPUT);
	#endif
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
	#if DEVICE == DEVICE_TYPE_HELTEC
	digitalWrite(PWR_LORA, HIGH); // was low
	#else
	digitalWrite(PWR_LORA, HIGH);
	#endif
	digitalWrite(PWR_SENSOR0, HIGH);
	digitalWrite(PWR_SENSOR1, HIGH);
	digitalWrite(PWR_SENSOR2, HIGH);
	digitalWrite(PWR_SENSOR3, HIGH);
	//digitalWrite(PWR_SENSORVP, HIGH);
	attachInterrupt(PIN_MASTER, isr, RISING);
	prefs_load();
	#ifdef LORA_SPI
	LORA_SPI.begin();
	LoRa.setSPI(LORA_SPI);
	#else
	SPI.begin(5, 19, 27, LORA_SS);
	debug("lora", "default spi mode (heltec) is used!");
	//SPI.begin(PIN_SPI1, PIN_SPI2, PIN_SPI3, PIN_SPI4);
	#endif
	//LoRa.setSPIFrequency(8E6);
	LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO);
	while(!LoRa.begin(LORA_FREQ)) {
		if(isr_state) {
			debug("lora", "skipping radio, going into emergency mode!");
			return mode_master(" WiSpotter");
			break;
		}
		debug("lora", ("trying to enable (lora) radio..."));
		delay(999);
		continue;
	}
	/*
	LoRa.enableCrc();
	LoRa.setTxPower(17, PA_OUTPUT_PA_BOOST_PIN);
	LoRa.setCodingRate4(5);
	LoRa.setPreambleLength(8);
	LoRa.setSpreadingFactor(7);
	LoRa.setSignalBandwidth(62.5E3);
	LoRa.setSyncWord(LORA_SYNC);
	//LoRa.dumpRegisters(Serial);
	*/
	//LoRa.onTxDone(lora_tx_handler);
	LoRa.onReceive(lora_receive_handler);
	LoRa.receive();
	for(int i=0; i<4; i++) {
		debug("core", "waiting for masterswitch");
		if(isr_state || !digitalRead(PIN_MASTER)) {
			debug("core", "masterswitch found!");
			while(!digitalRead(PIN_MASTER)) yield();
			return mode_master(NULL);
		}
		delay(333);
		continue;
	}
	// slave mode begins here
	debug("core", "thats slavery with extra steps");
	return mode_slave();
}

/*
	Receives message from udp sized `size`,
	Checks if it matches with `data_config_t`.
	If it does, passes through lora. Wrapping with headers.
*/
void udp_receive_handler(const int size) {
	if(!size) return;
	if(sizeof(data_config_t) != size) {
		debug("udp", "wrong size to publish!");
		//udp.flush();
		return;
	}
	debug("udp", "size:%d avail:%d", size, udp.available());
	data_config_t config;
	udp.read((char*) &config, sizeof(config));
	unsigned long target = config.meta.target;
	config.meta.target = 0;
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

void loop_operation_slave(unsigned initial_delay=1111) {
	delay(initial_delay);
	if(true
		&&  lora_last_pkg.meta.data.size
		&& !lora_last_pkg.meta.data.parsed
		&& !lora_last_pkg.data.pkg.container.config.meta.ver.maj
		&& (DATA_HEADER_FLAG_ALL == lora_last_pkg.data.
			pkg.container.header.container.values.v1.flags
		)
	) {
		lora_last_pkg.meta.data.parsed = true;
		cfg_runtime = lora_last_pkg.data.pkg.container.config;
		if(cfg_runtime.v1.timestamp > 0) {
			struct timeval tv = {
				.tv_sec  = cfg_runtime.v1.timestamp,
				.tv_usec = 0,
			};
			settimeofday(&tv, NULL);
		}
		debug("core", "saving settings...");
		prefs_save();
	}
	debug("core", "sleeping... (%lu)", cfg_runtime.v1.sleep);
	esp_sleep_enable_timer_wakeup(cfg_runtime.v1.sleep);
	esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
	esp_deep_sleep_start();
	return yield();
}

void loop_operation_master(void) {
	udp_receive_handler(udp.parsePacket());
	if(lora_last_pkg.meta.data.parsed) {
		return yield();
	}
	udp_send(NET_IP_DST, NET_PORT,  &lora_last_pkg, sizeof(lora_last_pkg));
	lora_last_pkg.meta.data.parsed = true;
	return yield();
}

void loop(void) {
	ArduinoOTA.handle();
	if(isr_state) {
		isr_state = false;
	}
	switch(device_mode) {
		case DEVICE_MODE_SLAVE:
			loop_operation_slave();
			break;
		case DEVICE_MODE_MASTER:
			loop_operation_master();
			break;
		default:
			printf("Unsupported Mode Error !Panic!"
				"(%s: %s: %d)",
				__FILE__,
				__func__,
				__LINE__,
				NULL
			);
			break;
	}
	return yield();
}
