#!/usr/bin/env python3

import os, sys, time, socket

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

while True:
	(data, addr) = sock.recvfrom(256)
	print("{ip}:{port}: #{length}: {data}".format(
		ip=addr[0],
		port=addr[1],
		data=hexdump(data),
		length=len(data),
	))
	sock.sendto("".join([ "hellobok" ] * 10).encode(), addr)
	continue
