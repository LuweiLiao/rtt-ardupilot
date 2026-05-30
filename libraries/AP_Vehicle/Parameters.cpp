#include "AP_Vehicle.h"

#if AP_VEHICLE_ENABLED

#include <AP_Param/AP_Param.h>
#include <StorageManager/StorageManager.h>

void AP_Vehicle::load_parameters(AP_Int16 &format_version, const uint16_t expected_format_version)
{
    extern volatile uint32_t rtt_dbg_setup_stage;
    rtt_dbg_setup_stage = 510; // entered load_parameters
    if (!format_version.load() ||
        format_version != expected_format_version) {
        rtt_dbg_setup_stage = 511; // mismatch, about to erase

        // erase all parameters
        hal.console->printf("Firmware change: erasing EEPROM...\n");

        StorageManager::erase();

        AP_Param::erase_all();
        rtt_dbg_setup_stage = 512; // erased, about to save format

        // save the current format version
        format_version.set_and_save(expected_format_version);
        rtt_dbg_setup_stage = 513; // format saved
        hal.console->printf("done.\n");
    }
    rtt_dbg_setup_stage = 514; // format OK or saved, about to load_all

    format_version.set_default(expected_format_version);

    // Load all auto-loaded EEPROM variables
    AP_Param::load_all();
    rtt_dbg_setup_stage = 515; // load_all done
}

#endif  // AP_VEHICLE_ENABLED
