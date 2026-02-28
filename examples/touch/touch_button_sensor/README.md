# Touch Button Sensor Example

This example demonstrates how to use the `touch_button_sensor` component to implement high-performance touch button functionality on modern ESP32 series chips (optimized for ESP32-P4).

The `touch_button_sensor` is an FSM-based touch sensor driver that provides a flexible, software-driven solution with superior noise immunity compared to traditional hardware-only methods.

## Overview

The component provides a high-level abstraction for capacitive touch buttons, handling:

- **FSM-based Detection**: Reliable state changes using a single-FSM multi-frequency architecture.
- **Absolute Thresholds**: Simple unscaled absolute value thresholds for direct sensitivity control.
- **Fused Data Output**: Combined signal output from multiple frequencies for cleaner detection.
- **Debounce Filtering**: Independent active and inactive debounce counts.

## Hardware Required

* A supported ESP32 development board (e.g., ESP32-P4 Function EV Board).
* Touch pads or electrodes connected to touch-enabled GPIO pins.
* A USB cable for serial monitoring and programming.

## How to Use Example

### Configuration

Most behavioral parameters are now tuned directly in the application code to facilitate runtime adjustment and PC tool debugging. The component's `Kconfig` is simplified to core switches.

1.  **Debug Mode**: Enable `TOUCH_BUTTON_SENSOR_DEBUG` in `idf.py menuconfig` to see detailed FSM logs in CSV format.
2.  **API Config**: Adjust parameters in `main/touch_button_sensor_main.c`:
    - `channel_threshold`: Unscaled absolute thresholds (e.g., `200`).
    - `debounce_active` / `debounce_inactive`: Sequential trigger counts.

### Build and Flash

```bash
idf.py set-target esp32p4  # Or your target
idf.py build
idf.py flash monitor
```

## Example Output

The example prints the current data and threshold for each channel in the main loop. If `DEBUG` mode is enabled, it also outputs structured CSV lines for PC plotting tools.

```text
Channel 1 data: 45, threshold: 200    Channel 2 data: 38, threshold: 200 ...
Channel 1 data: 42, threshold: 200    Channel 2 data: 40, threshold: 200 ...
I (2450) touch_button_sensor: Channel 1 is Active
Channel 1 data: 310, threshold: 200   Channel 2 data: 41, threshold: 200 ...
```

## Implementation Details

1.  **Initialization**: The `touch_button_config_t` is set up with channel lists and absolute thresholds.
2.  **FSM Marriage**: The component automatically initializes the underlying `touch_sensor_fsm` and `touch_sensor_lowlevel` units.
3.  **Event Handling**: A dedicated task calls `touch_button_sensor_handle_events()` periodically to process FSM state transitions and trigger callbacks.
4.  **Tuning**: Use the `vl` (Value Log) output with PC tools like `tptool` to visualize raw/smooth/fused data for precise threshold adjustment.

## Additional Notes

- In most usage scenarios, only the threshold needs to be adjusted. If an "EMS Fail" case occurs, a debugging tool should be used to adjust the internal detailed parameters.