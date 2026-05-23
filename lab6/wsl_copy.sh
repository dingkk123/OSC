set -e

mkimage -f kernel.its kernel.fit
sudo mount /dev/sde1 /mnt/sdboot
echo "[1/3] mount sdboot finish"

echo "[2/3] copy kernel.fit"
sudo cp -f kernel.fit /mnt/sdboot/
sync


sudo umount /mnt/sdboot
echo "[3/3] umount sdboot finish"
