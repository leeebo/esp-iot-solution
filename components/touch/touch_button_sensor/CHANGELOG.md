# ChangeLog

## v0.3.0 - 2026-02-14

* **Deep Refactor**: Complete transition to unified single-FSM multi-frequency architecture
* **API Modernization**:
    * Simplified `touch_button_config_t` with absolute unscaled thresholds and explicit debounce counts
    * Removed frequency-specific data access in favor of FSM output
    * Deleted obsolete `touch_button_sensor_get_state_bitmap` API
* **Target Optimization**: Removed legacy ESP32 support (focusing on modern Targets)
* **Configuration Cleanup**: Overhauled `Kconfig` to remove parameter tuning, facilitating PC debugging and runtime control
* **Standardized Debug Logging**: Unified CSV output using FSM macros (vl, ch, etc.)

## v0.2.2 - 2025-06-13

* update touch_sensor_fsm
* improve maximum minimum value handling
* improve esp32-p4 stability

## v0.2.1 - 2025-03-28

* update touch_sensor_fsm to 0.7.0
* improve default configuration for different targets

## v0.2.0 - 2025-03-25

* Using new touch touch_sensor_fsm 0.6.*

## v0.1.0 - 2025-03-12

* Add Initial version
