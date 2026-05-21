from building import *
import os

# Import environment variables
Import('env')

# Get the current working directory
cwd = GetCurrentDir()

# Initialize include paths and source files list
path = [os.path.join(cwd, 'Include')]
src = [os.path.join(cwd, 'Source', 'Templates', 'system_stm32f7xx.c')]

# Map microcontroller units (MCUs) to their corresponding startup files
mcu_startup_files = {
    'STM32F722xx': 'startup_stm32f722xx.s',
    'STM32F723xx': 'startup_stm32f723xx.s',
    'STM32F730xx': 'startup_stm32f730xx.s',
    'STM32F732xx': 'startup_stm32f732xx.s',
    'STM32F733xx': 'startup_stm32f733xx.s',
    'STM32F745xx': 'startup_stm32f745xx.s',
    'STM32F746xx': 'startup_stm32f746xx.s',
    'STM32F750xx': 'startup_stm32f750xx.s',
    'STM32F756xx': 'startup_stm32f756xx.s',
    'STM32F765xx': 'startup_stm32f765xx.s',
    'STM32F767xx': 'startup_stm32f767xx.s',
    'STM32F769xx': 'startup_stm32f769xx.s',
    'STM32F777xx': 'startup_stm32f777xx.s',
    'STM32F779xx': 'startup_stm32f779xx.s',
}

# Check each defined MCU, match the platform and append the appropriate startup file
cpp_defines_tuple = env.get('CPPDEFINES', [])
cpp_defines_list = [item[0] if isinstance(item, tuple) else item for item in cpp_defines_tuple]
for mcu, startup_file in mcu_startup_files.items():
    if mcu in cpp_defines_list:
        if rtconfig.PLATFORM in ['gcc', 'llvm-arm']:
            src += [os.path.join(cwd, 'Source', 'Templates', 'gcc', startup_file)]
        elif rtconfig.PLATFORM in ['armcc', 'armclang']:
            src += [os.path.join(cwd, 'Source', 'Templates', 'arm', startup_file)]
        elif rtconfig.PLATFORM in ['iccarm']:
            src += [os.path.join(cwd, 'Source', 'Templates', 'iar', startup_file)]
        break

# Define the build group
group = DefineGroup('STM32F7-CMSIS', src, depend=['PKG_USING_STM32F7_CMSIS_DRIVER'], CPPPATH=path)

# Return the build group
Return('group')
