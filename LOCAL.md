# Local Hardware & Build Verification (`LOCAL.md`)

## 1. Hardware & OS Environment
- **Target Board:** Raspberry Pi 4B (BCM2711 Cortex-A72, VideoCore VI `V3D 4.2.14.0`)
- **DRM/KMS Overlay:** `dtoverlay=vc4-kms-v3d` in `/boot/firmware/config.txt`
- **Compiler:** GCC (`g++ -std=c++26 -O3 -mcpu=cortex-a72 -mtune=cortex-a72 -fopenmp -Wall -Wextra`)

## 2. Build & Verification Commands

### Build Binary
```bash
cd /home/benjamin/butterfly/butterfly_vis
make clean && make -j4
```

### Verify Live Vision Mode (Headless / Default 75 Butterflies at 2x Hero Scale)
```bash
./butterfly_vis --frames 60
```

### Verify Interactive Demo Mode (Synthetic Head + 2 Palms)
```bash
./butterfly_vis --demo --frames 120
```

### Install / Update Systemd Service
```bash
sudo cp /home/benjamin/butterfly/butterfly_vis/butterfly.service /etc/systemd/system/butterfly.service
sudo systemctl daemon-reload
sudo systemctl restart butterfly.service
```
