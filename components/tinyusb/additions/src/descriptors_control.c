// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
// Modifications Copyright © 2021 Ci4Rail GmbH
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "esp_log.h"
#include "descriptors_control.h"

static const char *TAG = "tusb_desc";
static tusb_desc_device_t s_descriptor;
static char *s_str_descriptor[USB_STRING_DESCRIPTOR_ARRAY_SIZE];
/* Required for CDC-ECM. MAC Address of the host NIC. */
static uint8_t mac_address[6];

#define MAX_DESC_BUF_SIZE 32

enum
{
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_CDC,
  STRID_MSC,
  STRID_HID,
  STRID_ECM,
  STRID_MAC,
};

#define EPNUM_NET_NOTIF   0x81
#define EPNUM_NET_OUT     0x02
#define EPNUM_NET_IN      0x82

#if CFG_TUD_HID //HID Report Descriptor
uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD), ),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE), )
};
#endif

uint8_t const desc_configuration[] = {
    // interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(2, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

#   if CFG_TUD_CDC
    // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, 0x81, 8, 0x02, 0x82, 64),
#   endif
#   if CFG_TUD_MSC
    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, STRID_MSC, EPNUM_MSC, 0x80 | EPNUM_MSC, 64), // highspeed 512
#   endif
#   if CFG_TUD_HID
    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, STRID_HID, HID_PROTOCOL_NONE, sizeof(desc_hid_report), 0x84, 16, 10),
#   endif
#   if CFG_TUD_NET
    // Interface number, description string index, MAC address string index, EP notification address and size, EP data address (out, in), and size, max segment size.
    TUD_CDC_ECM_DESCRIPTOR(ITF_NUM_CDC, STRID_ECM, STRID_MAC, EPNUM_NET_NOTIF, 64, EPNUM_NET_OUT, EPNUM_NET_IN, CFG_TUD_NET_ENDPOINT_SIZE, CFG_TUD_NET_MTU),
#   endif
};

// =============================================================================
// CALLBACKS
// =============================================================================

/**
 * @brief Invoked when received GET DEVICE DESCRIPTOR.
 * Application returns pointer to descriptor
 *
 * @return uint8_t const*
 */
uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&s_descriptor;
}

/**
 * @brief Invoked when received GET CONFIGURATION DESCRIPTOR.
 * Descriptor contents must exist long enough for transfer to complete
 *
 * @param index
 * @return uint8_t const* Application return pointer to descriptor
 */
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index; // for multiple configurations
    return desc_configuration;
}

static uint16_t _desc_str[MAX_DESC_BUF_SIZE];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void) langid;

    unsigned int chr_count = 0;

    if ( index == STRID_LANGID) {
        memcpy(&_desc_str[1], s_str_descriptor[STRID_LANGID], 2);
        chr_count = 1;
    }
    else if (STRID_MAC == index) {
      // Convert MAC address into UTF-16
  
      for (unsigned i=0; i<sizeof(mac_address); i++)
      {
        _desc_str[1+chr_count++] = "0123456789ABCDEF"[(mac_address[i] >> 4) & 0xf];
        _desc_str[1+chr_count++] = "0123456789ABCDEF"[(mac_address[i] >> 0) & 0xf];
      }
    }
    else {
        // Convert ASCII string into UTF-16

        if ( index >= sizeof(s_str_descriptor) / sizeof(s_str_descriptor[0]) ) {
            return NULL;
        }

        const char *str = s_str_descriptor[index];

        // Cap at max char
        chr_count = strlen(str);
        if ( chr_count > MAX_DESC_BUF_SIZE - 1 ) {
            chr_count = MAX_DESC_BUF_SIZE - 1;
        }

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2 * chr_count + 2);

    return _desc_str;
}

/**
 * @brief Invoked when received GET HID REPORT DESCRIPTOR
 * Application returns pointer to descriptor. Descriptor contents must exist
 * long enough for transfer to complete
 *
 * @return uint8_t const*
 */
#if CFG_TUD_HID
uint8_t const *tud_hid_descriptor_report_cb(void)
{
    return desc_hid_report;
}
#endif

// =============================================================================
// Driver functions
// =============================================================================

void tusb_set_descriptor(tusb_desc_device_t *desc, char **str_desc)
{
    ESP_LOGI(TAG, "Setting of a descriptor: \n"
             ".bDeviceClass       = %u\n"
             ".bDeviceSubClass    = %u,\n"
             ".bDeviceProtocol    = %u,\n"
             ".bMaxPacketSize0    = %u,\n"
             ".idVendor           = 0x%08x,\n"
             ".idProduct          = 0x%08x,\n"
             ".bcdDevice          = 0x%08x,\n"
             ".iManufacturer      = 0x%02x,\n"
             ".iProduct           = 0x%02x,\n"
             ".iSerialNumber      = 0x%02x,\n"
             ".bNumConfigurations = 0x%02x\n",
             desc->bDeviceClass, desc->bDeviceSubClass,
             desc->bDeviceProtocol, desc->bMaxPacketSize0,
             desc->idVendor, desc->idProduct, desc->bcdDevice,
             desc->iManufacturer, desc->iProduct, desc->iSerialNumber,
             desc->bNumConfigurations);
    s_descriptor = *desc;

    if (str_desc != NULL) {
        memcpy(s_str_descriptor, str_desc,
               sizeof(s_str_descriptor[0])*USB_STRING_DESCRIPTOR_ARRAY_SIZE);
    }
    tusb_desc_set = true;
}

void tusb_set_mac_address(uint8_t cfg_mac_address[6])
{
    memcpy(mac_address, cfg_mac_address, sizeof(mac_address)/sizeof(mac_address[0]));
}

tusb_desc_device_t *tusb_get_active_desc(void)
{
    return &s_descriptor;
}

char **tusb_get_active_str_desc(void)
{
    return s_str_descriptor;
}

void tusb_clear_descriptor(void)
{
    memset(&s_descriptor, 0, sizeof(s_descriptor));
    memset(&s_str_descriptor, 0, sizeof(s_str_descriptor));
    tusb_desc_set = false;
}
