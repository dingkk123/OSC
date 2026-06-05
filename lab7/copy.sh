set -e

mkimage -f kernel.its kernel.fit

echo "[2/3] copy kernel.fit"
sudo cp -f kernel.fit /media/ubuntu/opirv2/
sync


sudo umount -l /media/ubuntu/opirv2

echo "[3/3] umount finish"
