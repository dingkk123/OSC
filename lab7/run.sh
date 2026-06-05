set -e

cd kernel
make
echo "[1/3] kernel finish"

echo "[2/3] copy kernel.bin"
cp kernel.bin ..

echo "[3/3] send kernel"
cd ..
sudo python3 send_kernel.py /dev/ttyUSB0 kernel.bin

sudo screen /dev/ttyUSB0 115200