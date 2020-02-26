#!/usr/bin/env python3

import os, sys, time, socket, struct, json, random, hashlib, binascii

from listen import lora, meta, header, payload, hexdump
from listen import lora_meta, lora_header, lora_payload, config

ADDR = "224.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 4321
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, True)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 32)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, True)

counter = 1

while True:
	data = bytearray(96)
	lora_meta.pack_into(data, 0x00, *meta(
		parse=0,
		size=80,
		rssi=random.randint(-160, -30),
		snr=0.0,
	))
	lora_header.pack_into(data, 0x10, *header(
		magic=0x12345678,
		ver=0xABCD,
		res=0x0000,
		dst=0x30e543a4,
		id=counter,
		ts=int(time.time()),
		cs=0x11223344,
		src=0xFFFFFFFF,
		size=233,
		ttl=0x80,
		net=0x20,
		flags=0xFF,
	))
	lora_payload.pack_into(data, 0x40, *payload(
		ver=0xBEEF,
		s30=random.randint(10, 30),
		s60=random.randint(30, 60),
		s90=random.randint(60, 90),
		temp=random.randint(0, 100),
		ec=random.randint(100, 200),
		vp=random.randint(200, 300),
	))
	struct.pack_into("<L", data, 0x24, binascii.crc32(data[0x40:], 0x4321))
	print("sending: ", hex(counter), hexdump(data))
	sock.sendto(data, (ADDR, PORT))
	time.sleep(1.0)
	counter += 1
	continue
