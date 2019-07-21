
#ifndef _DATA_H_
#define _DATA_H_

#include <rom/crc.h>
#include <Arduino.h>

#define DATA_CRC   0x4321
#define DATA_MAGIC 0x12345678

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
typedef enum {
	false,
	true,
} bool;
*/

typedef struct DataConfig {
	union {
		struct {
			unsigned short ver;
			unsigned short sub;
		};
		unsigned long target;
	};
	struct DataConfigV1 {
		unsigned long save;
		unsigned long mode;
		unsigned long mesh;
		unsigned long sleep;
		union {
			struct {
				unsigned long res1;
				unsigned long res2;
				unsigned long res3;
			} values;
			unsigned long array[3];
		} reserved;
	} v1;
} __attribute__((packed)) data_config_t;

typedef enum DataHeaderFlags {
	DATA_HEADER_FLAG_NONE = 0x00,
	DATA_HEADER_FLAG_A    = (1 << 0),
	DATA_HEADER_FLAG_B    = (1 << 1),
	DATA_HEADER_FLAG_C    = (1 << 2),
	DATA_HEADER_FLAG_D    = (1 << 3),
	DATA_HEADER_FLAG_E    = (1 << 4),
	DATA_HEADER_FLAG_F    = (1 << 5),
	DATA_HEADER_FLAG_G    = (1 << 6),
	DATA_HEADER_FLAG_H    = (1 << 7),
	DATA_HEADER_FLAG_ALL  = 0xFF,
	DATA_HEADER_FLAG_CNT  = 8,
} data_header_flag_e;

typedef union DataHeaderFlag {
	struct DataHeaderFlagBit {
		unsigned flag_a: 1;
		unsigned flag_b: 1;
		unsigned flag_c: 1;
		unsigned flag_d: 1;
		unsigned flag_e: 1;
		unsigned flag_f: 1;
		unsigned flag_g: 1;
		unsigned flag_h: 1;
	} bit;
	unsigned char data;
} data_header_flag_t;

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
				unsigned char netid;
				unsigned char flags;
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
		data_header_t header;
		union {
			data_payload_t payload;
			data_config_t config;
			unsigned char pad[32];
		};
	} container;
	unsigned char pad[80];
} __attribute__((packed)) data_package_t;

int data_verify(data_package_t*);
unsigned long data_checksum_config(data_header_t*, data_config_t*);
unsigned long data_checksum_payload(data_header_t*, data_payload_t*);
/*
data_package_t make_data(
	unsigned long id, unsigned long src, unsigned long dst, unsigned char ttl,
	unsigned short s1, unsigned short s2,  unsigned short s3, unsigned short s4,
	...
);
*/

data_header_t make_header(
	unsigned long id, unsigned long src, unsigned long dst,
	unsigned char ttl, unsigned char size, unsigned char flags,
	...
);
data_payload_t make_payload(unsigned short s1, unsigned short s2,  unsigned short s3, unsigned short s4, ...);
data_config_t make_config(unsigned long save, unsigned long mode, unsigned long mesh, unsigned long sleep, ...);
data_package_t make_package_wpayload(data_header_t header, data_payload_t payload, ...);
data_package_t make_package_wconfig(data_header_t header, data_config_t config, ...);

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
