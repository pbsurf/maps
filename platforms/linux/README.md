Ubuntu or Debian Linux
======================

## Setup ##

This project uses CMake (minimum version 3.2), you can download it [here](http://www.cmake.org/download/) or install it with apt-get.

```bash
sudo apt-get install cmake
```

Make sure to update git submodules before you build:

```bash
git submodule update --init
```

To build on Ubuntu or Debian you will need a C++ toolchain with support for C++14. GCC 4.9.2 (or higher) and Clang 3.4 (or higher) are known to work.

You will also need to install development packages for libcurl, x11, and opengl. On Ubuntu 16.04 or Debian Stretch all the required packages can be installed with

```bash
sudo apt-get install make g++ pkg-config libcurl4-openssl-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libgl1-mesa-dev
```

The demo application uses the Nextzen vector tile service, so you will need a Nextzen API key to build and run the demo.

 1. Visit https://developers.nextzen.org/ to get an API key.

 2. Setup an environment variable (`NEXTZEN_API_KEY`) to point to your API key.
    ```bash
    export NEXTZEN_API_KEY=YOUR_API_KEY
    ```

## Build ##

There are two ways to build the tangram-es library and demo application on Linux:

### Command Line ###

To build the executable demo application:

```bash
make linux
```

You can optionally use `make -j` to parallelize the build and append a `BUILD_TYPE` variable to choose the build type, for example `BUILD_TYPE=Debug`.

Then run it from the output folder:

```bash
cd build/linux/bin/ && ./tangram
```

You can open a different YAML scene file by dragging and dropping it into the window, or passing it as an argument:

```bash
cd build/linux/bin/ && ./tangram -f /path/to/your/scene.yaml
```

Tangram-es can optionally be built with system-installed font and GLFW libraries. You can install these libraries with:

```bash
sudo apt-get install libglfw3-dev libicu-dev libfreetype6-dev libharfbuzz-dev
```

Then compile with the following options:

```bash
make linux CMAKE_OPTIONS="-DTANGRAM_USE_SYSTEM_GLFW_LIBS=1 -DTANGRAM_USE_SYSTEM_FONT_LIBS=1"
```

### CLion ###

You can also run and debug from CLion.

After cloning and updating your git submodules, open CLion and __Import Project from Sources__. Select the root of this repo. Choose __Open Project__. Do not overwrite CMakeLists.txt.

CLion will automatically set everything up, all you have to do is wait a minute for the project to get initialized. Then, select the __tangram__ run configuration and hit run/debug.

![CLion Tangram Target](../../images/clion-tangram-target.png)
