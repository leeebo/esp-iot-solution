# Touch Button Sensor

**Note:** This component is for developers testing only. It is not intended for production use.

A component providing enhanced touch button detection functionality for ESP32 series chips. It uses multiple frequency sampling (for ESP32-P4) and FSM-based processing to provide reliable touch detection even in noisy environments.

## Features

- Unified multi-frequency touch sampling for superior noise immunity
- Single-FSM architecture with internal data fusion and state processing
- Permille-based coefficient configuration and unscaled parameter handling
- Efficient debounce and anomaly monitoring (Cover/Negative reset)
- Callback-based event notification for touch state changes

## Dependencies

- [touch_sensor_fsm](https://components.espressif.com/components/espressif/touch_sensor_fsm)
- [touch_sensor_lowlevel](https://components.espressif.com/components/espressif/touch_sensor_lowlevel)
