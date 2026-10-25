# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

# built only with TinyUSB's vendor class on (a debug build's sdkconfig); otherwise the component is empty
set(srcs)
if(CONFIG_TINYUSB_VENDOR_COUNT)
    list(APPEND srcs "usb_stream.cpp")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES freertos log)
