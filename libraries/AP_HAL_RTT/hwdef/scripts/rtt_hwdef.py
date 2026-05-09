#!/usr/bin/env python3
"""
RT-Thread hwdef script for ArduPilot + AP_HAL_RTT.
Inherits from HWDef base class to share IMU/BARO/MAG probe generation.
Enhanced version: parses full pin definitions (UART/PWM/I2C/SPI/GPIO/ADC)
and generates:
  1. hwdef.h          — all macro definitions (SPI table, PWM map, UART map, etc.)
  2. rt_pin_config.c  — generated HAL MSP init (UART/SPI/PWM/SD/USB GPIO setup)
  3. rtconfig.h       — RTT peripheral enables based on hwdef.dat
  4. link.lds         — memory layout from FLASH_SIZE_KB / RAM_SIZE_KB

Usage: rtt_hwdef.py [-D outdir] [--params params] hwdef.dat
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.realpath(__file__)),
                                '..', '..', '..', '..', 'libraries', 'AP_HAL', 'hwdef', 'scripts'))
from hwdef import HWDef  # noqa: E402


def _load_board_types(script_dir):
    bt = {}
    p = os.path.normpath(os.path.join(script_dir, '..', '..', '..', '..', 'Tools', 'AP_Bootloader', 'board_types.txt'))
    if os.path.isfile(p):
        for line in open(p):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split()
            if len(parts) >= 2 and parts[0].startswith('TARGET_HW_'):
                try:
                    bt[parts[0]] = int(parts[1])
                except ValueError:
                    pass
    return bt


def _parse_speed(s):
    """Parse speed string like '8*MHZ' or '1000000' to integer Hz."""
    s = s.strip().upper()
    m = re.match(r'^(\d+)\s*\*\s*MHZ$', s)
    if m:
        return int(m.group(1)) * 1000000
    m = re.match(r'^(\d+)\s*\*\s*KHZ$', s)
    if m:
        return int(m.group(1)) * 1000
    try:
        return int(s)
    except ValueError:
        return 1000000


def _spi_mode_num(s):
    """Convert MODE0..MODE3 to integer 0..3."""
    s = s.strip().upper()
    return {'MODE0': 0, 'MODE1': 1, 'MODE2': 2, 'MODE3': 3}.get(s, 3)


def _pin_to_rtt(pin_str):
    """Convert 'PE9' or 'PA10' to (port_letter, pin_num) and RTT GET_PIN macro."""
    m = re.match(r'^P([A-Z])(\d+)$', pin_str.strip().upper())
    if not m:
        return None
    port = m.group(1)
    pin = int(m.group(2))
    rtt_pin = (ord(port) - ord('A')) * 16 + pin
    return (port, pin, rtt_pin)


def _af_num(af_str):
    """Parse 'AF7' → 7, 'AF10' → 10."""
    m = re.match(r'^AF(\d+)$', af_str.strip().upper())
    return int(m.group(1)) if m else 0


def _gpio_port_base(port):
    """Return GPIO base address string for a port letter: 'A' → 'GPIOA'."""
    return 'GPIO' + port.upper()


def _gpio_pin_mask(pin_num):
    """Return GPIO_PIN_x macro for a pin number."""
    return 'GPIO_PIN_%d' % pin_num


# === STM32 AF mapping tables ===
# Map peripheral signal → AF number for STM32F7
# This allows auto-resolving AF when not specified in hwdef.dat
STM32F7_AF_MAP = {
    'USART1': 7, 'USART2': 7, 'USART3': 7, 'UART4': 8, 'UART5': 8,
    'UART7': 8, 'UART8': 8,
    'SPI1': 5, 'SPI2': 5, 'SPI3': 6, 'SPI4': 5, 'SPI5': 5, 'SPI6': 5,
    'I2C1': 4, 'I2C2': 4, 'I2C3': 4, 'I2C4': 4,
    'TIM1': 1, 'TIM2': 1, 'TIM3': 2, 'TIM4': 2, 'TIM5': 2,
    'TIM8': 3, 'TIM9': 3, 'TIM10': 3, 'TIM11': 3,
    'TIM12': 9, 'TIM13': 9, 'TIM14': 9,
    'SDMMC1': 12, 'SDMMC2': 12,
    'OTG1': 10, 'OTG2': 10,
}


class RTTHWDef(HWDef):
    def __init__(self, quiet=False, outdir=None, hwdef=[]):
        super().__init__(quiet=quiet, outdir=outdir, hwdef=hwdef)
        self.spidev = []
        self.pin_labels = {}
        self.user_defines = []
        self.romfs = {}

        # Pin definitions by type
        self.uart_pins = {}       # periph → [{'pin': 'PD8', 'signal': 'USART3_TX', 'af': 7, 'port': 'D', 'pin_num': 8}]
        self.spi_bus_pins = {}    # bus → {'SCK': ..., 'MISO': ..., 'MOSI': ...}
        self.i2c_pins = {}        # bus → {'SCL': ..., 'SDA': ...}
        self.pwm_pins = []        # [{'pin': 'PE9', 'timer': 'TIM1', 'ch': 'CH1', 'af': 1, 'pwm_num': 1, 'port': 'E', 'pin_num': 9}]
        self.gpio_out = []        # [{'pin': 'PE3', 'label': 'VDD_3V3_SENSORS_EN', 'init': 'HIGH', 'port': 'E', 'pin_num': 3}]
        self.adc_pins = []        # [{'pin': 'PA0', 'label': 'BATT_VOLTAGE_SENS', 'adc': 'ADC1', 'scale': 1, 'port': 'A', 'pin_num': 0}]
        self.sd_pins = {}         # periph → list of pin defs
        self.usb_pins = {}        # periph → list of pin defs

        self.board_types = _load_board_types(os.path.dirname(os.path.abspath(__file__)))

        # Config values
        self.ram_size_kb = 0
        self.ram_base = 0x20000000
        self.mcu_family = ''

    def process_line(self, line, depth=0):
        """Extend base class to handle all pin types and config keys."""
        import shlex
        a = shlex.split(line, posix=False)
        if not a:
            return

        # --- SPIDEV ---
        if a[0] == 'SPIDEV' and len(a) >= 7:
            self.spidev.append(a[1:])
            return

        # --- CS pin: PF2 ICM20689_CS CS ---
        if len(a) >= 3 and a[0].startswith('P') and len(a[0]) >= 3 and a[0][1].isalpha() and a[2] == 'CS':
            p = _pin_to_rtt(a[0])
            if p:
                self.pin_labels[a[1]] = (p[0], p[1])
            return

        # --- GPIO output: PE3 VDD_3V3_SENSORS_EN OUTPUT HIGH ---
        if len(a) >= 4 and a[0].startswith('P') and a[2] == 'OUTPUT':
            p = _pin_to_rtt(a[0])
            if p:
                self.gpio_out.append({
                    'pin': a[0], 'label': a[1], 'init': a[3],
                    'port': p[0], 'pin_num': p[1], 'rtt_pin': p[2]
                })
            return

        # --- ADC: PA0 BATT_VOLTAGE_SENS ADC1 SCALE(1) ---
        if len(a) >= 4 and a[0].startswith('P') and a[2].startswith('ADC'):
            m = re.match(r'SCALE\((\d+)\)', a[3])
            scale = int(m.group(1)) if m else 1
            p = _pin_to_rtt(a[0])
            if p:
                self.adc_pins.append({
                    'pin': a[0], 'label': a[1], 'adc': a[2], 'scale': scale,
                    'port': p[0], 'pin_num': p[1], 'rtt_pin': p[2]
                })
            return

        # --- Generic pin: PD8 USART3_TX USART3 AF7 [PWM(n)] ---
        # Also accepts hwdef lines like:
        #   PH7 I2C3_SCL AF4 I2C
        # where the bus number must be derived from the signal name.
        if len(a) >= 3 and a[0].startswith('P') and len(a[0]) >= 3 and a[0][1].isalpha():
            pin_str = a[0]
            signal = a[1]
            periph = a[2]
            af = None
            pwm_num = None

            # Some hwdef lines encode AF before the peripheral class, e.g.
            # "PH7 I2C3_SCL AF4 I2C". Normalize that form first.
            if re.match(r'^AF\d+$', periph, re.I) and len(a) >= 4:
                af = _af_num(periph)
                periph = a[3]

            # When the peripheral token is generic ("I2C", "SPI", "TIM", ...)
            # derive the concrete bus/timer name from the signal label.
            sig_match = re.match(r'^((?:USART|UART|SPI|I2C|TIM|SDMMC|OTG)\d+)_', signal, re.I)
            if sig_match:
                derived_periph = sig_match.group(1).upper()
                if periph.upper() in ('USART', 'UART', 'SPI', 'I2C', 'TIM', 'SDMMC', 'OTG'):
                    periph = derived_periph

            for token in a[3:]:
                m = re.match(r'^AF(\d+)$', token, re.I)
                if m:
                    af = int(m.group(1))
                m = re.match(r'^PWM\((\d+)\)$', token, re.I)
                if m:
                    pwm_num = int(m.group(1))

            p = _pin_to_rtt(pin_str)
            if not p:
                return

            pin_info = {
                'pin': pin_str, 'signal': signal, 'periph': periph,
                'af': af, 'port': p[0], 'pin_num': p[1], 'rtt_pin': p[2]
            }

            # Classify by peripheral type
            periph_upper = periph.upper()

            if periph_upper.startswith('USART') or periph_upper.startswith('UART'):
                if periph_upper not in self.uart_pins:
                    self.uart_pins[periph_upper] = []
                self.uart_pins[periph_upper].append(pin_info)

            elif periph_upper.startswith('SPI'):
                if periph_upper not in self.spi_bus_pins:
                    self.spi_bus_pins[periph_upper] = {}
                sig_upper = signal.upper()
                if 'SCK' in sig_upper:
                    self.spi_bus_pins[periph_upper]['SCK'] = pin_info
                elif 'MISO' in sig_upper:
                    self.spi_bus_pins[periph_upper]['MISO'] = pin_info
                elif 'MOSI' in sig_upper:
                    self.spi_bus_pins[periph_upper]['MOSI'] = pin_info

            elif periph_upper.startswith('I2C'):
                if periph_upper not in self.i2c_pins:
                    self.i2c_pins[periph_upper] = {}
                sig_upper = signal.upper()
                if 'SCL' in sig_upper:
                    self.i2c_pins[periph_upper]['SCL'] = pin_info
                elif 'SDA' in sig_upper:
                    self.i2c_pins[periph_upper]['SDA'] = pin_info

            elif periph_upper.startswith('TIM'):
                if pwm_num is not None:
                    ch_match = re.search(r'CH(\d+)', signal, re.I)
                    ch_num = int(ch_match.group(1)) if ch_match else 0
                    pin_info['pwm_num'] = pwm_num
                    pin_info['ch_num'] = ch_num
                    self.pwm_pins.append(pin_info)

            elif periph_upper.startswith('SDMMC'):
                if periph_upper not in self.sd_pins:
                    self.sd_pins[periph_upper] = []
                self.sd_pins[periph_upper].append(pin_info)

            elif periph_upper.startswith('OTG'):
                if periph_upper not in self.usb_pins:
                    self.usb_pins[periph_upper] = []
                self.usb_pins[periph_upper].append(pin_info)

            return

        # --- Config keys ---
        rtt_keys = {'BOARD_NAME', 'MCU', 'FLASH_SIZE_KB', 'OSCILLATOR_HZ',
                     'SERIAL_ORDER', 'APJ_BOARD_ID', 'FLASH_RESERVE_START_KB',
                     'FLASH_BOOTLOADER_LOAD_KB', 'FLASH_RESERVE_END_KB',
                     'RAM_SIZE_KB', 'RAM_BASE', 'IOMCU_UART'}
        if a[0] in rtt_keys:
            self.config[a[0]] = a[1:]
            if a[0] == 'RAM_SIZE_KB' and len(a) > 1:
                try:
                    self.ram_size_kb = int(a[1])
                except ValueError:
                    pass
            if a[0] == 'RAM_BASE' and len(a) > 1:
                try:
                    self.ram_base = int(a[1], 0)
                except ValueError:
                    pass
            if a[0] == 'MCU' and len(a) > 1:
                self.mcu_family = a[1].upper()  # e.g., STM32F7XX
            return

        if a[0] == 'define' and len(a) >= 2:
            self.user_defines.append(a[1:])

        if a[0] == 'ROMFS' and len(a) >= 3:
            self.romfs[a[1]] = a[2]
            return

        super().process_line(line, depth)

    def get_config(self, key, default=''):
        v = self.config.get(key, [])
        return ' '.join(v) if v else default

    def get_config_int(self, key, default=0):
        s = self.get_config(key, '').strip()
        if not s:
            return default
        try:
            return int(s, 0)
        except ValueError:
            return default

    # ===================== hwdef.h generation =====================

    def write_hwdef_header_content(self, f):
        """Generate comprehensive hwdef.h."""
        board = self.get_config('BOARD_NAME', 'unknown')
        mcu_tokens = self.config.get('MCU', [])
        flash_kb = self.get_config_int('FLASH_SIZE_KB', 0)
        osc_hz = self.get_config_int('OSCILLATOR_HZ', 0)
        serial_order = self.config.get('SERIAL_ORDER', [])
        apj_id = self.get_config('APJ_BOARD_ID', '')

        f.write('/* RT-Thread + AP_HAL_RTT board configuration — auto-generated */\n')
        f.write('/* Do not edit — generated from hwdef.dat by rtt_hwdef.py */\n\n')

        # SEEK_* constants for RT-Thread/newlib compatibility
        f.write('#ifndef SEEK_SET\n#define SEEK_SET 0\n#endif\n')
        f.write('#ifndef SEEK_CUR\n#define SEEK_CUR 1\n#endif\n')
        f.write('#ifndef SEEK_END\n#define SEEK_END 2\n#endif\n\n')

        # GNU libc extensions missing from newlib — declarations + compat shim
        f.write('/* GNU libc extensions for RT-Thread/newlib */\n')
        f.write('#ifdef __cplusplus\n')
        f.write('#include <cstddef>\n')
        f.write('extern "C" int ffs(int);\n')
        f.write('extern "C" int asprintf(char **, const char *, ...);\n')
        f.write('extern "C" void *memmem(const void *, std::size_t, const void *, std::size_t);\n')
        f.write('extern "C" std::size_t strnlen(const char *s, std::size_t maxlen);\n')
        f.write('extern "C" char *strdup(const char *s);\n')
        f.write('#endif\n\n')

        # Sensor probe macros
        f.write('/* Sensor probe macros */\n')
        f.write('#define PROBE_IMU_SPI(driver, devname, args ...) ADD_BACKEND(AP_InertialSensor_ ## driver::probe(*this,hal.spi->get_device(devname),##args))\n')
        f.write('#define PROBE_IMU_SPI2(driver, devname1, devname2, args ...) ADD_BACKEND(AP_InertialSensor_ ## driver::probe(*this,hal.spi->get_device(devname1),hal.spi->get_device(devname2),##args))\n')
        f.write('#define PROBE_IMU_I2C(driver, bus, addr, args ...) ADD_BACKEND(AP_InertialSensor_ ## driver::probe(*this,GET_I2C_DEVICE(bus,addr),##args))\n')
        f.write('#define PROBE_BARO_SPI(driver, devname, args ...) ADD_BACKEND(AP_Baro_ ## driver::probe(*this,hal.spi->get_device(devname),##args))\n')
        f.write('#define PROBE_BARO_I2C(driver, bus, addr, args ...) ADD_BACKEND(AP_Baro_ ## driver::probe(*this,std::move(GET_I2C_DEVICE(bus,addr)),##args))\n')
        f.write('#define PROBE_MAG_SPI(driver, devname, args ...) ADD_BACKEND(DRIVER_ ## driver, AP_Compass_ ## driver::probe(hal.spi->get_device(devname),##args))\n')
        f.write('#define PROBE_MAG_I2C(driver, bus, addr, args ...) ADD_BACKEND(DRIVER_ ## driver, AP_Compass_ ## driver::probe(GET_I2C_DEVICE(bus,addr),##args))\n\n')

        # Board name
        f.write('#define BOARD_NAME "%s"\n\n' % board.replace('"', '\\"'))

        # MCU defines
        if mcu_tokens:
            for t in mcu_tokens:
                f.write('#define HAL_MCU_%s 1\n' % t.upper().replace('.', '_'))
            f.write('\n')

        # Flash layout
        if flash_kb > 0:
            f.write('#define FLASH_SIZE_KB %d\n' % flash_kb)
        flash_reserve_start = self.get_config_int('FLASH_RESERVE_START_KB', 0)
        flash_reserve_end = self.get_config_int('FLASH_RESERVE_END_KB', 0)
        flash_bl_load = self.get_config_int('FLASH_BOOTLOADER_LOAD_KB', 0)
        if flash_reserve_start > 0:
            f.write('#define FLASH_RESERVE_START_KB %d\n' % flash_reserve_start)
        flash_origin = 0x08000000 + flash_reserve_start * 1024
        flash_length_kb = flash_kb - flash_reserve_start - flash_reserve_end if flash_kb > 0 else 0
        f.write('#define FLASH_ORIGIN 0x%08x\n' % flash_origin)
        f.write('#define FLASH_LENGTH_KB %d\n' % flash_length_kb)
        f.write('\n')

        # RAM layout
        if self.ram_size_kb > 0:
            f.write('#define RAM_SIZE_KB %d\n' % self.ram_size_kb)
            f.write('#define RAM_BASE 0x%08x\n' % self.ram_base)
            f.write('\n')

        # Oscillator
        if osc_hz > 0:
            f.write('#define OSCILLATOR_HZ %d\n\n' % osc_hz)

        # Serial ports — build complete device list first (including IOMCU UART)
        has_iomcu = 'IOMCU_UART' in self.config
        device_names = []
        for name in serial_order:
            if name.upper().startswith('OTG'):
                device_names.append('"usb-acm0"')
            elif 'UART' in name.upper() or 'USART' in name.upper():
                m = re.search(r'(\d+)', name)
                device_names.append('"uart%s"' % (m.group(1) if m else '1'))
            else:
                device_names.append('"uart1"')

        # IOMCU UART appended as last serial port
        iomcu_uart_num = None
        if has_iomcu:
            iomcu_uart_name = self.config['IOMCU_UART'][0]
            m = re.search(r'(\d+)', iomcu_uart_name)
            if m:
                iomcu_uart_num = m.group(1)
                device_names.append('"uart%s"' % iomcu_uart_num)
            else:
                self.error("IOMCU_UART '%s' has no UART number" % iomcu_uart_name)

        total_serial = len(device_names)
        f.write('#define SERIAL_PORT_COUNT %d\n' % total_serial)

        if serial_order:
            for i, name in enumerate(serial_order):
                f.write('#define SERIAL_PORT_%d_NAME %s\n' % (i, name))
            first = serial_order[0]
            if first.upper().startswith('OTG'):
                f.write('#define HAL_RTT_SERIAL0_OTG 1\n')
                f.write('#define HAL_OTG1_CONFIG 1\n')
            else:
                f.write('#define HAL_RTT_SERIAL0_OTG 0\n')
        else:
            f.write('#define HAL_RTT_SERIAL0_OTG 0\n')

        f.write('#define HAL_RTT_UART_DEVICE_LIST %s\n' % ', '.join(device_names))

        # IOMCU defines
        if has_iomcu and iomcu_uart_num is not None:
            iomcu_idx = len(serial_order)  # appended after regular serial ports
            f.write('#define HAL_UART_IOMCU_IDX %u\n' % iomcu_idx)
            f.write('#define HAL_WITH_IO_MCU 1\n')
            f.write('#define HAL_HAVE_SERVO_VOLTAGE 1\n')
            f.write('#define AP_FEATURE_SBUS_OUT 1\n')
        else:
            f.write('#define HAL_WITH_IO_MCU 0\n')
        f.write('\n')

        # Board ID
        if apj_id:
            if apj_id in self.board_types:
                f.write('#define %s %d\n' % (apj_id, self.board_types[apj_id]))
            f.write('#define APJ_BOARD_ID %s\n\n' % apj_id)

        # SPI device table
        self._write_spi_table(f)

        # PWM channel map (generated from hwdef.dat)
        self._write_pwm_map(f)

        # GPIO output defines
        self._write_gpio_defines(f)

        # ADC defines
        self._write_adc_defines(f)

        # IMU/BARO/MAG probe lists
        self.write_IMU_config(f)
        self.write_BARO_config(f)
        self.write_MAG_config(f)

        # User-defined macros
        self._write_user_defines(f)
        f.write('\n')

    def _write_spi_table(self, f):
        """Generate SPI device table and attach list."""
        if not self.spidev:
            return
        f.write('/* SPI device table — generated from SPIDEV lines */\n')
        devlist = []
        attach_entries = []
        for idx, dev in enumerate(self.spidev):
            if len(dev) < 6:
                continue
            name = dev[0]
            bus_str = dev[1]
            devid_str = dev[2]
            cs_label = dev[3]
            mode_str = dev[4]
            lowspeed_str = dev[5]
            highspeed_str = dev[6] if len(dev) > 6 else dev[5]
            bus_num = int(re.search(r'(\d+)', bus_str).group(1))
            devid_num = int(re.search(r'(\d+)', devid_str).group(1))
            mode_num = _spi_mode_num(mode_str)
            lowspeed = _parse_speed(lowspeed_str)
            highspeed = _parse_speed(highspeed_str)
            rtt_devname = 'spi%d%d' % (bus_num, devid_num)
            rtt_busname = 'spi%d' % bus_num
            cs_port = '?'
            cs_pin = 0
            if cs_label in self.pin_labels:
                cs_port, cs_pin = self.pin_labels[cs_label]
            macro_name = 'HAL_SPI_DEVICE%d' % idx
            f.write('#define %s {"%s", "%s", %d, %d, %d, %dU, %dU}\n' %
                    (macro_name, name, rtt_devname, bus_num, devid_num,
                     mode_num, lowspeed, highspeed))
            devlist.append(macro_name)
            if cs_port != '?':
                attach_entries.append('    {"%s", "%s", GET_PIN(%s, %d)}' %
                                      (rtt_busname, rtt_devname, cs_port, cs_pin))
        f.write('#define HAL_SPI_DEVICE_LIST %s\n' % ', '.join(devlist))
        f.write('#define HAL_SPI_DEVICE_COUNT %d\n\n' % len(devlist))
        if attach_entries:
            f.write('/* SPI device attach table */\n')
            f.write('#define HAL_RTT_SPI_ATTACH_LIST \\\n')
            f.write(', \\\n'.join(attach_entries))
            f.write('\n\n')
            # Numeric pin values version (no STM32 HAL GPIO defines needed)
            f.write('/* SPI device attach table — numeric pin values */\n')
            f.write('#define HAL_RTT_SPI_ATTACH_VALUES \\\n')
            val_entries = []
            for dev in self.spidev:
                if len(dev) < 6:
                    continue
                bus_num2 = int(re.search(r'(\d+)', dev[1]).group(1))
                devid_num2 = int(re.search(r'(\d+)', dev[2]).group(1))
                cs_label2 = dev[3]
                rtt_busname2 = 'spi%d' % bus_num2
                rtt_devname2 = 'spi%d%d' % (bus_num2, devid_num2)
                if cs_label2 in self.pin_labels:
                    cs_port2, cs_pin2 = self.pin_labels[cs_label2]
                    port_idx = ord(cs_port2) - ord('A')
                    pin_val = 16 * port_idx + cs_pin2
                    val_entries.append('    {"%s", "%s", %d}' %
                                       (rtt_busname2, rtt_devname2, pin_val))
            f.write(', \\\n'.join(val_entries))
            f.write('\n\n')

    def _write_pwm_map(self, f):
        """Generate PWM channel map from hwdef.dat pin definitions."""
        if not self.pwm_pins:
            return
        # Sort by PWM channel number
        sorted_pins = sorted(self.pwm_pins, key=lambda p: p['pwm_num'])
        f.write('/* PWM channel map — generated from PWM pin definitions */\n')
        f.write('/* Each entry: RTT PWM device name, timer channel (1-based) */\n')
        entries = []
        for p in sorted_pins:
            timer_num = re.search(r'(\d+)', p['periph']).group(1)
            rtt_dev = '"pwm%s"' % timer_num
            entries.append('    {%s, %d}' % (rtt_dev, p['ch_num']))
        f.write('#define HAL_RTT_PWM_MAP_COUNT %d\n' % len(entries))
        f.write('#define HAL_RTT_PWM_MAP { \\\n')
        f.write(', \\\n'.join(entries))
        f.write(' }\n\n')

    def _write_gpio_defines(self, f):
        """Generate GPIO output pin defines."""
        if not self.gpio_out:
            return
        f.write('/* GPIO output pins — generated from OUTPUT definitions */\n')
        for g in self.gpio_out:
            label = g['label']
            f.write('#define HAL_GPIO_%s_PIN   GET_PIN(%s, %d)\n' % (label, g['port'], g['pin_num']))
            # Numeric pin value for use without STM32 HAL headers (e.g. HAL_RTT_Class.cpp)
            port_idx = ord(g['port']) - ord('A')
            pin_val = 16 * port_idx + g['pin_num']
            f.write('#define HAL_GPIO_%s_VALUE %d\n' % (label, pin_val))
            f.write('#define HAL_GPIO_%s_INIT  %d\n' % (label, 1 if g['init'] == 'HIGH' else 0))
        f.write('\n')

    def _write_adc_defines(self, f):
        """Generate ADC pin defines."""
        if not self.adc_pins:
            return
        f.write('/* ADC input pins — generated from ADC definitions */\n')
        for a in self.adc_pins:
            label = a['label']
            f.write('#define HAL_ADC_%s_PIN   GET_PIN(%s, %d)\n' % (label, a['port'], a['pin_num']))
            f.write('#define HAL_ADC_%s_SCALE %d\n' % (label, a['scale']))
        f.write('\n')

    def _write_user_defines(self, f):
        """Write user-defined macros."""
        for parts in self.user_defines:
            name = parts[0]
            value = ' '.join(parts[1:]) if len(parts) > 1 else ''
            if value:
                f.write('#define %s %s\n' % (name, value))
            else:
                f.write('#define %s\n' % name)

    # ===================== rt_pin_config.c generation =====================

    def write_pin_config_c(self, outdir):
        """Generate rt_pin_config.c with HAL MSP init functions."""
        path = os.path.join(outdir, 'rt_pin_config.c')
        with open(path, 'w') as f:
            f.write('/*\n')
            f.write(' * rt_pin_config.c — auto-generated by rtt_hwdef.py\n')
            f.write(' * HAL MSP Initialization for UART, SPI, TIM (PWM), SD, USB\n')
            f.write(' * DO NOT EDIT — changes will be overwritten.\n')
            f.write(' */\n\n')
            f.write('#include "main.h"\n')
            f.write('#include <drv_common.h>\n\n')

            # HAL_MspInit
            f.write('void HAL_MspInit(void)\n{\n')
            f.write('    __HAL_RCC_PWR_CLK_ENABLE();\n')
            f.write('    __HAL_RCC_SYSCFG_CLK_ENABLE();\n')
            f.write('}\n\n')

            # UART MSP
            if self.uart_pins:
                self._gen_uart_msp(f)
            # SPI MSP
            if self.spi_bus_pins:
                self._gen_spi_msp(f)
            # TIM PWM MSP
            if self.pwm_pins:
                self._gen_pwm_msp(f)
            # SD MSP
            if self.sd_pins:
                self._gen_sd_msp(f)
            # USB MSP
            if self.usb_pins:
                self._gen_usb_msp(f)

        print("Generated: %s" % path)

    def _gen_uart_msp(self, f):
        """Generate HAL_UART_MspInit from UART pin definitions."""
        f.write('void HAL_UART_MspInit(UART_HandleTypeDef* huart)\n{\n')
        f.write('    GPIO_InitTypeDef GPIO_InitStruct = {0};\n\n')

        for periph, pins in self.uart_pins.items():
            # Group pins by port for combined init
            port_pins = {}
            for p in pins:
                port = p['port']
                if port not in port_pins:
                    port_pins[port] = []
                port_pins[port].append(p)

            f.write('    if (huart->Instance == %s) {\n' % periph)
            f.write('        __HAL_RCC_%s_CLK_ENABLE();\n' % periph)

            # Enable GPIO port clocks
            ports_seen = set()
            for p in pins:
                if p['port'] not in ports_seen:
                    f.write('        __HAL_RCC_GPIO%s_CLK_ENABLE();\n' % p['port'])
                    ports_seen.add(p['port'])

            af = pins[0]['af'] if pins[0]['af'] else STM32F7_AF_MAP.get(periph, 0)
            f.write('        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;\n')
            f.write('        GPIO_InitStruct.Pull = GPIO_PULLUP;\n')
            f.write('        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;\n')
            f.write('        GPIO_InitStruct.Alternate = GPIO_AF%d_%s;\n' % (af, periph))

            for port, pp in port_pins.items():
                pin_mask = ' | '.join(['GPIO_PIN_%d' % p['pin_num'] for p in pp])
                f.write('        GPIO_InitStruct.Pin = %s;\n' % pin_mask)
                f.write('        HAL_GPIO_Init(GPIO%s, &GPIO_InitStruct);\n' % port)

            f.write('    }\n')

        f.write('}\n\n')

        # DeInit
        f.write('void HAL_UART_MspDeInit(UART_HandleTypeDef* huart)\n{\n')
        for periph, pins in self.uart_pins.items():
            f.write('    if (huart->Instance == %s) {\n' % periph)
            f.write('        __HAL_RCC_%s_CLK_DISABLE();\n' % periph)
            port_pins = {}
            for p in pins:
                port = p['port']
                if port not in port_pins:
                    port_pins[port] = []
                port_pins[port].append(p)
            for port, pp in port_pins.items():
                pin_mask = ' | '.join(['GPIO_PIN_%d' % p['pin_num'] for p in pp])
                f.write('        HAL_GPIO_DeInit(GPIO%s, %s);\n' % (port, pin_mask))
            f.write('    }\n')
        f.write('}\n\n')

    def _gen_spi_msp(self, f):
        """Generate HAL_SPI_MspInit from SPI bus pin definitions."""
        f.write('void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)\n{\n')
        f.write('    GPIO_InitTypeDef GPIO_InitStruct = {0};\n\n')

        for bus, pins in self.spi_bus_pins.items():
            if not pins:
                continue
            f.write('    if (hspi->Instance == %s) {\n' % bus)
            f.write('        __HAL_RCC_%s_CLK_ENABLE();\n' % bus)

            # Enable GPIO clocks
            ports_seen = set()
            for sig_name, p in pins.items():
                if p['port'] not in ports_seen:
                    f.write('        __HAL_RCC_GPIO%s_CLK_ENABLE();\n' % p['port'])
                    ports_seen.add(p['port'])

            af = list(pins.values())[0]['af'] if list(pins.values())[0]['af'] else STM32F7_AF_MAP.get(bus, 5)
            f.write('        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;\n')
            f.write('        GPIO_InitStruct.Pull = GPIO_NOPULL;\n')
            f.write('        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;\n')
            f.write('        GPIO_InitStruct.Alternate = GPIO_AF%d_%s;\n' % (af, bus))

            for sig_name, p in pins.items():
                f.write('        GPIO_InitStruct.Pin = GPIO_PIN_%d;\n' % p['pin_num'])
                f.write('        HAL_GPIO_Init(GPIO%s, &GPIO_InitStruct);\n' % p['port'])
            f.write('    }\n')

        f.write('}\n\n')

    def _gen_pwm_msp(self, f):
        """Generate HAL_TIM_MspPostInit for PWM outputs."""
        f.write('void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim)\n{\n')
        f.write('    GPIO_InitTypeDef GPIO_InitStruct = {0};\n\n')

        # Group by timer
        timer_pins = {}
        for p in self.pwm_pins:
            timer = p['periph']
            if timer not in timer_pins:
                timer_pins[timer] = []
            timer_pins[timer].append(p)

        for timer, pins in timer_pins.items():
            af = pins[0]['af'] if pins[0]['af'] else STM32F7_AF_MAP.get(timer.upper(), 1)
            f.write('    if (htim->Instance == %s) {\n' % timer)
            f.write('        __HAL_RCC_%s_CLK_ENABLE();\n' % timer)

            ports_seen = set()
            for p in pins:
                if p['port'] not in ports_seen:
                    f.write('        __HAL_RCC_GPIO%s_CLK_ENABLE();\n' % p['port'])
                    ports_seen.add(p['port'])

            f.write('        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;\n')
            f.write('        GPIO_InitStruct.Pull = GPIO_NOPULL;\n')
            f.write('        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;\n')
            f.write('        GPIO_InitStruct.Alternate = GPIO_AF%d_%s;\n' % (af, timer))

            # Group by port
            port_groups = {}
            for p in pins:
                if p['port'] not in port_groups:
                    port_groups[p['port']] = []
                port_groups[p['port']].append(p)

            for port, pp in port_groups.items():
                pin_mask = ' | '.join(['GPIO_PIN_%d' % p['pin_num'] for p in pp])
                f.write('        GPIO_InitStruct.Pin = %s;\n' % pin_mask)
                f.write('        HAL_GPIO_Init(GPIO%s, &GPIO_InitStruct);\n' % port)

            f.write('    }\n')

        f.write('}\n\n')

    def _gen_sd_msp(self, f):
        """Generate HAL_SD_MspInit from SDMMC pin definitions."""
        f.write('void HAL_SD_MspInit(SD_HandleTypeDef *hsd)\n{\n')
        f.write('    GPIO_InitTypeDef GPIO_InitStruct = {0};\n\n')

        for periph, pins in self.sd_pins.items():
            f.write('    if (hsd->Instance == %s) {\n' % periph)
            f.write('        __HAL_RCC_%s_CLK_ENABLE();\n' % periph)
            f.write('        __HAL_RCC_DMA2_CLK_ENABLE();\n')

            ports_seen = set()
            for p in pins:
                if p['port'] not in ports_seen:
                    f.write('        __HAL_RCC_GPIO%s_CLK_ENABLE();\n' % p['port'])
                    ports_seen.add(p['port'])

            af = pins[0]['af'] if pins[0]['af'] else STM32F7_AF_MAP.get(periph, 12)
            f.write('        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;\n')
            f.write('        GPIO_InitStruct.Pull = GPIO_PULLUP;\n')
            f.write('        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;\n')
            f.write('        GPIO_InitStruct.Alternate = GPIO_AF%d_%s;\n' % (af, periph))

            port_groups = {}
            for p in pins:
                if p['port'] not in port_groups:
                    port_groups[p['port']] = []
                port_groups[p['port']].append(p)

            for port, pp in port_groups.items():
                pin_mask = ' | '.join(['GPIO_PIN_%d' % p['pin_num'] for p in pp])
                f.write('        GPIO_InitStruct.Pin = %s;\n' % pin_mask)
                f.write('        HAL_GPIO_Init(GPIO%s, &GPIO_InitStruct);\n' % port)

            f.write('        HAL_NVIC_SetPriority(%s_IRQn, 2, 0);\n' % periph)
            f.write('        HAL_NVIC_EnableIRQ(%s_IRQn);\n' % periph)
            f.write('    }\n')

        f.write('}\n\n')

        # DeInit
        f.write('void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd)\n{\n')
        for periph, pins in self.sd_pins.items():
            f.write('    if (hsd->Instance == %s) {\n' % periph)
            f.write('        __HAL_RCC_%s_CLK_DISABLE();\n' % periph)
            port_groups = {}
            for p in pins:
                if p['port'] not in port_groups:
                    port_groups[p['port']] = []
                port_groups[p['port']].append(p)
            for port, pp in port_groups.items():
                pin_mask = ' | '.join(['GPIO_PIN_%d' % p['pin_num'] for p in pp])
                f.write('        HAL_GPIO_DeInit(GPIO%s, %s);\n' % (port, pin_mask))
            f.write('        HAL_NVIC_DisableIRQ(%s_IRQn);\n' % periph)
            f.write('    }\n')
        f.write('}\n\n')

    def _gen_usb_msp(self, f):
        """Generate HAL_PCD_MspInit from USB pin definitions."""
        f.write('void HAL_PCD_MspInit(PCD_HandleTypeDef* hpcd)\n{\n')
        f.write('    GPIO_InitTypeDef GPIO_InitStruct = {0};\n\n')

        for periph, pins in self.usb_pins.items():
            # USB OTG FS instance mapping
            usb_instance = 'USB_OTG_FS'
            f.write('    if (hpcd->Instance == %s) {\n' % usb_instance)
            ports_seen = set()
            for p in pins:
                if p['port'] not in ports_seen:
                    f.write('        __HAL_RCC_GPIO%s_CLK_ENABLE();\n' % p['port'])
                    ports_seen.add(p['port'])

            af = pins[0]['af'] if pins[0]['af'] else 10
            dm_dp_pins = [p for p in pins if 'DM' in p['signal'] or 'DP' in p['signal']]
            vbus_pins = [p for p in pins if 'VBUS' in p['signal']]
            other_pins = [p for p in pins if p not in dm_dp_pins and p not in vbus_pins]

            if dm_dp_pins:
                f.write('        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;\n')
                f.write('        GPIO_InitStruct.Pull = GPIO_NOPULL;\n')
                f.write('        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;\n')
                f.write('        GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;\n')
                pin_mask = ' | '.join(['GPIO_PIN_%d' % p['pin_num'] for p in dm_dp_pins])
                f.write('        GPIO_InitStruct.Pin = %s;\n' % pin_mask)
                f.write('        HAL_GPIO_Init(GPIO%s, &GPIO_InitStruct);\n' % dm_dp_pins[0]['port'])

            if other_pins:
                port_groups = {}
                for p in other_pins:
                    if p['port'] not in port_groups:
                        port_groups[p['port']] = []
                    port_groups[p['port']].append(p)
                f.write('        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;\n')
                f.write('        GPIO_InitStruct.Pull = GPIO_NOPULL;\n')
                f.write('        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;\n')
                f.write('        GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;\n')
                for port, pp in port_groups.items():
                    pin_mask = ' | '.join(['GPIO_PIN_%d' % p['pin_num'] for p in pp])
                    f.write('        GPIO_InitStruct.Pin = %s;\n' % pin_mask)
                    f.write('        HAL_GPIO_Init(GPIO%s, &GPIO_InitStruct);\n' % port)

            if vbus_pins:
                f.write('        GPIO_InitStruct.Pin = GPIO_PIN_%d;\n' % vbus_pins[0]['pin_num'])
                f.write('        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;\n')
                f.write('        GPIO_InitStruct.Pull = GPIO_NOPULL;\n')
                f.write('        HAL_GPIO_Init(GPIO%s, &GPIO_InitStruct);\n' % vbus_pins[0]['port'])

            f.write('        __HAL_RCC_USB_OTG_FS_CLK_ENABLE();\n')
            f.write('        HAL_NVIC_SetPriority(OTG_FS_IRQn, 1, 0);\n')
            f.write('        HAL_NVIC_EnableIRQ(OTG_FS_IRQn);\n')
            f.write('    }\n')

        f.write('}\n\n')

        # DeInit
        f.write('void HAL_PCD_MspDeInit(PCD_HandleTypeDef* hpcd)\n{\n')
        for periph, pins in self.usb_pins.items():
            usb_instance = 'USB_OTG_FS'
            f.write('    if (hpcd->Instance == %s) {\n' % usb_instance)
            f.write('        __HAL_RCC_USB_OTG_FS_CLK_DISABLE();\n')
            dm_dp_pins = [p for p in pins if 'DM' in p['signal'] or 'DP' in p['signal']]
            vbus_pins = [p for p in pins if 'VBUS' in p['signal']]
            other_pins = [p for p in pins if p not in dm_dp_pins and p not in vbus_pins]
            all_signal_pins = dm_dp_pins + vbus_pins + other_pins
            for p in all_signal_pins:
                f.write('        HAL_GPIO_DeInit(GPIO%s, GPIO_PIN_%d);\n' % (p['port'], p['pin_num']))
            f.write('        HAL_NVIC_DisableIRQ(OTG_FS_IRQn);\n')
            f.write('    }\n')
        f.write('}\n\n')

    # ===================== link.lds generation =====================

    def write_linker_script(self, outdir):
        """Generate link.lds from FLASH/RAM layout."""
        flash_kb = self.get_config_int('FLASH_SIZE_KB', 2048)
        flash_reserve_start = self.get_config_int('FLASH_RESERVE_START_KB', 32)
        flash_reserve_end = self.get_config_int('FLASH_RESERVE_END_KB', 0)
        ram_kb = self.ram_size_kb if self.ram_size_kb else 512
        ram_base = self.ram_base

        flash_origin = 0x08000000 + flash_reserve_start * 1024
        flash_length = (flash_kb - flash_reserve_start - flash_reserve_end) * 1024
        ram_length = ram_kb * 1024

        path = os.path.join(outdir, 'link.lds')
        with open(path, 'w') as f:
            f.write('/*\n')
            f.write(' * linker script — auto-generated by rtt_hwdef.py\n')
            f.write(' * FLASH: %dKB (origin 0x%08x), RAM: %dKB (origin 0x%08x)\n' %
                    (flash_length // 1024, flash_origin, ram_kb, ram_base))
            f.write(' * DO NOT EDIT — regenerate from hwdef.dat\n')
            f.write(' */\n\n')

            f.write('MEMORY\n{\n')
            f.write('    ROM (RX) : ORIGIN = 0x%08x, LENGTH = %dK\n' % (flash_origin, flash_length // 1024))
            f.write('    RAM (RW) : ORIGIN = 0x%08x, LENGTH = %dK\n' % (ram_base, ram_kb))
            f.write('}\n\n')

            # Read existing linker script for section layout template
            template = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    '..', 'common', 'board', 'linker_scripts', 'link.lds')
            if os.path.isfile(template):
                with open(template) as tf:
                    content = tf.read()
                    # Find the SECTIONS part from template
                    sections_idx = content.find('SECTIONS')
                    if sections_idx >= 0:
                        f.write(content[sections_idx:])
                    else:
                        f.write('/* SECTIONS: copy from MCU family template */\n')
            else:
                f.write('/* SECTIONS: add MCU-specific section layout */\n')

        print("Generated: %s" % path)

    # ===================== rtconfig.h generation =====================

    def write_rtconfig_h(self, outdir):
        """Append hwdef-derived peripheral enables to existing rtconfig.h.

        The base rtconfig.h is generated by RT-Thread's mk_rtconfig from .config.
        This function only adds peripheral enables derived from hwdef.dat pin defs.
        If rtconfig.h doesn't exist yet, creates a minimal one.
        """
        import re
        path = os.path.join(outdir, 'rtconfig.h')

        # Read existing content if present (generated by mk_rtconfig from .config)
        existing = ''
        if os.path.isfile(path):
            with open(path) as f:
                existing = f.read()

        # Build peripheral enables from hwdef pin definitions
        enables = []
        for periph in self.uart_pins:
            enables.append('#define RT_USING_%s' % periph)
            # Also generate BSP_USING_UARTx for drv_usart.c registration
            m = re.search(r'(\d+)', periph)
            if m:
                enables.append('#define BSP_USING_UART%s' % m.group(1))
        # Collect unique SPI bus names from both pin definitions and SPIDEV entries
        spi_buses = set(self.spi_bus_pins.keys())
        for dev in self.spidev:
            # SPIDEV format: name bus devid cs_pin mode lowspeed highspeed
            if len(dev) >= 2:
                spi_buses.add(dev[1].upper())
        for bus in spi_buses:
            bus_upper = bus.upper()
            enables.append('#define RT_USING_%s' % bus)
            # Enable DMA for each SPI bus — except SPI1 where DMA returns
            # incorrect data (0xFF) on STM32F7/CUAV-V5; HAL polling works.
            if bus_upper != 'SPI1':
                enables.append('#define BSP_%s_RX_USING_DMA' % bus_upper)
                enables.append('#define BSP_%s_TX_USING_DMA' % bus_upper)
        if self.i2c_pins:
            enables.append('#define RT_USING_I2C')
            enables.append('#define RT_USING_I2C_BITOPS')
            for bus_name, pins in self.i2c_pins.items():
                bus_upper = bus_name.upper()
                enables.append('#define BSP_USING_%s' % bus_upper)
                if 'SCL' in pins:
                    enables.append('#define BSP_%s_SCL_PIN %d' % (bus_upper, pins['SCL']['rtt_pin']))
                if 'SDA' in pins:
                    enables.append('#define BSP_%s_SDA_PIN %d' % (bus_upper, pins['SDA']['rtt_pin']))
        if self.pwm_pins:
            enables.append('#define RT_USING_PWM')
        if self.sd_pins:
            enables.append('#define RT_USING_SDIO')
        if self.usb_pins:
            enables.append('#define BSP_USING_USB_DEVICE')

        append_block = '\n/* === Auto-enabled from hwdef.dat === */\n' + '\n'.join(enables) + '\n'

        if existing:
            # Check if block already present
            if 'Auto-enabled from hwdef.dat' in existing:
                # Replace existing block (preserve trailing #endif if present)
                had_endif = '#endif' in existing
                existing = re.sub(
                    r'\n/\* === Auto-enabled from hwdef\.dat === \*/\n.*',
                    append_block, existing, flags=re.DOTALL)
                if had_endif:
                    existing = existing.rstrip() + '\n#endif /* RTCONFIG_H */\n'
                with open(path, 'w') as f:
                    f.write(existing)
            else:
                # Append before #endif
                if '#endif' in existing:
                    existing = existing.replace('#endif', append_block + '#endif')
                else:
                    existing += append_block
                with open(path, 'w') as f:
                    f.write(existing)
        else:
            # No existing rtconfig.h — create minimal (should not normally happen)
            with open(path, 'w') as f:
                f.write('/* rtconfig.h — peripheral enables from hwdef.dat */\n')
                f.write('/* Base config should be generated by mk_rtconfig from .config */\n\n')
                f.write('#ifndef RTCONFIG_H\n#define RTCONFIG_H\n')
                f.write(append_block)
                f.write('#endif /* RTCONFIG_H */\n')

        print("Updated: %s (appended hwdef peripheral enables)" % path)

    # ===================== ROMFS =====================

    def write_ROMFS(self, outdir):
        """Write ROMFS file list and generate ap_romfs_embedded.h via embed.py."""
        if not self.romfs:
            return
        romfs_list = []
        hwdef_dir = os.path.dirname(os.path.abspath(self.hwdef[0])) if self.hwdef else os.getcwd()
        # Resolve AP_ROOT: walk up from hwdef_dir to find the ArduPilot root
        ap_root = os.path.normpath(os.path.join(hwdef_dir, '..', '..', '..', '..'))
        for name, path in self.romfs.items():
            if not os.path.isabs(path):
                abs_path = os.path.join(hwdef_dir, path)
                if not os.path.isfile(abs_path):
                    hal_dir = os.path.normpath(os.path.join(hwdef_dir, '..', '..', '..'))
                    abs_path = os.path.join(hal_dir, path)
                if not os.path.isfile(abs_path):
                    abs_path = os.path.join(ap_root, path)
                path = abs_path
            romfs_list.append((name, path))

        # Save pickle for reference
        import pickle
        pickle_path = os.path.join(outdir, 'romfs.pickle')
        with open(pickle_path, 'wb') as pf:
            pickle.dump(romfs_list, pf)
        print("ROMFS: %d file(s) registered in %s" % (len(romfs_list), pickle_path))
        for name, path in romfs_list:
            print("  %s <- %s" % (name, path))

        # Generate ap_romfs_embedded.h using ArduPilot's embed.py
        embed_script = os.path.join(ap_root, 'Tools', 'ardupilotwaf', 'embed.py')
        header_path = os.path.join(outdir, 'ap_romfs_embedded.h')
        if os.path.isfile(embed_script):
            # Import embed.py and call create_embedded_h directly
            import importlib.util
            spec = importlib.util.spec_from_file_location('embed', embed_script)
            embed_mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(embed_mod)
            ok = embed_mod.create_embedded_h(header_path, romfs_list)
            if ok:
                print("ROMFS: generated %s (%d files embedded)" % (header_path, len(romfs_list)))
                # Add HAL_HAVE_AP_ROMFS_EMBEDDED_H to hwdef.h so AP_ROMFS enables the embedded data
                hwdef_path = os.path.join(outdir, 'hwdef.h')
                if os.path.isfile(hwdef_path):
                    with open(hwdef_path, 'a') as hf:
                        hf.write('\n// ROMFS embedded header available\n')
                        hf.write('#define HAL_HAVE_AP_ROMFS_EMBEDDED_H 1\n')
                    print("ROMFS: added HAL_HAVE_AP_ROMFS_EMBEDDED_H to %s" % hwdef_path)
            else:
                print("ROMFS: ERROR generating %s" % header_path, file=sys.stderr)
        else:
            print("ROMFS: WARNING embed.py not found at %s" % embed_script, file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(
        description='RTT hwdef: parse hwdef.dat, generate hwdef.h + rt_pin_config.c + link.lds + rtconfig.h'
    )
    parser.add_argument('-D', '--outdir', dest='outdir', default=None)
    parser.add_argument('--params', default='')
    parser.add_argument('hwdef', nargs=1, metavar='hwdef.dat')
    args = parser.parse_args()

    outdir = args.outdir if args.outdir else os.getcwd()
    hwdef_path = args.hwdef[0]
    if not os.path.isfile(hwdef_path):
        print('rtt_hwdef: hwdef file not found: %s' % hwdef_path, file=sys.stderr)
        return 1

    try:
        os.makedirs(outdir, exist_ok=True)
    except OSError as e:
        print('rtt_hwdef: cannot create outdir %s: %s' % (outdir, e), file=sys.stderr)
        return 1

    h = RTTHWDef(outdir=outdir, hwdef=[hwdef_path])
    h.run()

    # Generate additional files
    h.write_pin_config_c(outdir)
    h.write_linker_script(outdir)
    h.write_rtconfig_h(outdir)
    h.write_ROMFS(outdir)

    # Placeholder ldscript.ld for waf compatibility
    ldscript_path = os.path.join(outdir, 'ldscript.ld')
    if not os.path.isfile(ldscript_path):
        with open(ldscript_path, 'w') as f:
            f.write('/* placeholder - actual linker script is link.lds */\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
