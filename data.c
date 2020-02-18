
#include "data.h"

// payload

void
data_serialize_payload(data_payload_t *payload) {
	return;
}

void
data_serialize_header(data_header_t *header) {
	return;
}

void
data_serialize_package(data_package_t *package) {
	return;
}

data_payload_t*
data_make_payload_ptr(unsigned short _3, unsigned short _6, unsigned short _9,
	unsigned short t, unsigned short e
) {
	data_payload_t *payload = calloc(1, sizeof(data_payload_t));
	payload->container.values.v1.sensors[0] = _3;
	payload->container.values.v1.sensors[1] = _6;
	payload->container.values.v1.sensors[2] = _9;
	payload->container.values.v1.sensors[3] = t;
	payload->container.values.v1.sensors[4] = e;
	return payload;
}

unsigned long
data_checksum_payload(data_header_t *header, data_payload_t *payload) {
	unsigned long checksum = crc32_le(DATA_CRC, (void*) payload, sizeof(data_payload_t));
	if(header) {
		header->container.values.v1.checksum = checksum;
	}
	return checksum;
}

unsigned long
data_checksum_config(data_header_t *header, data_config_t *config) {
	unsigned long checksum = crc32_le(DATA_CRC, (void*) config, sizeof(data_config_t));
	if(header) {
		header->container.values.v1.checksum = checksum;
	}
	return checksum;
}

int
data_verify(data_package_t *package) {
	const unsigned long orig = package->container.header.container.values.v1.checksum;
	const unsigned long config = data_checksum_config(
		&(package->container.header),
		&(package->container.config)
	);
	const unsigned long payload = data_checksum_payload(
		&(package->container.header),
		&(package->container.payload)
	);
	package->container.header.container.values.v1.checksum = orig; // restore original
	return (orig == config) || (orig == payload);
}

//
data_header_t*
data_make_header_ptr(
	unsigned long dst,
	unsigned long id,
	unsigned long ts,
	unsigned long chksum,
	unsigned long origin,
	unsigned char length,
	unsigned char ttl,
	unsigned char netid,
	unsigned char flags,
	...
) {
	data_header_t *header = calloc(1, sizeof(data_header_t));
	header->container.values.v1.destination = dst;
	header->container.values.v1.identifier = id;
	header->container.values.v1.timestamp = ts;
	header->container.values.v1.checksum = chksum;
	header->container.values.v1.origin = origin;
	header->container.values.v1.length = length;
	header->container.values.v1.ttl = ttl;
	header->container.values.v1.netid = netid;
	header->container.values.v1.flags = flags;
	return header;
}

data_package_t*
data_make_package_ptr(data_header_t header, data_payload_t payload) {
	data_package_t *package = calloc(1, sizeof(data_package_t));
	package->container.payload = payload;
	package->container.header = header;
	package->container.header.container.values.v1.checksum = data_checksum_payload(&header, &payload);
	return package;
}

data_header_t
make_header(
	unsigned long id, unsigned long src, unsigned long dst,
	unsigned char ttl, unsigned char size, unsigned char flags,
	...
) {
	time_t now;
	time(&now);
	data_header_t header = {
		.container = {
			.magic = DATA_MAGIC,
			.ver = 0xABCD,
			.values = {
				.v1 = {
					.destination = dst,
					.identifier  = id,
					.timestamp   = now, // previously `millis()` issue: #1
					.checksum    = 0 & ~DATA_CRC,
					.origin      = src,
					.length      = size,
					.ttl         = ttl,
					.netid       = 0x99,
					.flags       = flags,
				},
				//.v2 = {
				//	.id   = 0x0,
				//	.ttl  = 0x0,
				//	.addr = 0x0,
				//},
			},
		},
	};
	return header;
}

data_payload_t
make_payload(
	unsigned short s1, unsigned short s2, unsigned short s3,
	unsigned short s4, unsigned short s5, unsigned short s6,
	...
) {
	data_payload_t payload = {
		.container = {
			.ver = 0xBEEF,
			.values = {
				.v1 = {
					.by_name = {
						.moist = {
							.by_length = {
								._30 = s1,
								._60 = s2,
								._90 = s3,
							},
						},
						.temp  = s4,
						.ec    = s5,
						.vp    = s6,
					},
				},
				//.v2 = {
				//	.message = "",
				//},
			},
		},
	};
	return payload;
}

data_config_t
make_config(
	unsigned long save, unsigned long mode,
	unsigned long mesh, unsigned long sleep,
	unsigned long timestamp, ...
) {
	data_config_t config = {
		.meta = {
			.ver = {
				.maj = DATA_CONFIG_VER_MAJOR,
				.min = DATA_CONFIG_VER_MINOR,
			},
		},
		.v1 = {
			.save  = save,
			.mode  = mode,
			.mesh  = mesh,
			.sleep = sleep,
			.timestamp = timestamp,
		},
	};
	return config;
}

data_package_t
make_package_wpayload(data_header_t header, data_payload_t payload, ...) {
	data_package_t package = {
		.container = {
			.header  = header,
			.payload = payload,
		},
	};
	data_checksum_payload(&(package.container.header), &(package.container.payload));
	return package;
}

data_package_t
make_package_wconfig(data_header_t header, data_config_t config, ...) {
	data_package_t package = {
		.container = {
			.header = header,
			.config = config,
		},
	};
	data_checksum_config(&(package.container.header), &(package.container.config));
	return package;
}
