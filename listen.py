#!/usr/bin/env python3

import os, sys, time, socket, struct, json

from collections import namedtuple

hexdump = lambda data, width=8, fmt="{hex:2X}": " ".join([
		("\n{index:4X} | " + fmt).format(index=i, hex=(b))
		if i%width is 0
		else fmt.format(index=i, hex=(b))
		for i, b in enumerate(data)
	])

lora = namedtuple("lora", [
	"meta",
	"header",
	"payload",
])
meta = namedtuple("meta", [
	"parse",
	"size",
	"rssi",
	"snr",
])
header = namedtuple("header", [
	"magic",
	"ver",
	"res",
	"dst",
	"id",
	"ts",
	"cs",
	"src",
	"size",
	"ttl",
	"net",
	"flags",
])
payload = namedtuple("payload", [
	"ver",
	"s30",
	"s60",
	"s90",
	"temp",
	"ec",
	"vp",
])

config = struct.Struct("<5L")
lora_meta = struct.Struct("<2L1h1f")
lora_header = struct.Struct("<1L2H5L4B")
lora_payload = struct.Struct("<7H")

def getTime(filename: str, ident: str, default=int(15e6), devices={}):
	if os.path.exists(filename):
		with open(filename) as file:
			devices = json.load(file)
	if ident not in devices.keys():
		return default
	return int(devices[ident])

"""
192.168.4.1:4321: #96:
   0 |  0  0  0  0 50  0  0  0
   8 | B5 FF  0  0  0  0 24 41 <- ends lora part
  10 | 78 56 34 12 CD AB  0  0 <- packet begins at 0x10
  18 | FF FF FF FF  7  0  0  0
  20 | 53  9  0  0 22 B4 14 C2
  28 | A4 43 E9 44 20 FF 99  0
  30 |  0  0  0  0  0  0  0  0
  38 |  0  0  0  0  0  0  0  0
  40 | BE EF FF  F  0  0  0  0 <- starts data "beef"
  48 | FF  F FF  F  0  0  0  0
  50 |  0  0  0  0  0  0  0  0
  58 |  0  0  0  0  0  0  0  0
"""

"""
(
	lora_magic,
	lora_ver,
	lora_res,
	lora_dst,
	lora_id,
	lora_ts,
	lora_cs,
	lora_src,
	lora_size,
	lora_ttl,
	lora_net,
	lora_flags,
) = lora_header.unpack_from(data, 0x10)
"""

def main():
	#ADDR = "224.0.0.1"
	PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 4321
	sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
	sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, True)
	sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 32)
	sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, True)
	sock.bind(("", PORT))
	print("listening on: {}".format(PORT))
	while True:
		(data, addr) = sock.recvfrom(256)
		print("{ip}:{port}: #{length}: {data}".format(
			ip=addr[0],
			port=addr[1],
			data=hexdump(data),
			length=len(data),
		))
		#lora_data = struct.unpack_from("<6H", data, 0x42)
		print(
			"\nmeta:",          meta(*lora_meta.unpack_from(data, 0x00)),
			"\nheaders:",   header(*lora_header.unpack_from(data, 0x10)),
			"\nsensors:", payload(*lora_payload.unpack_from(data, 0x40)),
		)
		mbuffer = bytearray(32)
		lora_addr = struct.unpack_from("<L", data, 0x28)[0]
		config.pack_into(mbuffer, 0x0,
			lora_addr, # TARGET ADDR
			0x12345678,  # save
			0x87654321,  # mode
			0xABC00DEF,  # mesh
			getTime("devices.json", "{:08x}".format(lora_addr)), # sleep
		)
		print("sending: ", hex(lora_addr), hexdump(mbuffer))
		sock.sendto(mbuffer, addr)
		continue
	return


if __name__ == '__main__':
	main()
