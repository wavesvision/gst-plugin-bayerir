# GST Bayer-IR plugin


## Introduction
This plugin is designed to debayer the pixel stream from a RGB-IR camera.


## Requirements
A number of packages are required to build and run this.

For Debian:
```
sudo apt-get install build-essential pkg-config liborc-0.4-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
sudo apt-get install v4l-utils gstreamer1.0-tools gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad
```


## Compiling
Currently this needs to be compiled on the target machine.
Change to the source directory, and run:
```
make
```

Install the plugin into the system GStreamer plugin directory:
```
sudo make install
```

After installation, GStreamer finds the plugin automatically, so `GST_PLUGIN_PATH` is not required.

To test that it compiled and installed correctly, run:
```
gst-inspect-1.0 bayerir2bayer
```
Should list the plugin's capabilities.


## Capture device
Find and configure the OX05B1S with one command (from the `kmod-ox05b1s-basic` repository):
```
bash ../kmod-ox05b1s-basic/scripts.d/media-ctl_setup.sh
```

The script prints the devices and sensor format, for example:
```
Media device: /dev/media0
Video device: /dev/video8
Sensor device: /dev/v4l-subdev6
Sensor format: SRGGB10_1X10
Configured sensor: SRGGB10_1X10/2592x1944
```
Use the printed `Video device` in the commands below. The examples below use `/dev/video8`; replace it with the device printed by the script on your system.


## Plugins
A number of plugins are provided:
* bayerir2bayer: Bayer-IR to standard Bayer translator
* bayerir2grey: Bayer-IR to greyscale decoder
* bayerir2rgb: Bayer-IR to RGB decoder (deprecated)


### bayerir2bayer
This is the main plugin to use, and it has a number of parameters that can be set:
* bayer-mode: Method to transcode data
* filter-ir: Apply IR filter to RGB components
* filter-ir-blue: IR filter blue multiplier
* filter-ir-green: IR filter green multiplier
* filter-ir-red: IR filter red multiplier
* packed: Whether the source stream is packed
* swap-order: Swap order lines are processed in


### bayer-modes
Several methods are provided to translate bayer IR to standard bayer:
* fast: This method replicates pixel values to fill missing information
* lavg: Linear average, averages adjacent pixel values
* test: Used for development

### Fast mode
Fast mode is complete and should be able to handle any data streams presented.

### Linear averaging
Linear average currently has no IR filter. Use fast mode when IR filtering is required.


## Use
To start streaming to a display:
```
gst-launch-1.0 v4l2src device=/dev/video8 ! 'video/x-bayer,format=(string)rggb16le,width=(int)2592,height=(int)1944,framerate=(fraction)60/1' ! bayerir2bayer bayer-mode=rgbir-fast swap-order=false ! 'video/x-bayer,format=(string)rggb16le' ! bayer2rgb ! videoscale ! 'video/x-raw,height=800' ! ximagesink handle_events=false
```

To start streaming to a display, enabling the daytime IR filter:
```
gst-launch-1.0 v4l2src device=/dev/video8 ! 'video/x-bayer,format=(string)rggb16le,width=(int)2592,height=(int)1944,framerate=(fraction)60/1' ! bayerir2bayer bayer-mode=rgbir-fast filter-ir=day swap-order=false ! 'video/x-bayer,format=(string)rggb16le' ! bayer2rgb ! videoscale ! 'video/x-raw,height=800' ! ximagesink handle_events=false
```

The display examples require a graphical session. On a headless system, replace `ximagesink` with `fakesink`.


## Testing
To measure the framerate of the plugin path:
```
gst-launch-1.0 v4l2src device=/dev/video8 ! 'video/x-bayer,format=(string)rggb16le,width=(int)2592,height=(int)1944,framerate=(fraction)60/1' ! bayerir2bayer bayer-mode=rgbir-lavg swap-order=false ! 'video/x-bayer,format=(string)rggb16le' ! fpsdisplaysink text-overlay=false video-sink=fakesink --verbose
```

To measure the full display/debayer path:
```
gst-launch-1.0 v4l2src device=/dev/video8 ! 'video/x-bayer,format=(string)rggb16le,width=(int)2592,height=(int)1944,framerate=(fraction)60/1' ! bayerir2bayer bayer-mode=rgbir-fast filter-ir=day swap-order=false ! 'video/x-bayer,format=(string)rggb16le' ! bayer2rgb ! fpsdisplaysink text-overlay=false video-sink=fakesink --verbose
```
