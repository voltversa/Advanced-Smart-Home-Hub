#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * Central application configuration.
 *
 * Keep hardware-independent timing, thresholds and Blynk virtual-pin
 * assignments here. Private Wi-Fi/Blynk credentials live in app_secrets.h.
 */

#include "app_secrets.h"

#if defined(__GNUC__)
#define APP_UNUSED __attribute__((unused))
#else
#define APP_UNUSED
#endif

/* Remote services used by the ESP-01 AT firmware. */
#define BLYNK_SERVER                 "lon1.blynk.cloud"
#define BLYNK_TEMPLATE_ID            "TMPL58WLMvqE4"
#define BLYNK_TEMPLATE_NAME          "Home HUB"
#define LOCATION_SERVER              "ip-api.com"
#define WEATHER_SERVER               "wttr.in"
#define WEATHER_FALLBACK_SERVER      "wttr.is"

/* Application timing. */
#define FLAME_SAMPLE_PERIOD_MS       200U
#define SENSOR_SAMPLE_PERIOD_MS      5000U
#define DISPLAY_REFRESH_MS           500U
#define BLYNK_UPLOAD_PERIOD_MS       10000U
#define BLYNK_RETRY_PERIOD_MS        3000U
#define WIFI_RETRY_PERIOD_MS         30000U
#define SENSOR_RETRY_PERIOD_MS       30000U
#define WEATHER_REFRESH_MS           10000U
#define WEATHER_RETRY_MS             10000U
#define LOCATION_REFRESH_MS          21600000U
#define LOCATION_RETRY_MS            300000U

/* Retained for the optional single-pin Blynk diagnostic page. */
#define BLYNK_SCREEN_DELAY_MS        1500U

/* Indoor-comfort and alarm thresholds. Values ending in 10 use 0.1 units. */
#define CO2_FAIR_PPM                 1000U
#define CO2_POOR_PPM                 1500U
#define CO2_DANGER_PPM               2500U
#define ROOM_TEMP_LOW10              100
#define ROOM_TEMP_HIGH10             350
#define ROOM_RH_LOW10                200U
#define ROOM_RH_HIGH10               750U
#define PRESSURE_MIN_HPA10           8500U
#define PRESSURE_MAX_HPA10           11000U

/*
 * SCD41 integration temperature offset, in 0.1 degC.
 *
 * Calibrated after the assembled hub reached thermal equilibrium:
 *   SCD41 output  = 32.3 degC
 *   reference     = 27.0 degC
 *   previous SCD41 offset = 4.0 degC (factory default)
 *
 * Sensirion's equation gives: 32.3 - 27.0 + 4.0 = 9.3 degC.
 * The firmware writes this absolute offset at every boot while the sensor is
 * idle. It is intentionally not persisted, avoiding unnecessary EEPROM writes.
 * Recalibrate this value after changing the enclosure, airflow or heat load.
 */
#define SCD41_TEMPERATURE_OFFSET10   93U

/* IIR filter: each new SCD41 sample contributes 1/4 of the displayed value. */
#define ROOM_TEMPERATURE_FILTER_DIV  4
#define SENSOR_FAILURE_LIMIT         3U

/* BME688 adaptive gas-change detection. */
#define GAS_BASELINE_SAMPLES         60U
#define GAS_TRIGGER_RATIO_PERCENT    55U
#define GAS_CLEAR_RATIO_PERCENT      75U
#define GAS_ALARM_CONFIRM_SAMPLES    6U
#define GAS_CLEAR_CONFIRM_SAMPLES    6U
#define GAS_TRIGGER_CHANGE_PERCENT   (100U - GAS_TRIGGER_RATIO_PERCENT)
#define GAS_CLEAR_CHANGE_PERCENT     (100U - GAS_CLEAR_RATIO_PERCENT)

/* Blynk virtual-pin assignments. Each pin must exist in the Home HUB template. */
#define VPIN_FLAME_DETECTED          0U
#define VPIN_FLAME_ADC               1U
#define VPIN_FLAME_VOLTAGE           2U
#define VPIN_CO2                     3U
#define VPIN_SCD_TEMPERATURE         4U
#define VPIN_SCD_HUMIDITY            5U
#define VPIN_BME_TEMPERATURE         6U
#define VPIN_BME_HUMIDITY            7U
#define VPIN_BME_PRESSURE            8U
#define VPIN_GAS_RESISTANCE          9U
#define VPIN_GAS_RATIO               10U
#define VPIN_GAS_ALARM               11U
#define VPIN_OUTSIDE_TEMP            12U
#define VPIN_OUTSIDE_FEELS           13U
#define VPIN_OUTSIDE_HIGH            14U
#define VPIN_OUTSIDE_LOW             15U
#define VPIN_WEATHER_THEME           16U

#endif /* APP_CONFIG_H */
