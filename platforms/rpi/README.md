Raspberry Pi
============

## Setup ##

This project uses CMake (minimum version 3.0), you can download it [here](http://www.cmake.org/download/) or install it with apt-get.

```bash
sudo apt-get install cmake
```

Make sure to update git submodules before you build:

```bash
git submodule update --init
```

To build on Rasberry Pi you will need a C++ toolchain with support for C++14. GCC 4.9.2 (or higher) and Clang 3.4 (or higher) are known to work (refer [here](https://community.thinger.io/t/starting-with-the-raspberry-pi/36) for instructions on getting GCC 4.9).

You will also need to install some development packages:

```
sudo apt-get install libcurl4-openssl-dev libfontconfig1-dev
```

The demo application uses the Nextzen vector tile service, so you will need a Nextzen API key to build and run the demo. 

 1. Visit https://developers.nextzen.org/ to get an API key.

 2. Setup an environment variable (`NEXTZEN_API_KEY`) to point to your API key.
    ```bash
    export NEXTZEN_API_KEY=YOUR_API_KEY
    ```

## Build ##

Compile the demo application with:

```
make rpi
```

You can optionally use `make -j` to parallelize the build and append a `BUILD_TYPE` variable to choose the build type, for example `BUILD_TYPE=Debug`.

Run the demo program from the output folder:
```
cd build/rpi
./tangram
```

You can provide several command line options:
 - `-s` or `--scene` followed by a path or URL to a scene file to load
 - `-lat` or `--latitude` followed by a latitude for the map view
 - `-lon` or `--longitude` followed by a longitude for the map view
 - `-z` or `--zoom` followed by a zoom level for the map view
 - `-x` or `--x_position` followed by a horizontal offset in pixels for the window
 - `-y` or `--y_position` followed by a vertical offset in pixels for the window
 - `-w` or `--width` followed by a horizontal size in pixels for the window
 - `-h` or `--height` followed by a vertical size in pixels for the window
 - `-t` or `--tilt` followed by a tilt in radians for the map view
 - `-r` or `--rotation` followed by a rotation from North in radians for the map view

You can move the map with `w`, `a`, `s`, and `d`, zoom in and out with `-` and `=`, and quit with `esc`.

## Troubleshooting

If you encounter strange behavior or crashes, the first thing to try is to compile in __debug mode__, run it again, and watch the log output for clues.

```
make rpi BUILD_TYPE=Debug
...
cd build/rpi
./tangram
```

If there are `GL_OUT_OF_MEMORY` errors in the output, you can try increasing the memory allocated to the GPU. In your Raspberry Pi's [config.txt](https://www.raspberrypi.org/documentation/configuration/config-txt/), you can increase the GPU memory from the default value of 64MB to 128MB or more (https://www.raspberrypi.org/documentation/configuration/config-txt/memory.md).
