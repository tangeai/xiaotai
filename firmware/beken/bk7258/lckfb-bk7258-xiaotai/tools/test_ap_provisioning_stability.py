#!/usr/bin/env python3
import socket
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ap_provisioning_stability import dns_query, parse_dns_a, percentile  # noqa: E402


transaction = 0x7258
query = dns_query("connectivitycheck.gstatic.com", transaction)
question = query[12:]
answer = b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x1e\x00\x04" + socket.inet_aton(
    "192.168.6.1"
)
response = struct.pack("!HHHHHH", transaction, 0x8180, 1, 1, 0, 0) + question + answer
assert parse_dns_a(response, transaction) == ["192.168.6.1"]
assert percentile([10.0, 20.0, 30.0, 40.0], 0.50) == 20.0
assert percentile([10.0, 20.0, 30.0, 40.0], 0.95) == 30.0

try:
    parse_dns_a(response[:15], transaction)
except ValueError:
    pass
else:
    raise AssertionError("truncated DNS response was accepted")

print("AP provisioning stability helper tests passed")
