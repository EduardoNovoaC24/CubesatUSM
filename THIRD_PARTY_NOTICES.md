# Third-party notices

## desertkun/MPU9250

`Core/Src/mpu9250.c` and `Core/Inc/mpu9250.h` are adapted from
[desertkun/MPU9250](https://github.com/desertkun/MPU9250), commit
`4d98a485df9101856ba122971afb1be073bf1300`.

Upstream is distributed under the WTFPL version 2. The local adaptation keeps
the register setup while changing the transport to I2C, with explicit STM32 HAL
contexts, error propagation, AK8963 status validation, and physical-unit conversion.
