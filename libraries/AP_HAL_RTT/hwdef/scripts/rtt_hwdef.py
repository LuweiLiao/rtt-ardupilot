#!/usr/bin/env python3
"""
RT-Thread hwdef script for ArduPilot + AP_HAL_RTT.
Inherits from HWDef base class to share IMU/BARO/MAG probe generation with ChibiOS.
Generates hwdef.h with SPI device tables, sensor probe lists, and board config.

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
    if s == 'MODE0':
        return 0
    elif s == 'MODE1':
        return 1
    elif s == 'MODE2':
        return 2
    elif s == 'MODE3':
        return 3
    return 3


class RTTHWDef(HWDef):
    def __init__(self, quiet=False, outdir=None, hwdef=[]):
        super().__init__(quiet=quiet, outdir=outdir, hwdef=hwdef)
        self.spidev = []
        self.pin_labels = {}
        self.user_defines = []
        self.board_types = _load_board_types(os.path.dirname(os.path.abspath(__file__)))

    def process_line(self, line, depth=0):
        """Extend base class to handle SPIDEV and CS pin definitions."""
        import shlex
        a = shlex.split(line, posix=False)

        if a[0] == 'SPIDEV' and len(a) >= 7:
            self.spidev.append(a[1:])
            return

        if len(a) >= 3 and len(a[0]) >= 3 and a[0][0] == 'P' and a[0][1].isalpha() and a[2] == 'CS':
            port = a[0][1].upper()
            try:
                pin = int(a[0][2:])
                self.pin_labels[a[1]] = (port, pin)
            except ValueError:
                pass
            return

        # known RTT-specific keys handled below
        rtt_keys = {'BOARD_NAME', 'MCU', 'FLASH_SIZE_KB', 'OSCILLATOR_HZ',
                     'SERIAL_ORDER', 'APJ_BOARD_ID', 'FLASH_RESERVE_START_KB',
                     'FLASH_BOOTLOADER_LOAD_KB', 'FLASH_RESERVE_END_KB'}
        if a[0] in rtt_keys:
            self.config[a[0]] = a[1:]
            return

        if a[0] == 'define' and len(a) >= 2:
            self.user_defines.append(a[1:])

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

    def write_hwdef_header_content(self, f):
        """Generate RTT-specific hwdef.h content."""
        board = self.get_config('BOARD_NAME', 'unknown')
        mcu_tokens = self.config.get('MCU', [])
        flash_kb = self.get_config_int('FLASH_SIZE_KB', 0)
        osc_hz = self.get_config_int('OSCILLATOR_HZ', 0)
        serial_order = self.config.get('SERIAL_ORDER', [])
        apj_id = self.get_config('APJ_BOARD_ID', '')

        f.write('/* RT-Thread + AP_HAL_RTT board configuration */\n\n')

        # SEEK_* constants for RT-Thread/newlib compatibility
        f.write('#ifndef SEEK_SET\n#define SEEK_SET 0\n#endif\n')
        f.write('#ifndef SEEK_CUR\n#define SEEK_CUR 1\n#endif\n')
        f.write('#ifndef SEEK_END\n#define SEEK_END 2\n#endif\n\n')

        # Sensor probe macros
        f.write('/* Sensor probe macros */\n')
        f.write('#define PROBE_IMU_SPI(driver, devname, args ...) ADD_BACKEND(AP_InertialSensor_ ## driver::probe(*this,hal.spi->get_device(devname),##args))\n')
        f.write('#define PROBE_IMU_SPI2(driver, devname1, devname2, args ...) ADD_BACKEND(AP_InertialSensor_ ## driver::probe(*this,hal.spi->get_device(devname1),hal.spi->get_device(devname2),##args))\n')
        f.write('#define PROBE_IMU_I2C(driver, bus, addr, args ...) ADD_BACKEND(AP_InertialSensor_ ## driver::probe(*this,GET_I2C_DEVICE(bus,addr),##args))\n')
        f.write('#define PROBE_BARO_SPI(driver, devname, args ...) ADD_BACKEND(AP_Baro_ ## driver::probe(*this,hal.spi->get_device(devname),##args))\n')
        f.write('#define PROBE_BARO_I2C(driver, bus, addr, args ...) ADD_BACKEND(AP_Baro_ ## driver::probe(*this,std::move(GET_I2C_DEVICE(bus,addr)),##args))\n')
        f.write('#define PROBE_MAG_SPI(driver, devname, args ...) ADD_BACKEND(DRIVER_ ## driver, AP_Compass_ ## driver::probe(hal.spi->get_device(devname),##args))\n')
        f.write('#define PROBE_MAG_I2C(driver, bus, addr, args ...) ADD_BACKEND(DRIVER_ ## driver, AP_Compass_ ## driver::probe(GET_I2C_DEVICE(bus,addr),##args))\n')
        f.write('\n')

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
        if flash_reserve_start > 0 or flash_bl_load > 0:
            f.write('#define FLASH_RESERVE_START_KB %d\n' % flash_reserve_start)
            flash_origin = 0x08000000 + flash_reserve_start * 1024
            if flash_kb > 0:
                flash_length_kb = flash_kb - flash_reserve_start - flash_reserve_end
            else:
                flash_length_kb = 0
            f.write('#define FLASH_ORIGIN 0x%08x\n' % flash_origin)
            f.write('#define FLASH_LENGTH_KB %d\n' % flash_length_kb)
        f.write('\n')

        # Oscillator
        if osc_hz > 0:
            f.write('#define OSCILLATOR_HZ %d\n\n' % osc_hz)

        # Serial ports
        f.write('#define SERIAL_PORT_COUNT %d\n' % len(serial_order))
        if serial_order:
            for i, name in enumerate(serial_order):
                f.write('#define SERIAL_PORT_%d_NAME %s\n' % (i, name))
            device_names = []
            for name in serial_order:
                if name.upper().startswith('OTG'):
                    device_names.append('"usb-acm0"')
                elif 'UART' in name.upper() or 'USART' in name.upper():
                    m = re.search(r'(\d+)', name)
                    device_names.append('"uart%s"' % (m.group(1) if m else '1'))
                else:
                    device_names.append('"uart1"')
            f.write('#define HAL_RTT_UART_DEVICE_LIST %s\n' % ', '.join(device_names))
            # GCS_Param::queued_param_send() uses bw_in_bytes_per_second(); default 5760 stalls param download.
            # ChibiOS uses is_usb for 200*1024; RTT must know serial0 is OTG even if device name table mismatches.
            first = serial_order[0]
            if first.upper().startswith('OTG'):
                f.write('#define HAL_RTT_SERIAL0_OTG 1\n')
                f.write('#define HAL_OTG1_CONFIG 1\n')  # AP_SerialManager: lock SERIAL0 to MAVLink like ChibiOS USB
            else:
                f.write('#define HAL_RTT_SERIAL0_OTG 0\n')
        else:
            f.write('#define HAL_RTT_SERIAL0_OTG 0\n')
        f.write('\n')

        # Board ID
        if apj_id:
            if apj_id in self.board_types:
                f.write('#define %s %d\n' % (apj_id, self.board_types[apj_id]))
            f.write('#define APJ_BOARD_ID %s\n\n' % apj_id)

        # SPI device table
        self._write_spi_table(f)

        # IMU/BARO/MAG probe lists (from HWDef base class)
        self.write_IMU_config(f)
        self.write_BARO_config(f)
        self.write_MAG_config(f)

        # User-defined macros (define KEY VALUE)
        self._write_user_defines(f)

        f.write('\n')

    def _write_spi_table(self, f):
        """Generate RTT_SPIDesc device table and SPI attach list from SPIDEV lines."""
        if not self.spidev:
            return

        f.write('/* SPI device table — generated from SPIDEV lines in hwdef.dat */\n')
        f.write('/* RTT_SPIDesc: name, rtt_devname, bus, devid, mode, lowspeed, highspeed */\n')

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
            f.write('/* SPI device attach table for rt_hw_spi_device_attach() */\n')
            f.write('#define HAL_RTT_SPI_ATTACH_LIST \\\n')
            f.write(', \\\n'.join(attach_entries))
            f.write('\n\n')

    def _write_user_defines(self, f):
        """Write user-defined macros from 'define KEY VALUE' lines."""
        for parts in self.user_defines:
            name = parts[0]
            value = ' '.join(parts[1:]) if len(parts) > 1 else ''
            if value:
                f.write('#define %s %s\n' % (name, value))
            else:
                f.write('#define %s\n' % name)


def main():
    parser = argparse.ArgumentParser(
        description='RTT hwdef: parse hwdef.dat, generate hwdef.h'
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

    # Generate placeholder ldscript.ld so waf dependency check passes.
    # The actual linker script is the BSP's board/linker_scripts/link.lds;
    # this file satisfies waf's target declaration only.
    ldscript_path = os.path.join(outdir, 'ldscript.ld')
    if not os.path.isfile(ldscript_path):
        with open(ldscript_path, 'w') as f:
            f.write('/* placeholder - actual linker script is BSP board/linker_scripts/link.lds */\n')

    return 0


if __name__ == '__main__':
    sys.exit(main())
