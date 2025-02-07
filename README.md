# Two-Button LED Blink Driver for Raspberry Pi

This is a simple Linux kernel module that uses two GPIO buttons to control the blink frequency of an LED on a Raspberry Pi.

## Features
- Uses GPIO buttons to increase or decrease the LED blink frequency
- Implements a polling mechanism using a kernel thread
- Adjustable blink frequency
- Works on Raspberry Pi 2B (tested with GPIO18 for LED, GPIO23 and GPIO24 for buttons)

## Requirements
- Raspberry Pi 2B (or similar device with GPIO support)
- Linux kernel with module support
- Build tools (`make`, `gcc`, `linux-headers` package for your kernel)

## Installation

### 1. Clone the repository:
```bash
git clone https://github.com/galitianu/two_button_blink.git
cd two-button-led-driver
```

### 2. Build the module:
```bash
make
```

### 3. Load the module:
```bash
sudo insmod two_button_blink.ko
```

### 4. Check if the device is registered:
```bash
dmesg | grep "two_button_blink"
```

### 5. Test functionality:
- Press the increment button (GPIO23) to increase blink frequency
- Press the decrement button (GPIO24) to decrease blink frequency

To see frequency changes being registered in real time, use ```sudo dmesg -wH```

## Unloading the Module
To remove the module, run:
```bash
sudo rmmod two_button_blink
```

## License
This project is licensed under the GPLv2 license.

## Author
Andrei Galitianu

