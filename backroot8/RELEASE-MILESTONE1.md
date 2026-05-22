# Backroot 8 Milestone 1 — x86_64 disk image

Pre-built QEMU disk image (Arch Linux + Backroot 8 desktop). **ARM is not included** in this release.

## Download

From [GitHub Releases](https://github.com/lasangainc/os-backroot/releases/tag/v8-m1):

1. `backroot8-milestone1-x86_64.img.zst.part-aa`
2. `backroot8-milestone1-x86_64.img.zst.part-ab`
3. `SHA256SUMS-milestone1-x86_64` (verify after download)

GitHub limits uploads to 2 GiB per file, so the compressed image is split into two parts.

## Restore the image

```bash
cd backroot8
mkdir -p vm
# Move downloaded part-* files into vm/
sha256sum -c vm/SHA256SUMS-milestone1-x86_64
chmod +x scripts/restore-milestone1-image.sh
./scripts/restore-milestone1-image.sh
```

Requires `zstd` and ~6 GiB free under `vm/`.

## Bootable ISO (UTM / physical / any VM)

Build a **~1.4 GB** live ISO from the disk image:

```bash
cd backroot8
sudo ./scripts/build-iso.sh
```

Output: `vm/backroot8-x86_64.iso`

**UTM:** Create VM → mount **CD/DVD** → select `backroot8-x86_64.iso` → **Legacy BIOS** (UEFI off) → boot from CD.  
At GRUB, pick **Backroot 8 Milestone 1 (live)**. Login: `root` / `backroot8`.

## Run disk image in QEMU (cloud script)

```bash
./scripts/run-vm-gui.sh
```

Browser: http://localhost:6080/vnc.html?autoconnect=1&resize=scale  

SSH: `ssh -p 2222 root@localhost` (password `backroot8`)

## Build from source instead

```bash
mkdir -p backroot8/vm
cd backroot8
sudo ./scripts/build-rootfs.sh   # BACKROOT8_ARCH=x86_64 (default)
```

## Contents

- Milestone 1 branding (`/etc/os-release`: `8-m1`)
- `br8-wm`, `br8-panel`, `br8-start`, `backroot-hello`, `powerpdf`
- Arch `linux` kernel, Xorg, Dolphin, NetworkManager, OpenSSH
