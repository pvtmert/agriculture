
#ifndef _DATA_H_
#define _DATA_H_

#include <rom/crc.h>
#include <Arduino.h>

#define DATA_CRC 0x4321

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

/*
typedef union {
	false,
	true,
} bool;
*/

typedef union DataHeader {
	struct DataHeaderContainer {
		unsigned long magic;
		unsigned short ver;
		union DataHeaderValues {
			struct DataHeaderV1 {
				unsigned long destination;
				unsigned long identifier;
				unsigned long timestamp;
				unsigned long checksum;
				unsigned long origin;
				unsigned char length;
				unsigned char ttl;
			} v1;
			struct DataHeaderV2 {
				unsigned long id;
				unsigned long ttl;
				unsigned long addr;
			} v2;
		} values;
	} container;
	unsigned char pad[48];
} __attribute__((packed)) data_header_t;

typedef union DataPayload {
	struct DataPayloadContainer {
		unsigned short ver;
		union DataPayloadValues {
			struct DataPayloadV1 {
				struct {
					union {
						struct {
							unsigned short _30;
							unsigned short _60;
							unsigned short _90;
						} by_length;
						unsigned short as_array[3];
					} moist;
					unsigned short temp;
					unsigned short ec;
				} by_name;
				unsigned short sensors[5];
			} v1;
			struct DataPayloadV2 {
				char message[30];
			} v2;
		} values;
	} container;
	unsigned char pad[32];
} __attribute__((packed)) data_payload_t;

typedef union DataPackage {
	struct DataPackageContainer {
		data_header_t  header;
		data_payload_t payload;
	} container;
	unsigned char pad[80];
} __attribute__((packed)) data_package_t;

int data_verify(data_package_t*);
unsigned long data_checksum(data_header_t*, data_payload_t*);
data_package_t make_data(
	unsigned long, unsigned long, unsigned long, unsigned char,
	unsigned short, unsigned short,  unsigned short, ...
);

/*
data_package_t data_make_auto(
	unsigned long, unsigned long, unsigned long, unsigned short,
	unsigned short, unsigned short,  unsigned short, ...
);
*/

#ifdef __cplusplus
}
#endif

#endif
