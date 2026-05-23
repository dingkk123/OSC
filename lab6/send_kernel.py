import struct
import sys
import os
import termios

if len(sys.argv) != 3:
    print(f"Usage: python3 {sys.argv[0]} <serial_device> <kernel_bin>")
    sys.exit(1)

serial_dev = sys.argv[1]
kernel_path = sys.argv[2]

with open(kernel_path, "rb") as f:
    kernel_data = f.read()

header = struct.pack('<II',
    0x544F4F42,         # "BOOT"
    len(kernel_data),   # size
)
# <:little edian I:unsigned int(4bytes) I:unsigned int(4bytes)
#data send will be 42(B) 4F(O) 4F(O) 54(T)

payload = header + kernel_data

fd = os.open(serial_dev, os.O_WRONLY | os.O_NOCTTY)
try:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)

    sent = 0
    total = len(payload)
    chunk_size = 4096

    while sent < total:
        n = os.write(fd, payload[sent:sent + chunk_size])
        if n <= 0:
            raise RuntimeError("serial write returned 0 bytes")
        sent += n
        if sent == total or sent % (64 * 1024) < chunk_size:
            print(f"\rSending {sent}/{total} bytes", end="", flush=True)

    termios.tcdrain(fd)
    print()
finally:
    os.close(fd)

print(f"Sent {len(kernel_data)} bytes to {serial_dev}")

