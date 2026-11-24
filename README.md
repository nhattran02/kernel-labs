# LFD430 labs

Hands-on labs for a self-study course that follows the public outline of the
Linux Foundation course LFD430 "Developing Linux Device Drivers", on Linux v6.12.
Not produced or endorsed by the Linux Foundation.

* **Lab book:** `LFD430-LabBook.pdf` at the repository root (sources in
  `book/`; build it with `make labs` from the root). Read it first: every lab says which
  slides to read, what to build, and how it is accepted.
* **Where:** this `labs/` directory of the `lfd430-slides` repository; run every
  command below from inside it.
* **Branches:** `main` = slides and starting code with `TODO Tn` comments;
  `labs-solutions` = the same plus the reference implementation. Compare when you
  are done: `git diff HEAD labs-solutions -- <lab directory>`.

## Setup (Ubuntu 20.04 or newer)

```sh
sudo apt install build-essential git bc bison flex libelf-dev libssl-dev cpio \
     qemu-system-x86 qemu-system-arm qemu-utils busybox-static \
     device-tree-compiler gdb-multiarch sparse dwarves python3 \
     gcc-aarch64-linux-gnu gcc-arm-linux-gnueabihf gcc-i686-linux-gnu
# Do NOT install gcc-multilib: it conflicts with (removes) the ARM cross compilers.
git clone --depth 1 --branch v6.12 \
    https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git ~/lfd430/linux-6.12
env/scripts/build-kernel.sh x86_64 && env/scripts/build-busybox.sh x86_64 && env/scripts/mk-initramfs.sh x86_64
env/scripts/build-kernel.sh arm64  && env/scripts/build-busybox.sh arm64 && env/scripts/mk-initramfs.sh arm64
env/scripts/build-kernel.sh bbb && env/scripts/build-busybox.sh bbb \
    && env/scripts/mk-bbb-rootfs.sh  # BeagleBone Black (Labs 16, 17 part 2, 25)
env/scripts/build-sparse.sh          # Ubuntu 20.04 only: its sparse is too old
```
Paths are in `env/lab.env` (`LFD_KSRC`, `LFD_KBUILD`, `LFD_IMAGES`); each can be
overridden from the environment.

## Workflow

```sh
cd 00-foundations/lab01-modules
make              # build modules and test programs
make qemu         # boot the lab kernel, repository mounted at /lab
make qemu-test    # fresh guest, run test/run.sh, exit status = result
make check        # checkpatch + sparse
```

From `labs/` itself:

```sh
make              # build every lab
make test         # every acceptance test in QEMU (labs-solutions branch: all pass)
make test LABS="07-network/lab20-lfdnet"   # only some labs
make check        # checkpatch + sparse on every lab
make clean        # clean every lab and the test logs
make env          # x86_64 kernel + BusyBox + initramfs (also env-arm64, env-bbb, sparse)
```

## Labs

| # | Directory | Topic | Target |
|---|-----------|-------|--------|
| 00 | 00-foundations/lab00-environment | Lab environment | QEMU, BBB |
| 01 | 00-foundations/lab01-modules | Modules, namespaces, Kbuild/Kconfig | QEMU |
| 02 | 00-foundations/lab02-forensics | Oops, KASAN, kmemleak, ftrace, GDB | QEMU |
| 03 | 01-drivers-memory/lab03-allocators | Allocators, slab cache, mempool | QEMU |
| 04 | 02-chardev-uaccess/lab04-lfdbuf | Character devices | QEMU |
| 05 | 02-chardev-uaccess/lab05-ioctl | ioctl ABI, compat | QEMU |
| 06 | 02-chardev-uaccess/lab06-mmap | mmap, pinned pages | QEMU |
| 07 | 03-sync-waitqueues/lab07-races | Locking, RCU, lockdep | QEMU |
| 08 | 03-sync-waitqueues/lab08-blocking | Wait queues, poll | QEMU |
| 09 | 04-irq-time-deferred/lab09-timers | Clocks, timers, delays | QEMU |
| 10 | 04-irq-time-deferred/lab10-edu-irq | Interrupts (edu) | QEMU |
| 11 | 04-irq-time-deferred/lab11-deferred | Bottom halves | QEMU |
| 12 | 04-irq-time-deferred/lab12-uio | UIO user-space driver | QEMU |
| 13 | 05-devmodel-platform/lab13-lfdbus | Bus type, device model | QEMU |
| 14 | 05-devmodel-platform/lab14-firmware | Firmware loader | QEMU |
| 15 | 05-devmodel-platform/lab15-pl031-dt | Platform driver, DT, YAML | QEMU arm64 |
| 16 | 05-devmodel-platform/lab16-bbb-gpio-irq | GPIO IRQ latency | BBB |
| 17 | 05-devmodel-platform/lab17-uart | Serial-core UART | QEMU, BBB |
| 18 | 06-pci-dma/lab18-edu-pci | PCI driver | QEMU |
| 19 | 06-pci-dma/lab19-edu-dma | DMA | QEMU |
| 20 | 07-network/lab20-lfdnet | netdev + NAPI | QEMU |
| 21 | 07-network/lab21-rtl8139 | RTL8139 capstone | QEMU |
| 22 | 08-usb-block-pm/lab22-usb-mouse | USB driver | QEMU |
| 23 | 08-usb-block-pm/lab23-pm | Power management | QEMU |
| 24 | 08-usb-block-pm/lab24-blkmq | blk-mq block driver | QEMU |
| 25 | 05-devmodel-platform/lab25-bbb-eeprom | I2C client, regmap, nvmem | BBB |

Code: GPL-2.0. Book: CC BY-SA 4.0.
