import struct
import sys

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

with open(serial_dev, "wb", buffering=0) as tty:
    tty.write(header)
    tty.write(kernel_data)

print(f"Sent {len(kernel_data)} bytes to {serial_dev}")
