ibm515x
=======

![IBM 5153 and RPi CGA](https://github.com/trylle/ibm515x/raw/master/img/ibm5153_and_rpi.png)

This repository contains code and configuration for rendering an UDP video stream (netvid) to an IBM 5153 compatible monitor via a Raspberry Pi. Crude hooks are available for [RetroArch](https://github.com/trylle/RetroArch) and [Open Broadcaster Software](https://github.com/trylle/obs-studio) to provide the video stream.

## Dependencies

* Boost
* Eigen 3

### DPI-based code

* SDL (optional)

### GPIO-based code (non-functional)

* [PJ_RPI](https://github.com/Pieter-Jan/PJ_RPI)
* [jss_bitmask](https://www.justsoftwaresolutions.co.uk/cplusplus/using-enum-classes-as-bitfields.html)

## Interface

Video signals are provided through the [DPI (Parallel Display Interface)](https://www.raspberrypi.org/documentation/hardware/raspberrypi/dpi/README.md), using 4 bits of color (RGBI) and 2 sync signals. Due to the RPi pixel clock being picky, the pixel clock and the relevant timings had to be tripled. The actual framebuffer resolution is therefore 1920x200.

The repository also includes a GPIO interface, which was never finished, after oscilloscope analysis revealed timing issues (suspected main culprit: USB polling).

### Pinout on RPi header

![RGBI pinout on RPi header](https://github.com/trylle/ibm515x/raw/master/img/pinout.svg?sanitize=true)

## Adapter

![Photo of adapter](https://github.com/trylle/ibm515x/raw/master/img/rpi_cga.png)
![Adapter circuit diagram](https://github.com/trylle/ibm515x/raw/master/img/adapter.svg?sanitize=true)

The actual adapter board and timings were based on [the previous work by Benjamin Gould](http://www.paradigmlift.net/projects/teensy_cga.html). I added some resistors to limit the current draw; purely precautionary. On my monitor I also needed to adjust the vertical and horizontal porches to provide a centered image.

The required config, cmdline and device tree overlay files suitable for a Raspberry Pi 3 have been added to this repository.

![16-color palette as rendered by fbrender_test](https://github.com/trylle/ibm515x/raw/master/img/cga_16.png)

The adapter can render the complete 16 color CGA palette at up to 640x200 resolution.

![136-color palette as rendered by fbrender_test](https://github.com/trylle/ibm515x/raw/master/img/cga_136.png)

Temporal dithering is supported for extending the palette to 136 colors. The image above shows two 60 Hz exposures combined.

## Downsampling

A downsample application provides needed processing to convert a 16-bit/32-bit RGB video stream (as provided by RetroArch/OBS) into a CGA compatible 4-bit video stream. It supports nearest neighbor color downsampling (for rendering conventional CGA/EGA output), temporal and bayer dithering, local contrast enhancement and black level adjustment.

## Demo videos

[![Space Quest 3](https://img.youtube.com/vi/8KQf0JHnT7E/0.jpg)](https://www.youtube.com/watch?v=8KQf0JHnT7E)
[![Quest for Glory (EGA)](https://img.youtube.com/vi/awt02u_cEpc/0.jpg)](https://www.youtube.com/watch?v=awt02u_cEpc)
[![Beneath a Steel Sky](https://img.youtube.com/vi/MLR8ObHTDDo/0.jpg)](https://www.youtube.com/watch?v=MLR8ObHTDDo)
[![Loom (EGA)](https://img.youtube.com/vi/PoJVduigMK8/0.jpg)](https://www.youtube.com/watch?v=PoJVduigMK8)
[![Caren and the Tangled Tentacles (C64)](https://img.youtube.com/vi/N0ByRlbwS8A/0.jpg)](https://www.youtube.com/watch?v=N0ByRlbwS8A)
