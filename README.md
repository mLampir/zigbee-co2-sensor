# CO2 Zigbee sensor

> **Fork** of [florianL21/zigbee-co2-sensor](https://github.com/florianL21/zigbee-co2-sensor) with fixes for a sensor that stops measuring:
>
> - ⚠️ **SCL is on GPIO19 (D8), not GPIO23 (D5)** – adapted to one particular
>   sensor whose D5 pad has no connection. If you wire it as in the instructions
>   below, set `SDC4X_SCL_PIN` in `main/config.h` back to `GPIO_NUM_23` and build
>   yourself. The release files and the web flasher linked below are not
>   interchangeable: the web flasher installs the upstream firmware.
> - `I2C_WIRING_DIAG` in `main/config.h` (off by default) builds a test firmware
>   that does not talk to the SCD4x but toggles SDA and the free pins D5, D8, D9,
>   D10 between 0 V and 3.3 V every 3 s, to find a broken pad or solder joint with a
>   multimeter. At every start the firmware also logs a short pin test for SDA/SCL
>   (`released=1` means the SCD4x pull-up is reached).
> - **No light sleep** between measurements (`ENABLE_LIGHT_SLEEP` in `main/config.h`).
>   With light sleep the SCD4x stopped answering on I2C right after the first sleep;
>   without it the sensor measures reliably. It is USB powered, so the extra power
>   does not matter. The Zigbee sleep setting stays as paired (turning it off made
>   the rejoin fail).
> - Built with ESP-IDF **v5.3.4**. Up to v5.3.1 the I2C driver can spin forever in
>   `s_i2c_master_clear_bus` after an I2C error; the Zigbee part keeps running and
>   reports the last values, so the sensor looks alive but never measures again.
> - Zigbee API calls from the sensor task take the Zigbee lock (mandatory per the
>   esp-zigbee-sdk; newer IDF versions assert on it, see upstream issue #6).
> - The task watchdog restarts the ESP instead of only logging; after 15 minutes
>   without a valid measurement the firmware restarts itself. Waiting for a
>   measurement has a timeout; a failed report no longer aborts.
> - Min/max of the temperature and humidity clusters in ZCL units (0.01 °C / 0.01 %),
>   otherwise `esp_zb_zcl_set_attribute_val()` rejects every value.
> - Failed measurements and restarts since power-on are counted and logged (USB
>   serial). Reporting them as Analog Input (endpoints 11/12) is disabled for now:
>   `esp_zb_zcl_report_attr_cmd_req()` asserts for them in esp-zigbee-lib 1.5.1.
> - Every push to `main` creates a release `build-<n>` with the firmware files.

![Render of sensor](images/render.png)

This project is a Zigbee device based on the ESP32-H2 / ESP32-C6 that can read CO2 sensor values from a Sensirion SCD-4X based sensor via i2c.

The aim of this project is to provide a "pug-and-play" experience for building such a sensor and should be very easy to replicate.
To make building this as easy as possible a full list of step-by-step instructions is provided and firmware for the chips is precompiled and can be flashed via a web browser.

---

#### ⚠️ Disclaimer
The firmware for the H2 chip has not yet been tested and confirmed working

---

## Features

Once built the sensor will provide:

- Temperature
- Humidity
- CO2

These values are reported over zigbee every 5 minutes

Exposed values (example screenshot from Home Assistant ZHA):

![Home Assistant UI](images/ha.png)

## Hardware

This project comes bundled together with a ready made case that can be 3D-Printed.
The case and assembly instructions for it are provided on the [Printables page](https://www.printables.com/model/1036769-compact-co2-zigbee-sensor)

To be able to utilize the provided case the following components must be used:
- [Seeed Sudio XIAO-ESP32C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html)
- [Adafruit SCD-41](https://www.adafruit.com/product/5190)

If the case is not needed any variants/breakout boards that use the following components can be used with the provided firmware:
- [ESP32-H2](https://www.espressif.com/en/products/socs/h2/overview)
- [ESP32-C6](https://www.espressif.com/en/products/socs/c6/overview)
- [Sensirion SCD-41](https://sensirion.com/products/catalog/SCD41)


## GPIO

The firmware is configured to use the following PIO pins by default:

| GPIO   | Function              |
| ------ | --------------------- |
| GPIO22 | I2C SDA to SCD-41     |
| GPIO23 | I2C SCL to SCD-41 (**this fork: GPIO19 / D8**, see top) |

With the suggested breakout boards from above it would be wired up like this:

![Wiring diagram](images/wire_diagram.png)

Please note that if you pan on using the included enclose you should solder your wires to the BACK of the sensors PCB!

## Flashing

You can very easily flash the firmware using a handy web interface.
Simply go to the link below and click the connect button (note that you will have to use a supported browser for this to work):

[Web flash tool](https://florianl21.github.io/zigbee-co2-sensor/)

If the flashing does not work you may already have a firmware on the device that puts the ESP into deep sleep (will also be the case if you try to update this firmware).
In such a case you will need to hold the boot button of the ESP before plugging it into USB to put it into download mode (or be very fast with clicking the buttons in the web interface right after plugging it in).

## Un-pairing/factory reset

The firmware does currently not have any mechanic to support factory resetting the device from a user perspective.
If the device it removed from the zigbee coordinator while the device is connected it is automatically un-paired and will go into a state where it is ready to be re-paired.
If for some reason this is not an option, you can just erase the devices entire flash effectively "factory resetting" it. This can also be done via the web interface by selecting the `Erase device` checkbox when flashing the firmware:

![Screenshot of erase flash option in the web UI](images/factory_reset.png)

## Notes on power consumption

The firmware is quite okay with power consumption. On average the sensor will consume 2.5mWh (measured over the span of 14h).
This can be slightly reduced by disabling the onboard LED of the adafruit breakout board.

For now the firmware reports as being mains powered over zigbee as this is its intended use case.
Battery operation with such a low power draw is however totally feasible.
Further improvements could be made to the FW to lower the power consumption even more. For example:

- [ ] Increasing the `ED_KEEP_ALIVE` time to reduce the numbers of sleep wake-ups from the zigbee stack
- [ ] Currently the SCD-4x operates in a single shot operation mode which is quite low power but it has options for reducing the power consumption even further. For this to work a mechanic for custom calibration triggering would need to be implemented as in this low power mode autocalibration is not available.
- [ ] Decreasing the polling frequency of the SCD-4x to some time span over 30 minutes would allow for the ESP to utilize deep sleep (Espressif recommends not using deep sleep with zigbee for sensors that send more frequently than once every 30 minutes as with deep sleep the zigbee stack must be re-initialized after every wakeup, thus this puts additional strain on the zigbee network itself)
- [ ] Utilize the low power core to communicate with the sensor via I2C and only wake up from sleep (or even deep sleep) when there was an actual significant value change from the last update that was sent

# Credits

Thanks to @xmow49 for his [zigbee demo sensor repository](https://github.com/xmow49/ESP32H2-Zigbee-Demo).
The code in this repository is based on his implementation with some significant modifications
