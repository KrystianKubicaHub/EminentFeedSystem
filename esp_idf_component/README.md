# EminentFeedSystem — ESP-IDF Component

This directory provides an ESP-IDF component wrapper for integrating EminentFeedSystem
into any ESP-IDF project (ESP32, ESP32-S3, etc.).

## Usage

In your ESP-IDF project's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)

# Register the EminentFeedSystem path as an extra component directory
set(EXTRA_COMPONENT_DIRS "/path/to/EminentFeedSystem/esp_idf_component")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_app)
```

Or symlink/copy this directory into your project's `components/` folder:

```bash
cd your_esp_idf_project/components
ln -s /path/to/EminentFeedSystem/esp_idf_component eminent_sdk
```

Then in your code:
```cpp
#include "EminentSdk.hpp"
#include "PhysicalLayerEsp32Wifi.hpp"
```
