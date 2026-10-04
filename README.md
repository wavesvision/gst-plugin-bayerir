# GST Bayer-IR plugin


## Introduction
This plugin is designed to debayer the pixel stream from a RGB-IR camera.


## Requirements
A number of packages are required to build this.

For Debian:
```
sudo apt-get install build-essential libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev pkg-config
```


## Compiling
Currently this needs to be compiled on the target machine.
Change to the source directory, and run:
```
make
```

To test that it compiled correctly, run:
```
GST_PLUGIN_PATH=. gst-inspect-1.0 bayerir2bayer
```
Should list the plugin's capabilities.


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
Linear average is incomplete, it can only handle GRBG streams and has no IR filter.


## Use
To start streaming to a display (run from the plugin directory):
```
GST_PLUGIN_PATH=. gst-launch-1.0 v4l2src device=/dev/video0 ! 'video/x-bayer, format=(string)grbg10le, width=2592, height=1944, framerate=30/1' ! bayerir2bayer bayer-mode=rgbir-fast ! 'video/x-bayer,format=(string)grbg10le' ! bayer2rgb ! videoscale ! 'video/x-raw,height=800' ! ximagesink handle_events=false
```

To start streaming to a display, enabling the daytime IR filter (run from the plugin directory):
```
GST_PLUGIN_PATH=. gst-launch-1.0 v4l2src device=/dev/video0 ! 'video/x-bayer, format=(string)grbg10le, width=2592, height=1944, framerate=30/1' ! bayerir2bayer bayer-mode=rgbir-fast filter-ir=day ! 'video/x-bayer,format=(string)grbg10le' ! bayer2rgb ! videoscale ! 'video/x-raw,height=800' ! ximagesink handle_events=false
```


## Testing
To measure the framerate of the pipeline (run from the plugin directory):
```
GST_PLUGIN_PATH=. gst-launch-1.0 v4l2src device=/dev/video0 ! 'video/x-bayer, format=(string)grbg10le, width=2592, height=1944, framerate=30/1' ! bayerir2bayer bayer-mode=rgbir-test filter-ir=day ! 'video/x-bayer,format=(string)grbg10le' ! bayer2rgb ! fpsdisplaysink text-overlay=false video-sink=fakesink --verbose
``
