# ICM20689 WHO_AM_I Diagnosis

Task: t_cea704cf (child of t_3c4330fd)
Board: CUAV V5 (FMUv5, STM32F767)
Target chip: ICM20689 on SPI1
Expected WHO_AM_I: 0x98
Actual reads: ~0x48, 0x5E, 0x58

## Plan
1. Check CUAV V5 hardware definition — SPI1 config, CS pin, chip select order
2. Check other ArduPilot board defs for ICM20689 WHO_AM_I patterns
3. Check ChibiOS reference for what CUAV V5 actually detects
4. Check SPI driver setup in RTT port
5. Use GDB to probe WHO_AM_I directly
6. Consider ICM-42688 substitution (WHO_AM_I=0x47)
