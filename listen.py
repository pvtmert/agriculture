#!/usr/bin/env python3

import os, sys, time, socket, struct, json

#ADDR = "224.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 4321

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, True)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 32)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, True)
sock.bind(("", PORT))

print("listening on: {}".format(PORT))

hexdump = lambda data, width=8, fmt="{hex:2X}": " ".join([
		("\n{index:4X} | " + fmt).format(index=i, hex=(b))
		if i%width is 0
		else fmt.format(index=i, hex=(b))
		for i, b in enumerate(data)
	])

config = struct.Struct("<5L")

def getTime(filename: str, ident: str, default=int(15e6), devices={}):
	if os.path.exists(filename):
		with open(filename) as file:
			devices = json.load(file)
	if ident not in devices.keys():
		return default
	return int(devices[ident])

while True:
	(data, addr) = sock.recvfrom(256)
	print("{ip}:{port}: #{length}: {data}".format(
		ip=addr[0],
		port=addr[1],
		data=hexdump(data),
		length=len(data),
	))
	lora_addr = struct.unpack_from("<L", data, 0x28)
	lora_data = struct.unpack_from("<6H", data, 0x42)
	#print([ hex(i) for i in lora_addr ])
	print("sensors:", lora_data)
	mbuffer = bytearray(32)
	config.pack_into(mbuffer, 0x0,
		lora_addr[0], # TARGET ADDR
		0x12345678,  # save
		0x87654321,  # mode
		0xABC00DEF,  # mesh
		getTime("devices.json", "{:08x}".format(*lora_addr)), # sleep
	)
	print("sending: ", hex(lora_addr[0]), hexdump(mbuffer))
	sock.sendto(mbuffer, addr)
	continue
