
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
data_make_payload(unsigned short _3, unsigned short _6, unsigned short _9,
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
data_checksum(data_header_t *header, data_payload_t *payload) {
	unsigned long checksum = crc32_le(DATA_CRC, (void*) payload, sizeof(data_payload_t));
	if(header) {
		header->container.values.v1.checksum = checksum;
	}
	return checksum;
}

int
data_verify(data_package_t *package) {
	unsigned long orig = package->container.header.container.values.v1.checksum;
	unsigned long calc = data_checksum(
		&(package->container.header),
		&(package->container.payload)
	);
	package->container.header.container.values.v1.checksum = orig; // restore original
	return (calc == orig);
}

//
data_header_t*
data_make_header(
	unsigned long dst,
	unsigned long id,
	unsigned long ts,
	unsigned long chksum,
	unsigned long origin,
	unsigned char length,
	unsigned char ttl,
	unsigned char netid,
	data_header_flag_t flags,
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
data_make_package(data_header_t header, data_payload_t payload) {
	data_package_t *package = calloc(1, sizeof(data_package_t));
	package->container.payload = payload;
	package->container.header = header;
	package->container.header.container.values.v1.checksum = data_checksum(&header, &payload);
	return package;
}

data_package_t
make_data(
	unsigned long id, unsigned long src, unsigned long dst, unsigned char ttl,
	unsigned short s1, unsigned short s2,  unsigned short s3, ...
) {
	data_header_t header = {
		//{ .pad = "" },
		.container = {
			.magic = 0x12345678,
			.ver = 0x1234,
			.values = {
				.v1 = {
					.destination = dst,
					.identifier  = id,
					.timestamp   = millis(),
					.checksum    = 0 & ~DATA_CRC,
					.origin      = src,
					.length      = sizeof(data_payload_t),
					.ttl         = ttl,
					.netid       = 0xFF,
					.flags       = {
						.data = DATA_HEADER_FLAG_NONE,
					},
				},
				//.v2 = {
				//	.id   = 0x0,
				//	.ttl  = 0x0,
				//	.addr = 0x0,
				//},
			},
		},
	};
	data_payload_t payload = {
		//{ .pad = "" },
		.container = {
			.ver = 0x1234,
			.values = {
				.v1 = {
					.by_name = {
						.moist = s1,
						.temp  = s2,
						.ec    = s3,
					},
				},
				//.v2 = {
				//	.message = "",
				//},
			},
		},
	};
	data_package_t package = {
		//{ .pad = "" },
		.container = {
			.header = header,
			.payload = payload,
		},
	};
	data_checksum(&(package.container.header), &(package.container.payload));
	return package;
}
