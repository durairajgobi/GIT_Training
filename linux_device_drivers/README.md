# 100 Linux Device Driver Sample Programs in C

This folder has 100 small Linux kernel modules. Each one is a single `.c` file that shows one driver idea. They start with a "hello world" module and go up to block, network, hwmon, watchdog and USB/PCI drivers. Every file has a short header that says what it does and how to try it.

All 100 modules build with no warnings against **Linux 6.8** headers. Where a kernel API changed (`class_create`, platform `.remove`, `blk_mq_alloc_disk`), the code uses `LINUX_VERSION_CODE` checks, so it should also build on newer 6.x kernels.

## Layout

```
linux_device_drivers/
├── Makefile                      # builds every category + user apps
├── 01_module_basics/ ... 10_subsystems_advanced/
│   ├── Makefile                  # builds every .c in the folder as its own .ko
│   └── NNN_name.c
└── user_apps/                    # user-space test programs for some samples
```

## Building

```bash
# Kernel headers for the running kernel
sudo apt install build-essential linux-headers-$(uname -r)    # Debian/Ubuntu
sudo dnf install kernel-devel kernel-headers gcc make         # Fedora/RHEL

cd linux_device_drivers
make                              # all modules and user apps
make -C 02_char_drivers           # just one category
make KDIR=/path/to/linux/build    # cross / custom kernel tree
make clean
```

## Loading and testing

```bash
sudo insmod 01_module_basics/001_hello_world.ko
dmesg | tail
lsmod | grep hello
sudo rmmod 001_hello_world

# Module parameters
sudo insmod 01_module_basics/002_module_params.ko count=3 name=driver
modinfo 01_module_basics/002_module_params.ko

# Char device + user-space test
sudo insmod 03_ioctl_poll_mmap/021_ioctl_basic.ko
sudo ./user_apps/ioctl_basic_test
```

> **Hardware-bound samples.** Some drivers bind to real hardware or Device Tree nodes: 062, 065, 066, 079, 084, 085, 086, 087 and 088. Loading the module only registers the driver, and `probe()` runs only when a matching device exists. Each header gives the DT snippet or IDs to use. 088 matches QEMU's `-device edu`. The other platform samples (067, 076–078, 083, 090, 094–096) register their own virtual device, so they run on any machine.

> **Safety.** Kernel code runs with full privileges, and a bug can crash or hang the machine. Try these in a VM such as QEMU, VirtualBox or a cloud instance, not on a machine you care about. Secure Boot systems may also require signed modules.

## User-space test programs (`user_apps/`)

| Program | For sample | Purpose |
|---------|-----------|---------|
| `ioctl_basic_test` | 021 | set/get/reset an int via ioctl |
| `ioctl_struct_test` | 022 | exchange a config struct, check validation |
| `poll_test` | 024 | wait for data with `poll()` |
| `fasync_test` | 025 | receive `SIGIO` notifications |
| `mmap_test` | 027 / 028 | map a kernel buffer into user space |
| `netlink_test` | 100 | send/receive netlink messages |

## Index of all 100 programs

### 01. Module basics (`01_module_basics/`)

| # | File | What it shows |
|---|------|---------------|
| 001 | [`001_hello_world.c`](01_module_basics/001_hello_world.c) | The smallest possible loadable kernel module |
| 002 | [`002_module_params.c`](01_module_basics/002_module_params.c) | Passing parameters to a module at load time |
| 003 | [`003_param_array.c`](01_module_basics/003_param_array.c) | Array module parameters |
| 004 | [`004_param_callback.c`](01_module_basics/004_param_callback.c) | Get notified when a parameter changes at runtime |
| 005 | [`005_export_symbol.c`](01_module_basics/005_export_symbol.c) | Exporting a function and variable for other modules |
| 006 | [`006_printk_levels.c`](01_module_basics/006_printk_levels.c) | Log levels, pr_fmt, rate limiting and print-once |
| 007 | [`007_jiffies_hz.c`](01_module_basics/007_jiffies_hz.c) | Measuring time with jiffies and HZ |
| 008 | [`008_kernel_version.c`](01_module_basics/008_kernel_version.c) | Compile-time and run-time kernel version checks |
| 009 | [`009_init_exit_sections.c`](01_module_basics/009_init_exit_sections.c) | __init, __initdata, __exit and error unwinding |
| 010 | [`010_module_refcount.c`](01_module_basics/010_module_refcount.c) | Holding a module reference |

### 02. Character drivers (`02_char_drivers/`)

| # | File | What it shows |
|---|------|---------------|
| 011 | [`011_static_major.c`](02_char_drivers/011_static_major.c) | Old-style registration with a fixed major number |
| 012 | [`012_dynamic_major.c`](02_char_drivers/012_dynamic_major.c) | Dynamically allocated device numbers |
| 013 | [`013_cdev_basic.c`](02_char_drivers/013_cdev_basic.c) | struct cdev with open/release/read/write callbacks |
| 014 | [`014_auto_device_node.c`](02_char_drivers/014_auto_device_node.c) | Let udev create /dev/auto_node via class/device |
| 015 | [`015_read_write_buffer.c`](02_char_drivers/015_read_write_buffer.c) | A character device backed by a kernel buffer |
| 016 | [`016_llseek.c`](02_char_drivers/016_llseek.c) | Implementing llseek for a fixed-size memory device |
| 017 | [`017_multi_minor.c`](02_char_drivers/017_multi_minor.c) | One driver, several device instances (minors) |
| 018 | [`018_private_data.c`](02_char_drivers/018_private_data.c) | Per-open state stored in file->private_data |
| 019 | [`019_exclusive_open.c`](02_char_drivers/019_exclusive_open.c) | Allow only one process to open the device at a time |
| 020 | [`020_misc_device.c`](02_char_drivers/020_misc_device.c) | Misc device with explicit init/exit |

### 03. ioctl, poll, async I/O and mmap (`03_ioctl_poll_mmap/`)

| # | File | What it shows |
|---|------|---------------|
| 021 | [`021_ioctl_basic.c`](03_ioctl_poll_mmap/021_ioctl_basic.c) | Simple ioctl commands to get/set an integer |
| 022 | [`022_ioctl_struct.c`](03_ioctl_poll_mmap/022_ioctl_struct.c) | Exchanging a configuration structure via ioctl |
| 023 | [`023_ioctl_capability.c`](03_ioctl_poll_mmap/023_ioctl_capability.c) | Restricting privileged ioctls with capable() |
| 024 | [`024_poll_select.c`](03_ioctl_poll_mmap/024_poll_select.c) | Supporting poll()/select()/epoll() |
| 025 | [`025_fasync_sigio.c`](03_ioctl_poll_mmap/025_fasync_sigio.c) | Asynchronous notification with SIGIO |
| 026 | [`026_kfifo_device.c`](03_ioctl_poll_mmap/026_kfifo_device.c) | A FIFO character device using the kfifo API |
| 027 | [`027_mmap_page.c`](03_ioctl_poll_mmap/027_mmap_page.c) | Sharing one kernel page with user space through mmap() |
| 028 | [`028_mmap_vmalloc.c`](03_ioctl_poll_mmap/028_mmap_vmalloc.c) | mmap() of a multi-page vmalloc buffer |
| 029 | [`029_scull_quantum.c`](03_ioctl_poll_mmap/029_scull_quantum.c) | A simplified LDD3 "scull" device |
| 030 | [`030_blocking_pipe.c`](03_ioctl_poll_mmap/030_blocking_pipe.c) | A ring-buffer "pipe" with blocking reads and writes |

### 04. procfs, sysfs and debugfs (`04_procfs_sysfs_debugfs/`)

| # | File | What it shows |
|---|------|---------------|
| 031 | [`031_procfs_read.c`](04_procfs_sysfs_debugfs/031_procfs_read.c) | A read-only /proc entry |
| 032 | [`032_procfs_rw.c`](04_procfs_sysfs_debugfs/032_procfs_rw.c) | A read/write /proc entry used as a config knob |
| 033 | [`033_seq_file.c`](04_procfs_sysfs_debugfs/033_seq_file.c) | The seq_file iterator interface for large /proc output |
| 034 | [`034_proc_dir_single.c`](04_procfs_sysfs_debugfs/034_proc_dir_single.c) | A /proc directory with single_open() entries |
| 035 | [`035_sysfs_kobject.c`](04_procfs_sysfs_debugfs/035_sysfs_kobject.c) | Creating a kobject with attributes under /sys/kernel |
| 036 | [`036_sysfs_attr_group.c`](04_procfs_sysfs_debugfs/036_sysfs_attr_group.c) | Several attributes registered as one group |
| 037 | [`037_device_attr.c`](04_procfs_sysfs_debugfs/037_device_attr.c) | DEVICE_ATTR attributes attached to a misc device |
| 038 | [`038_debugfs_basic.c`](04_procfs_sysfs_debugfs/038_debugfs_basic.c) | Exposing driver internals through debugfs |
| 039 | [`039_debugfs_regset.c`](04_procfs_sysfs_debugfs/039_debugfs_regset.c) | Dumping a (simulated) register bank via debugfs |
| 040 | [`040_sysfs_notify.c`](04_procfs_sysfs_debugfs/040_sysfs_notify.c) | Waking user-space pollers on a sysfs attribute |

### 05. Synchronization (`05_synchronization/`)

| # | File | What it shows |
|---|------|---------------|
| 041 | [`041_mutex.c`](05_synchronization/041_mutex.c) | Protecting shared data with a mutex across kernel threads |
| 042 | [`042_spinlock.c`](05_synchronization/042_spinlock.c) | Spinlocks shared between process and interrupt context |
| 043 | [`043_rwlock.c`](05_synchronization/043_rwlock.c) | Reader/writer spinlock for a read-mostly lookup table |
| 044 | [`044_semaphore.c`](05_synchronization/044_semaphore.c) | A counting semaphore limiting concurrent users |
| 045 | [`045_atomic.c`](05_synchronization/045_atomic.c) | atomic_t, atomic64_t, refcount_t and atomic bit operations |
| 046 | [`046_completion.c`](05_synchronization/046_completion.c) | Waiting for an event with struct completion |
| 047 | [`047_wait_queue.c`](05_synchronization/047_wait_queue.c) | Wait queues with a condition and a timeout |
| 048 | [`048_rcu.c`](05_synchronization/048_rcu.c) | Read-Copy-Update protecting a configuration pointer |
| 049 | [`049_seqlock.c`](05_synchronization/049_seqlock.c) | seqlock for small data that is written rarely and read often |
| 050 | [`050_percpu.c`](05_synchronization/050_percpu.c) | Per-CPU counters that avoid cache-line contention |

### 06. Timers and deferred work (`06_timers_deferred_work/`)

| # | File | What it shows |
|---|------|---------------|
| 051 | [`051_kernel_timer.c`](06_timers_deferred_work/051_kernel_timer.c) | A periodic timer_list timer |
| 052 | [`052_hrtimer.c`](06_timers_deferred_work/052_hrtimer.c) | High-resolution timer with nanosecond precision |
| 053 | [`053_tasklet.c`](06_timers_deferred_work/053_tasklet.c) | Tasklets as a softirq-context bottom half |
| 054 | [`054_workqueue.c`](06_timers_deferred_work/054_workqueue.c) | Deferring work to process context on the system workqueue |
| 055 | [`055_delayed_work.c`](06_timers_deferred_work/055_delayed_work.c) | Periodic polling with delayed_work |
| 056 | [`056_custom_workqueue.c`](06_timers_deferred_work/056_custom_workqueue.c) | A dedicated ordered workqueue with many work items |
| 057 | [`057_kthread.c`](06_timers_deferred_work/057_kthread.c) | A kernel thread bound to a CPU that stops cleanly |
| 058 | [`058_kthread_worker.c`](06_timers_deferred_work/058_kthread_worker.c) | kthread_worker: a private thread that runs queued work |
| 059 | [`059_delays.c`](06_timers_deferred_work/059_delays.c) | Busy-wait vs sleeping delays and how long they really take |
| 060 | [`060_ktime.c`](06_timers_deferred_work/060_ktime.c) | Reading the different kernel clocks |

### 07. Interrupts, GPIO and notifiers (`07_interrupts_gpio/`)

| # | File | What it shows |
|---|------|---------------|
| 061 | [`061_shared_irq.c`](07_interrupts_gpio/061_shared_irq.c) | Attaching a handler to an existing shared IRQ line |
| 062 | [`062_threaded_irq.c`](07_interrupts_gpio/062_threaded_irq.c) | Threaded interrupt handler in a platform driver |
| 063 | [`063_irq_top_bottom.c`](07_interrupts_gpio/063_irq_top_bottom.c) | Top half queues data, bottom half (workqueue) consumes it |
| 064 | [`064_irq_work.c`](07_interrupts_gpio/064_irq_work.c) | Raising a callback in hard-IRQ context with irq_work |
| 065 | [`065_gpio_button_irq.c`](07_interrupts_gpio/065_gpio_button_irq.c) | Push button on a GPIO with debounce and IRQ |
| 066 | [`066_gpio_led_blink.c`](07_interrupts_gpio/066_gpio_led_blink.c) | Blinking an LED on a GPIO output with a timer |
| 067 | [`067_gpio_chip.c`](07_interrupts_gpio/067_gpio_chip.c) | A virtual GPIO controller (provider side) |
| 068 | [`068_keyboard_notifier.c`](07_interrupts_gpio/068_keyboard_notifier.c) | Observing console keyboard events via a notifier |
| 069 | [`069_netdev_notifier.c`](07_interrupts_gpio/069_netdev_notifier.c) | Reacting to network interface events |
| 070 | [`070_custom_notifier_chain.c`](07_interrupts_gpio/070_custom_notifier_chain.c) | Defining your own notifier chain plus a reboot notifier |

### 08. Memory management and DMA (`08_memory_dma/`)

| # | File | What it shows |
|---|------|---------------|
| 071 | [`071_kmalloc.c`](08_memory_dma/071_kmalloc.c) | kmalloc family: kmalloc, kzalloc, kcalloc, krealloc, ksize |
| 072 | [`072_vmalloc.c`](08_memory_dma/072_vmalloc.c) | vmalloc vs kvmalloc for large buffers |
| 073 | [`073_kmem_cache.c`](08_memory_dma/073_kmem_cache.c) | A slab cache for frequently allocated objects |
| 074 | [`074_alloc_pages.c`](08_memory_dma/074_alloc_pages.c) | Page allocator: alloc_pages, page_address, kmap_local |
| 075 | [`075_mempool.c`](08_memory_dma/075_mempool.c) | Guaranteed allocations with a mempool |
| 076 | [`076_devm_resources.c`](08_memory_dma/076_devm_resources.c) | Device-managed (devm_*) resources |
| 077 | [`077_dma_coherent.c`](08_memory_dma/077_dma_coherent.c) | Coherent DMA buffer for a descriptor ring |
| 078 | [`078_dma_streaming.c`](08_memory_dma/078_dma_streaming.c) | Streaming DMA mappings: single buffer and scatterlist |
| 079 | [`079_ioremap_mmio.c`](08_memory_dma/079_ioremap_mmio.c) | Mapping and accessing memory-mapped registers |
| 080 | [`080_kernel_lists.c`](08_memory_dma/080_kernel_lists.c) | Kernel data structures: list_head, hlist hashtable, rbtree |

### 09. Platform and bus drivers (platform, DT, I2C, SPI, USB, PCI, input, LED) (`09_bus_drivers/`)

| # | File | What it shows |
|---|------|---------------|
| 081 | [`081_platform_driver.c`](09_bus_drivers/081_platform_driver.c) | Minimal platform driver with probe/remove and PM ops |
| 082 | [`082_platform_device.c`](09_bus_drivers/082_platform_device.c) | Registering platform devices from code (board file style) |
| 083 | [`083_platform_resources.c`](09_bus_drivers/083_platform_resources.c) | Passing MEM/IRQ resources and platform data |
| 084 | [`084_device_tree_props.c`](09_bus_drivers/084_device_tree_props.c) | Matching via Device Tree and reading properties |
| 085 | [`085_i2c_client.c`](09_bus_drivers/085_i2c_client.c) | I2C client driver for a temperature sensor (LM75-like) |
| 086 | [`086_spi_device.c`](09_bus_drivers/086_spi_device.c) | SPI protocol driver for an EEPROM-like chip |
| 087 | [`087_usb_skeleton.c`](09_bus_drivers/087_usb_skeleton.c) | USB driver exposing a bulk-IN/OUT endpoint pair as a char device (/dev/trainusbN), modeled on drivers/usb/usb-skeleton.c |
| 088 | [`088_pci_skeleton.c`](09_bus_drivers/088_pci_skeleton.c) | PCI driver: enable device, map BAR0, MSI interrupt |
| 089 | [`089_input_device.c`](09_bus_drivers/089_input_device.c) | A virtual input device that generates key events |
| 090 | [`090_led_class.c`](09_bus_drivers/090_led_class.c) | Registering LEDs with the LED class (with triggers) |

### 10. Subsystem drivers and advanced topics (`10_subsystems_advanced/`)

| # | File | What it shows |
|---|------|---------------|
| 091 | [`091_virtual_netdev.c`](10_subsystems_advanced/091_virtual_netdev.c) | A virtual Ethernet device that loops TX back to RX |
| 092 | [`092_netfilter_hook.c`](10_subsystems_advanced/092_netfilter_hook.c) | Counting (and optionally dropping) ICMP packets |
| 093 | [`093_blk_mq_ramdisk.c`](10_subsystems_advanced/093_blk_mq_ramdisk.c) | A RAM-backed block device using blk-mq |
| 094 | [`094_hwmon_sensor.c`](10_subsystems_advanced/094_hwmon_sensor.c) | A virtual temperature/fan sensor using the hwmon API |
| 095 | [`095_watchdog.c`](10_subsystems_advanced/095_watchdog.c) | A software watchdog using the watchdog core |
| 096 | [`096_virtual_rtc.c`](10_subsystems_advanced/096_virtual_rtc.c) | A virtual RTC driver backed by an offset from system time |
| 097 | [`097_power_supply.c`](10_subsystems_advanced/097_power_supply.c) | A virtual battery for the power_supply class |
| 098 | [`098_kobject_uevent.c`](10_subsystems_advanced/098_kobject_uevent.c) | Sending custom uevents to udev / user space |
| 099 | [`099_kprobe.c`](10_subsystems_advanced/099_kprobe.c) | Tracing a kernel function with a kprobe and kretprobe |
| 100 | [`100_netlink_socket.c`](10_subsystems_advanced/100_netlink_socket.c) | Kernel <-> user space messaging over netlink |

## Suggested learning path

1. **01 → 02**: module lifecycle, then the `file_operations` model.
2. **03 → 04**: the ways user space talks to a driver (ioctl, poll, mmap, proc/sys/debugfs).
3. **05 → 06**: concurrency and deferred execution. Read these before writing interrupt code.
4. **07 → 08**: interrupts, GPIO, memory and DMA.
5. **09 → 10**: the Linux device model (bus/driver/device) and the kernel's subsystem frameworks.

## References

- *Linux Device Drivers, 3rd Edition* (Corbet, Rubini, Kroah-Hartman), the classic text, now dated in places
- Kernel documentation: <https://docs.kernel.org/driver-api/>
- In-tree examples: `samples/` and `drivers/usb/usb-skeleton.c` in the kernel source
