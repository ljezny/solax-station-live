#pragma once

#include "../../Protocol/ModbusTCP.hpp"
#include "../../utils/RemoteLogger.hpp"
#include <WiFi.h>
#include <HTTPClient.h>
#include "../InverterResult.hpp"

class SolaxModbusDongleAPI
{
public:
    SolaxModbusDongleAPI() : isSupportedDongle(true), ip(0, 0, 0, 0), consecutiveTimeoutErrors(0) {}

    /**
     * Returns true if this inverter supports intelligence mode control
     */
    bool supportsIntelligence() { return true; }

    InverterData_t loadData(const String &ipAddress)
    {
        InverterData_t inverterData = {};
        inverterData.millis = millis();

        if (!isSupportedDongle)
        {
            inverterData.status = DONGLE_STATUS_UNSUPPORTED_DONGLE;
            return inverterData;
        }

        if (!connectToDongle(ipAddress))
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }
        
        // Read data and track modbus timeouts
        // Connection succeeded, but if reads fail it might be a stuck dongle
        bool readSuccess = readInverterInfo(inverterData) &&
                          readMainInverterData(inverterData) &&
                          readPowerData(inverterData) &&
                          readPhaseData(inverterData) &&
                          readPV3Power(inverterData);
        
        if (!handleModbusResult(ipAddress, readSuccess))
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            channel.disconnect();
            return inverterData;
        }
        
        // Read run mode first - if inverter is idle/standby, assume Self-Use mode
        // (reading work mode registers fails when inverter is sleeping)
        uint16_t runMode = readRunMode();
        if (runMode == RUN_MODE_IDLE || runMode == RUN_MODE_STANDBY)
        {
            inverterData.inverterMode = INVERTER_MODE_SELF_USE;
        }
        else
        {
            if (!readWorkMode(inverterData))
            { 
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                channel.disconnect();
                return inverterData;
            }
        }

        // Read RTC time (optional, don't fail if not available)
        readInverterRTC(inverterData);

        finalizePowerCalculations(inverterData);
        logInverterData(inverterData);
        channel.disconnect();
        return inverterData;
    }

    /**
     * Nastaví work mode střídače Solax
     * Pokud je střídač v idle/standby, nejprve ho probudí
     * @param ipAddress IP adresa donglu
     * @param mode Požadovaný režim (InverterMode_t)
     * @return true pokud se nastavení podařilo
     */
    bool setWorkMode(const String &ipAddress, InverterMode_t mode)
    {
        if (!connectToDongle(ipAddress))
        {
            return false;
        }

        // First check if inverter is awake
        uint16_t runMode = readRunMode();
        
        // If inverter is in idle/standby mode, try to wake it up
        // According to forum advice: Switch to Manual Mode + Hold (Stop Charge/Discharge)
        // This should wake the inverter, then we can set the desired mode
        if (runMode == RUN_MODE_IDLE || runMode == RUN_MODE_STANDBY)
        {
            
            // First unlock the inverter with advanced code
            bool unlockSuccess = channel.writeSingleRegister(UNIT_ID, REG_LOCK_STATE, LOCK_STATE_UNLOCKED_ADVANCED);
            if (!unlockSuccess)
            {
                LOGW("Failed to unlock inverter for wakeup");
            }
            else
            {
            }
            
            // Step 1: Set Work Mode to Manual (3) 
            bool workModeSuccess = channel.writeSingleRegister(UNIT_ID, REG_CHARGER_USE_MODE_WRITE, SOLAX_WORK_MODE_MANUAL);
            
            // Step 2: Set Manual Mode to Stop/Hold (0) - this should wake the inverter
            bool manualModeSuccess = channel.writeSingleRegister(UNIT_ID, REG_MANUAL_MODE_WRITE, SOLAX_MANUAL_STOP);
            
            if (workModeSuccess && manualModeSuccess)
            {
                // Wait for the inverter to wake up - poll every 5 seconds for up to 5 minutes
                const int maxWaitMs = 300000;  // 5 minutes
                const int pollIntervalMs = 5000;
                int elapsedMs = 0;
                
                while (elapsedMs < maxWaitMs)
                {
                    delay(pollIntervalMs);
                    elapsedMs += pollIntervalMs;
                    
                    runMode = readRunMode();
                    
                    if (runMode != RUN_MODE_IDLE && runMode != RUN_MODE_STANDBY && runMode != 0xFFFF)
                    {
                        break;
                    }
                }
                
                if (runMode == RUN_MODE_IDLE || runMode == RUN_MODE_STANDBY)
                {
                    LOGW("Inverter still in idle/standby mode after %d seconds, will try to set mode anyway", elapsedMs / 1000);
                }
            }
        }
        
        // If waiting, we might be able to proceed after a short delay
        if (runMode == RUN_MODE_WAITING)
        {
        }

        uint16_t solaxWorkMode, solaxManualMode;
        inverterModeToSolaxMode(mode, solaxWorkMode, solaxManualMode);

        // Unlock the inverter with Advanced unlock code (6868)
        bool success = channel.writeSingleRegister(UNIT_ID, REG_LOCK_STATE, LOCK_STATE_UNLOCKED_ADVANCED);
        if (!success)
        {
            channel.disconnect();
            return false;
        }

        // Set the work mode using WRITE register 0x1F (GEN4/GEN5/GEN6)
        success = channel.writeSingleRegister(UNIT_ID, REG_CHARGER_USE_MODE_WRITE, solaxWorkMode);
        
        // If switching to Manual mode, also set the manual mode register 0x20
        if (success && solaxWorkMode == SOLAX_WORK_MODE_MANUAL)
        {
            success = channel.writeSingleRegister(UNIT_ID, REG_MANUAL_MODE_WRITE, solaxManualMode);
        }
        
        // Note: We don't lock the inverter back - it auto-locks after timeout
        // Trying to lock immediately causes Modbus exception 4 (Slave Device Failure)
        
        channel.disconnect();
        
        if (success)
        {
        }
        else
        {
        }
        
        return success;
    }
    
    /**
     * Nastaví režim střídače pomocí Remote Power Control registrů
     * Tato metoda je bezpečnější než setWorkMode - automatický návrat do Self-Use po timeoutu
     * DŮLEŽITÉ: Používá writeMultipleRegisters (FC 0x10) - střídač vyžaduje atomický zápis všech registrů najednou!
     * @param ipAddress IP adresa donglu
     * @param mode Požadovaný režim (InverterMode_t)
     * @param maxChargePowerW Maximální nabíjecí výkon v W (default 10000)
     * @param maxDischargePowerW Maximální vybíjecí výkon v W (default 10000)
     * @param timeoutSec Timeout v sekundách, po kterém se vrátí do Self-Use (default 300 = 5 min)
     * @return true pokud se nastavení podařilo
     */
    bool setWorkModeViaPowerControl(const String &ipAddress, InverterMode_t mode, 
                                     uint16_t maxChargePowerW = 10000, 
                                     uint16_t maxDischargePowerW = 10000,
                                     uint16_t timeoutSec = 300)
    {
        if (!connectToDongle(ipAddress))
        {
            return false;
        }

        // Převod InverterMode na Power Control parametry
        // Podle dokumentace: positive = charge, negative = discharge
        int32_t activePowerTarget = 0;
        uint16_t powerCtrlMode = POWER_CTRL_POWER;  // 1 = enable power control
        
        switch (mode)
        {
        case INVERTER_MODE_SELF_USE:
            // Vypnout remote control → střídač řídí sám
            powerCtrlMode = POWER_CTRL_DISABLED;  // 0 = disable
            timeoutSec = 0;
            activePowerTarget = 0;
            break;
            
        case INVERTER_MODE_CHARGE_FROM_GRID:
            // Nabíjení = KLADNÝ výkon (podle dokumentace: positive = charge)
            activePowerTarget = (int32_t)maxChargePowerW;
            break;
            
        case INVERTER_MODE_DISCHARGE_TO_GRID:
            // Vybíjení = ZÁPORNÝ výkon (podle dokumentace: negative = discharge)
            activePowerTarget = -(int32_t)maxDischargePowerW;
            break;
            
        case INVERTER_MODE_HOLD_BATTERY:
            // Držet baterii = 0 W výkon
            activePowerTarget = 0;
            break;
            
        default:
            powerCtrlMode = POWER_CTRL_DISABLED;
            timeoutSec = 0;
            break;
        }

        // Unlock střídače
        bool success = channel.writeSingleRegister(UNIT_ID, REG_LOCK_STATE, LOCK_STATE_UNLOCKED_ADVANCED);
        if (!success)
        {
            channel.disconnect();
            return false;
        }

        // Sestavit payload pro writeMultipleRegisters
        // Registry 0x7C-0x88 (13 registrů) podle HA implementace:
        // 0x7C: ModbusPowerControl mode (U16)
        // 0x7D: TargetSetType (U16) = 1 (Set)
        // 0x7E-0x7F: ActivePower (S32, LSB first)
        // 0x80-0x81: ReactivePower (S32, LSB first) = 0
        // 0x82: Duration (U16)
        // 0x83: Target SOC (U16) = 0 (dummy)
        // 0x84-0x85: Target Energy Wh (U32) = 0 (dummy)
        // 0x86-0x87: Charge/Discharge Power (S32) = 0 (dummy)
        // 0x88: Timeout (U16)
        
        uint16_t registers[13];
        registers[0] = powerCtrlMode;                                    // 0x7C: power control mode
        registers[1] = 1;                                                // 0x7D: set type = Set
        registers[2] = (uint16_t)(activePowerTarget & 0xFFFF);           // 0x7E: active power LSB
        registers[3] = (uint16_t)((activePowerTarget >> 16) & 0xFFFF);   // 0x7F: active power MSB
        registers[4] = 0;                                                // 0x80: reactive power LSB
        registers[5] = 0;                                                // 0x81: reactive power MSB
        registers[6] = timeoutSec;                                       // 0x82: duration
        registers[7] = 0;                                                // 0x83: target SOC (dummy)
        registers[8] = 0;                                                // 0x84: target energy LSB (dummy)
        registers[9] = 0;                                                // 0x85: target energy MSB (dummy)
        registers[10] = 0;                                               // 0x86: charge/discharge power LSB (dummy)
        registers[11] = 0;                                               // 0x87: charge/discharge power MSB (dummy)
        registers[12] = timeoutSec > 0 ? 2 : 0;                          // 0x88: timeout (0 or 2 minutes default)
        
        
        // Použít writeMultipleRegisters pro atomický zápis všech registrů najednou
        success = channel.writeMultipleRegisters(UNIT_ID, REG_POWER_CTRL_MODE, registers, 13);
        
        channel.disconnect();
        
        if (success)
        {
        }
        else
        {
            LOGW("Failed to write Power Control registers");
        }
        
        return success;
    }
    
    /**
     * Přečte aktuální stav Remote Power Control z INPUT registrů
     * @param outMode Výstup: aktuální režim (0=disabled, 1=power, 2=quantity, 3=SOC)
     * @param outActivePower Výstup: aktuální cílový aktivní výkon (W)
     * @return true pokud se čtení podařilo
     */
    bool readPowerControlStatus(uint16_t &outMode, int32_t &outActivePower)
    {
        // Čtení z INPUT registrů 0x100-0x103
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 
                                                            REG_POWER_CTRL_STATUS, 4);
        if (!response.isValid)
        {
            return false;
        }

        outMode = response.readUInt16(REG_POWER_CTRL_STATUS);
        // ActivePowerTarget is int32 at 0x102-0x103 (LSB first)
        uint16_t activeLSB = response.readUInt16(REG_POWER_CTRL_ACTIVE_TARGET);
        uint16_t activeMSB = response.readUInt16(REG_POWER_CTRL_ACTIVE_TARGET + 1);
        outActivePower = (int32_t)((activeMSB << 16) | activeLSB);
        
        
        return true;
    }
    
    /**
     * Reads the current run mode of the inverter
     * @return Run mode value (0=Waiting, 2=Normal, 9=Idle, 10=Standby, etc.)
     */
    uint16_t readRunMode()
    {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, REG_RUN_MODE, 1);
        if (!response.isValid)
        {
            return 0xFFFF;  // Return invalid value
        }
        return response.readUInt16(REG_RUN_MODE);
    }
    
    /**
     * Checks if the inverter is awake and in normal operating mode
     * @return true if inverter is in Normal Mode
     */
    bool isInverterAwake()
    {
        uint16_t runMode = readRunMode();
        return runMode == RUN_MODE_NORMAL || runMode == RUN_MODE_EPS;
    }

protected:
    bool isSupportedDongle;
    IPAddress ip;
    String sn = "";
    ModbusTCP channel;
    int consecutiveTimeoutErrors;              // Counter for consecutive modbus timeout errors
    static constexpr int MAX_TIMEOUT_ERRORS_BEFORE_RESET = 3;  // Reset dongle after this many consecutive timeouts

private:
    static constexpr uint16_t MODBUS_PORT = 502;
    static constexpr uint8_t UNIT_ID = 1;
    static constexpr uint8_t FUNCTION_CODE_READ_HOLDING = 0x03;
    static constexpr uint8_t FUNCTION_CODE_READ_INPUT = 0x04;
    
    // Run Mode register - for reading inverter status
    static constexpr uint16_t REG_RUN_MODE = 0x0009;                // Run Mode (input register)
    static constexpr uint16_t RUN_MODE_WAITING = 0;                 // Waiting
    static constexpr uint16_t RUN_MODE_CHECKING = 1;                // Checking
    static constexpr uint16_t RUN_MODE_NORMAL = 2;                  // Normal Mode - inverter is active
    static constexpr uint16_t RUN_MODE_OFF = 3;                     // Off Mode
    static constexpr uint16_t RUN_MODE_PERMANENT_FAULT = 4;         // Permanent Fault Mode
    static constexpr uint16_t RUN_MODE_UPDATE = 5;                  // Update Mode
    static constexpr uint16_t RUN_MODE_EPS_CHECK = 6;               // EPS Check Mode
    static constexpr uint16_t RUN_MODE_EPS = 7;                     // EPS Mode
    static constexpr uint16_t RUN_MODE_SELF_TEST = 8;               // Self Test
    static constexpr uint16_t RUN_MODE_IDLE = 9;                    // Idle Mode
    static constexpr uint16_t RUN_MODE_STANDBY = 10;                // Standby
    
    // System On/Off, Battery Awaken and VPP Exit Idle registers
    static constexpr uint16_t REG_SYSTEM_ON_OFF = 0x001C;           // System On/Off (1=On, 0=Off)
    static constexpr uint16_t REG_BATTERY_AWAKEN = 0x0056;          // Battery Awaken (write 1 to wake) - only for G2/lead-acid
    static constexpr uint16_t REG_VPP_EXIT_IDLE = 0x00F4;           // VPP Exit Idle Enable (0=Disabled, 1=Enabled)
    
    // Remote Power Control WRITE registers (Holding registers 0x7C-0x85)
    static constexpr uint16_t REG_POWER_CTRL_MODE = 0x007C;         // 0=disable, 1=power control, 2=electric quantity, 3=SOC target
    static constexpr uint16_t REG_POWER_CTRL_SET_TYPE = 0x007D;     // 1=set, 2=update
    static constexpr uint16_t REG_POWER_CTRL_ACTIVE_POWER = 0x007E; // int32 LSB - ActivePower (W): positive=charge, negative=discharge
    // 0x007F = REG_POWER_CTRL_ACTIVE_POWER MSB
    static constexpr uint16_t REG_POWER_CTRL_REACTIVE_POWER = 0x0080; // int32 LSB - ReactivePower (VAr)
    // 0x0081 = REG_POWER_CTRL_REACTIVE_POWER MSB
    static constexpr uint16_t REG_POWER_CTRL_TIMEOUT = 0x0082;      // Time of Duration in seconds
    static constexpr uint16_t REG_POWER_CTRL_TARGET_SOC = 0x0083;   // Target SOC (%)
    
    // Remote Power Control READ registers (Input registers 0x100+)
    static constexpr uint16_t REG_POWER_CTRL_STATUS = 0x0100;       // Current ModbusPowerControl status (0-3)
    static constexpr uint16_t REG_POWER_CTRL_TARGET_FINISH = 0x0101; // 0=unfinished, 1=finish
    static constexpr uint16_t REG_POWER_CTRL_ACTIVE_TARGET = 0x0102; // int32 LSB - ActivePowerTarget
    // 0x0103 = REG_POWER_CTRL_ACTIVE_TARGET MSB
    
    // Power Control Mode values
    static constexpr uint16_t POWER_CTRL_DISABLED = 0;              // Disable remote control
    static constexpr uint16_t POWER_CTRL_POWER = 1;                 // Enable power control
    static constexpr uint16_t POWER_CTRL_QUANTITY = 2;              // Enable electric quantity control
    static constexpr uint16_t POWER_CTRL_SOC = 3;                   // Enable SOC target control
    
    // Lock State register - must unlock before writing mode registers
    static constexpr uint16_t REG_LOCK_STATE = 0x0000;              // Lock State register
    static constexpr uint16_t LOCK_STATE_LOCKED = 0;                // Locked
    static constexpr uint16_t LOCK_STATE_UNLOCKED = 2014;           // Unlocked
    static constexpr uint16_t LOCK_STATE_UNLOCKED_ADVANCED = 6868;  // Unlocked - Advanced (required for mode changes)
    
    // Solax Work Mode register addresses
    // READ registers (INPUT registers 0x8B, 0x8C) - for reading current state
    static constexpr uint16_t REG_SOLAR_CHARGER_USE_MODE = 0x008B;  // SolarChargerUseMode - read current work mode
    static constexpr uint16_t REG_MANUAL_MODE_READ = 0x008C;        // Manual Mode register - for reading
    
    // WRITE registers (HOLDING registers 0x1F, 0x20) - for GEN4/GEN5/GEN6 inverters
    static constexpr uint16_t REG_CHARGER_USE_MODE_WRITE = 0x001F;  // Charger Use Mode - for writing (GEN4+)
    static constexpr uint16_t REG_MANUAL_MODE_WRITE = 0x0020;       // Manual Mode Select - for writing (GEN4+)
    
    // Solax Work Mode values (SolarChargerUseMode register 0x008B)
    static constexpr uint16_t SOLAX_WORK_MODE_SELF_USE = 0;         // Self use mode
    static constexpr uint16_t SOLAX_WORK_MODE_FEEDIN_PRIORITY = 1;  // Feedin Priority
    static constexpr uint16_t SOLAX_WORK_MODE_BACK_UP = 2;          // Back up mode
    static constexpr uint16_t SOLAX_WORK_MODE_MANUAL = 3;           // Manual mode
    
    // Manual Mode values (register 0x008C) - when in Manual mode (mode 3)
    static constexpr uint16_t SOLAX_MANUAL_STOP = 0;                // Stop charge & discharge
    static constexpr uint16_t SOLAX_MANUAL_FORCE_CHARGE = 1;        // Force charge
    static constexpr uint16_t SOLAX_MANUAL_FORCE_DISCHARGE = 2;     // Force discharge

    /**
     * Převede InverterMode_t na Solax work mode a manual mode hodnoty
     * @param mode Požadovaný režim
     * @param outWorkMode Výstup: hodnota pro registr 0x008B (SolarChargerUseMode)
     * @param outManualMode Výstup: hodnota pro registr 0x008C (ManualMode)
     */
    void inverterModeToSolaxMode(InverterMode_t mode, uint16_t &outWorkMode, uint16_t &outManualMode)
    {
        switch (mode)
        {
        case INVERTER_MODE_SELF_USE:
            outWorkMode = SOLAX_WORK_MODE_SELF_USE;
            outManualMode = SOLAX_MANUAL_STOP;
            break;
        case INVERTER_MODE_CHARGE_FROM_GRID:
            // Pro nabíjení ze sítě použijeme Manual mode s Force Charge
            outWorkMode = SOLAX_WORK_MODE_MANUAL;
            outManualMode = SOLAX_MANUAL_FORCE_CHARGE;
            break;
        case INVERTER_MODE_DISCHARGE_TO_GRID:
            // Pro prodej do sítě použijeme Manual mode s Force Discharge
            outWorkMode = SOLAX_WORK_MODE_MANUAL;
            outManualMode = SOLAX_MANUAL_FORCE_DISCHARGE;
            break;
        case INVERTER_MODE_HOLD_BATTERY:
            // Pro držení baterie použijeme Manual mode se Stop
            outWorkMode = SOLAX_WORK_MODE_MANUAL;
            outManualMode = SOLAX_MANUAL_STOP;
            break;
        default:
            outWorkMode = SOLAX_WORK_MODE_SELF_USE;
            outManualMode = SOLAX_MANUAL_STOP;
            break;
        }
    }

    /**
     * Převede Solax work mode a manual mode hodnoty na InverterMode_t
     * @param solaxMode Hodnota z registru 0x008B (SolarChargerUseMode)
     * @param manualMode Hodnota z registru 0x008C (ManualMode)
     */
    InverterMode_t solaxModeToInverterMode(uint16_t solaxMode, uint16_t manualMode = 0)
    {
        switch (solaxMode)
        {
        case SOLAX_WORK_MODE_SELF_USE:
            return INVERTER_MODE_SELF_USE;
        case SOLAX_WORK_MODE_FEEDIN_PRIORITY:
            return INVERTER_MODE_DISCHARGE_TO_GRID;
        case SOLAX_WORK_MODE_BACK_UP:
            return INVERTER_MODE_HOLD_BATTERY;
        case SOLAX_WORK_MODE_MANUAL:
            // V manual mode záleží na hodnotě manualMode registru
            switch (manualMode)
            {
            case SOLAX_MANUAL_FORCE_CHARGE:
                return INVERTER_MODE_CHARGE_FROM_GRID;
            case SOLAX_MANUAL_FORCE_DISCHARGE:
                return INVERTER_MODE_DISCHARGE_TO_GRID;
            case SOLAX_MANUAL_STOP:
            default:
                return INVERTER_MODE_HOLD_BATTERY;
            }
        default:
            return INVERTER_MODE_UNKNOWN;
        }
    }

    bool connectToDongle(const String &ipAddress)
    {
        IPAddress targetIp = getIp(ipAddress);
        if (!channel.connect(targetIp, MODBUS_PORT))
        {
            ip = IPAddress(0, 0, 0, 0);
            channel.disconnect();
            return false;
        }
        return true;
    }

    /**
     * Reset the dongle via HTTP POST to update endpoint with empty firmware file.
     * This causes the dongle to restart and recover from stuck state.
     * @param ipAddress IP address of the dongle
     * @return true if reset request was sent successfully
     */
    bool resetDongle(const String &ipAddress)
    {
        IPAddress targetIp = getIp(ipAddress);
        String url = "http://" + targetIp.toString() + "/update.htm";
        
        LOGW("Attempting to reset stuck dongle at %s", url.c_str());
        
        HTTPClient http;
        http.begin(url);
        
        // Set headers matching the browser request
        http.addHeader("Content-Type", "multipart/form-data; boundary=----WebKitFormBoundaryDongleReset");
        http.addHeader("Authorization", "Basic YWRtaW46U1JEQURTSlc0TA==");  // admin:SRDADSJW4L
        http.addHeader("Origin", "http://" + targetIp.toString());
        http.addHeader("Referer", "http://" + targetIp.toString() + "/upload.html");
        
        // Send empty multipart form data (no actual file)
        String payload = "------WebKitFormBoundaryDongleReset\r\n"
                        "Content-Disposition: form-data; name=\"inputFile\"; filename=\"\"\r\n"
                        "Content-Type: application/octet-stream\r\n"
                        "\r\n"
                        "\r\n"
                        "------WebKitFormBoundaryDongleReset--\r\n";
        
        int httpCode = http.POST(payload);
        http.end();
        
        if (httpCode > 0)
        {
            LOGW("Dongle reset request sent, HTTP code: %d. Dongle should restart within 20 seconds.", httpCode);
            consecutiveTimeoutErrors = 0;  // Reset the counter
            return true;
        }
        else
        {
            LOGE("Failed to send dongle reset request, error: %s", http.errorToString(httpCode).c_str());
            return false;
        }
    }

    /**
     * Handle modbus read result - track timeout errors and reset dongle if stuck
     * @param ipAddress IP address of the dongle (for potential reset)
     * @param success Whether the modbus operation succeeded
     * @return The same success value passed in
     */
    bool handleModbusResult(const String &ipAddress, bool success)
    {
        if (success)
        {
            // Reset counter on successful communication
            if (consecutiveTimeoutErrors > 0)
            {
            }
            consecutiveTimeoutErrors = 0;
        }
        else
        {
            consecutiveTimeoutErrors++;
            LOGW("Modbus timeout error #%d", consecutiveTimeoutErrors);
            
            // If we've had too many consecutive timeouts, try to reset the dongle
            if (consecutiveTimeoutErrors >= MAX_TIMEOUT_ERRORS_BEFORE_RESET)
            {
                LOGW("Detected stuck dongle (connected but %d consecutive timeouts), attempting reset...", 
                      consecutiveTimeoutErrors);
                channel.disconnect();
                resetDongle(ipAddress);
                // After reset, wait for dongle to restart
                delay(25000);  // Wait 25 seconds for dongle to restart
            }
        }
        return success;
    }

    bool readInverterInfo(InverterData_t &data)
    {
        if (!sn.isEmpty())
        {
            data.sn = sn;
            return true;
        }
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, 0x00, 0x14);
        if (!response.isValid)
        {
            return false;
        }

        data.status = DONGLE_STATUS_OK;
        sn = response.readString(0x00, 14);
        data.sn = sn;
        String factoryName = response.readString(0x07, 14);
        String moduleName = response.readString(0x0E, 14);
        return true;
    }

    bool readInverterRTC(InverterData_t &data)
    {
        // Solax RTC registers: 0x85-0x8A (holding registers) - for reading
        // 0x85: Second, 0x86: Minute, 0x87: Hour, 0x88: Day, 0x89: Month, 0x8A: Year-2000
        // Note: Writing RTC uses registers 0x00-0x05
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, 0x85, 6);
        if (!response.isValid)
        {
            return false;
        }

        struct tm timeinfo = {};
        timeinfo.tm_sec = response.readUInt16(0x85);
        timeinfo.tm_min = response.readUInt16(0x86);
        timeinfo.tm_hour = response.readUInt16(0x87);
        timeinfo.tm_mday = response.readUInt16(0x88);
        timeinfo.tm_mon = response.readUInt16(0x89) - 1;    // Month 1-12 to 0-11
        timeinfo.tm_year = response.readUInt16(0x8A) + 100; // Year-2000 + 100 = years since 1900
        timeinfo.tm_isdst = -1;  // Let mktime determine DST

        data.inverterTime = mktime(&timeinfo);
        return true;
    }

    bool readMainInverterData(InverterData_t &data)
    {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x00, 0x54 + 1);
        if (!response.isValid)
        {
            return false;
        }
        data.status = DONGLE_STATUS_OK;
        data.inverterOutpuPowerL1 = response.readInt16(0x02);
        data.pv1Power = response.readUInt16(0x0A);
        data.pv2Power = response.readUInt16(0x0B);
        data.inverterTemperature = response.readInt16(0x08);
        if (isGen5(data.sn))
        {
            data.inverterTemperature /= 10;
        }
        data.soc = response.readUInt16(0x1C);
        data.batteryPower = response.readInt16(0x16);
        data.batteryVoltage = response.readInt16(0x14) / 10.0f;
        //data.gridPower = response.readInt32LSB(0x46); //read later from phases
        data.batteryChargedToday = response.readUInt16(0x23) / 10.0f;
        data.batteryDischargedToday = response.readUInt16(0x20) / 10.0f;
        data.batteryTemperature = response.readInt16(0x18);
        data.gridBuyTotal = response.readUInt32LSB(0x4A) / 100.0f;
        data.gridSellTotal = response.readUInt32LSB(0x48) / 100.0f;
        data.pvTotal = response.readUInt32LSB(0x52) / 10.0f;
        data.loadToday = response.readUInt16(0x50) / 10.0f;
        data.batteryCapacityWh = response.readUInt16(0x26);
        //log 0x26 register value for debugging
        
        // Read BMS max charge/discharge current from registers 0x24 and 0x25
        // Scale is 0.1A, we convert to power using battery voltage
        float bmsChargeMaxCurrent = response.readUInt16(0x24) * 0.1f;  // Amperes
        float bmsDischargeMaxCurrent = response.readUInt16(0x25) * 0.1f;  // Amperes
        // Calculate power: P = U * I, use battery voltage if available
        float batteryVoltageForCalc = data.batteryVoltage;
        data.maxChargePowerW = (uint16_t)(bmsChargeMaxCurrent * batteryVoltageForCalc);
        data.maxDischargePowerW = (uint16_t)(bmsDischargeMaxCurrent * batteryVoltageForCalc);
        
        data.minSoc = 10;
        data.maxSoc = 100;
        return true;
    }

    bool readWorkMode(InverterData_t &data)
    {
        // Nejprve zkusíme přečíst Power Control status z INPUT registrů (0x100+)
        ModbusResponse pcResponse = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 
                                                               REG_POWER_CTRL_STATUS, 4);
        if (pcResponse.isValid)
        {
            uint16_t pcMode = pcResponse.readUInt16(REG_POWER_CTRL_STATUS);
            // ActivePowerTarget is int32 at 0x102-0x103 (LSB first)
            uint16_t activeLSB = pcResponse.readUInt16(REG_POWER_CTRL_ACTIVE_TARGET);
            uint16_t activeMSB = pcResponse.readUInt16(REG_POWER_CTRL_ACTIVE_TARGET + 1);
            int32_t activePowerTarget = (int32_t)((activeMSB << 16) | activeLSB);
            
            
            if (pcMode == POWER_CTRL_POWER)  // Power control is active
            {
                // Určíme mode podle aktivního výkonu
                // Podle dokumentace: positive = charge, negative = discharge
                if (activePowerTarget > 100)  // Kladný = nabíjení (s tolerancí)
                {
                    data.inverterMode = INVERTER_MODE_CHARGE_FROM_GRID;
                }
                else if (activePowerTarget < -100)  // Záporný = vybíjení (s tolerancí)
                {
                    data.inverterMode = INVERTER_MODE_DISCHARGE_TO_GRID;
                }
                else  // Blízko 0 = hold
                {
                    data.inverterMode = INVERTER_MODE_HOLD_BATTERY;
                }
                return true;
            }
            else if (pcMode != POWER_CTRL_DISABLED)
            {
            }
            else
            {
            }
        }
        else
        {
        }
        
        // Fallback na klasické čtení work mode registrů
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, REG_SOLAR_CHARGER_USE_MODE, 2);
        if (!response.isValid)
        {
            return false;
        }

        uint16_t solaxMode = response.readUInt16(REG_SOLAR_CHARGER_USE_MODE);
        uint16_t manualMode = response.readUInt16(REG_MANUAL_MODE_READ);
        data.inverterMode = solaxModeToInverterMode(solaxMode, manualMode);
        return true;
    }

    bool readPowerData(InverterData_t &data)
    {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x91, 0x9A - 0x91 + 2);
        if (!response.isValid)
        {
            return false;
        }

        data.gridBuyToday = response.readUInt32LSB(0x9A) / 100.0f;
        data.gridSellToday = response.readUInt32LSB(0x98) / 100.0f;
        data.pvToday = response.readUInt16(0x96) / 10.0f;
        return true;
    }

    bool readPhaseData(InverterData_t &data)
    {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x6A, 0x88 - 0x6A + 2);
        if (!response.isValid)
        {
            return false;
        }
        data.inverterOutpuPowerL1 = response.readInt16(0x6C);
        data.inverterOutpuPowerL2 = response.readInt16(0x70);
        data.inverterOutpuPowerL3 = response.readInt16(0x74);

        //backup
        uint16_t backupL1Power = response.readInt16(0x78);
        uint16_t backupL2Power = response.readInt16(0x7C);
        uint16_t backupL3Power = response.readInt16(0x80);
        data.inverterOutpuPowerL1 += backupL1Power;
        data.inverterOutpuPowerL2 += backupL2Power;
        data.inverterOutpuPowerL3 += backupL3Power;

        data.gridPowerL1 = response.readInt16(0x82);
        data.gridPowerL2 = response.readInt16(0x84);
        data.gridPowerL3 = response.readInt16(0x86);
        //data.gridPower = data.gridPowerL1 + data.gridPowerL2 + data.gridPowerL3;
        return true;
    }

    bool readPV3Power(InverterData_t &data)
    {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x0124, 1);
        if (!response.isValid)
        {
            return false;
        }

        data.pv3Power = response.readInt16(0x0124);
        return true;
    }

    void finalizePowerCalculations(InverterData_t &data)
    {
        //data.inverterPower = data.L1Power + data.L2Power + data.L3Power;
        data.loadPower = data.inverterOutpuPowerL1 + data.inverterOutpuPowerL2 + data.inverterOutpuPowerL3 - (data.gridPowerL1 + data.gridPowerL2 + data.gridPowerL3);
        data.loadToday += data.gridBuyToday - data.gridSellToday;
    }

    IPAddress getIp(const String &ipAddress)
    {
        if (ip == IPAddress(0, 0, 0, 0))
        {
            if (!ipAddress.isEmpty())
            {
                ip = IPAddress();
                ip.fromString(ipAddress);
            }
        }

        if (ip == IPAddress(0, 0, 0, 0))
        {
            ip = discoverIpViaMDNS();
        }

        if (ip == IPAddress(0, 0, 0, 0))
        {
            ip = (WiFi.localIP()[0] == 192) ? IPAddress(192, 168, 10, 10) : IPAddress(5, 8, 8, 8);
        }

        return ip;
    }

    IPAddress discoverIpViaMDNS()
    {
        mdns_result_t *results = nullptr;
        mdns_init();
        esp_err_t err = mdns_query_ptr("_pocketseries", "_tcp", 5000, 20, &results);
        if (err != ESP_OK || !results)
        {
            mdns_query_results_free(results);
            return IPAddress(0, 0, 0, 0);
        }

        IPAddress foundIp(0, 0, 0, 0);
        for (mdns_result_t *r = results; r; r = r->next)
        {
            foundIp = r->addr->addr.u_addr.ip4.addr;
            break;
        }
        mdns_query_results_free(results);
        mdns_free();
        return foundIp;
    }

    bool isGen5(const String &sn)
    {
        return sn.startsWith("H35") || sn.startsWith("H3B");
    }
};