#pragma once

#include "../../Protocol/ModbusTCP.hpp"
#include <RemoteLogger.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include "../InverterResult.hpp"

/**
 * Kategorie střídače Solax - určuje sadu Modbus registrů
 */
enum SolaxInverterCategory
{
    SOLAX_CATEGORY_UNKNOWN = 0,
    SOLAX_CATEGORY_HYBRID,  // X1/X3-Hybrid, X3-Ultra, X1/X3-IES - má baterii
    SOLAX_CATEGORY_MIC      // X1-Boost, X1-Mini, X3-MIC - pouze PV, bez baterie
};

/**
 * Generace střídače Solax - ovlivňuje škálování některých hodnot
 */
enum SolaxInverterGeneration
{
    SOLAX_GEN_UNKNOWN = 0,
    SOLAX_GEN2,
    SOLAX_GEN3,
    SOLAX_GEN4,
    SOLAX_GEN5,
    SOLAX_GEN6
};

class SolaxModbusDongleAPI
{
public:
    SolaxModbusDongleAPI() : isSupportedDongle(true), ip(0, 0, 0, 0), consecutiveTimeoutErrors(0), 
                             inverterCategory(SOLAX_CATEGORY_UNKNOWN), inverterGeneration(SOLAX_GEN_UNKNOWN),
                             isThreePhase(false) {}

    /**
     * Returns true if this inverter supports intelligence mode control.
     * MIC inverters (X1-Boost, X1-Mini, X3-MIC) don't have battery, so no intelligence.
     * Note: This should be called after loadData() to ensure SN is cached and type detected.
     */
    bool supportsIntelligence() 
    { 
        // Pokud ještě neznáme kategorii, předpokládáme HYBRID (pro zpětnou kompatibilitu)
        if (inverterCategory == SOLAX_CATEGORY_UNKNOWN)
        {
            LOGW("supportsIntelligence() called before inverter type detection, assuming HYBRID");
            return true;
        }
        return inverterCategory == SOLAX_CATEGORY_HYBRID; 
    }

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
        
        // Nejprve načteme info o střídači (SN) - tím se detekuje typ
        if (!readInverterInfo(inverterData))
        {
            LOGW("Failed to read inverter info");
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            channel.disconnect();
            return inverterData;
        }
        
        bool readSuccess;
        
        // Větvení podle kategorie střídače
        if (inverterCategory == SOLAX_CATEGORY_MIC)
        {
            // MIC střídač (X1-Boost, X1-Mini, X3-MIC) - jiná registrová mapa, bez baterie
            LOGD("Loading data for MIC inverter");
            readSuccess = readMicInverterData(inverterData);
            
            if (!handleModbusResult(ipAddress, readSuccess))
            {
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                channel.disconnect();
                return inverterData;
            }
            
            // MIC nemá work mode řízení baterie
            inverterData.inverterMode = INVERTER_MODE_SELF_USE;
        }
        else
        {
            // HYBRID střídač - původní logika
            LOGD("Loading data for HYBRID inverter");
            readSuccess = readMainInverterData(inverterData) &&
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
        }

        // Read RTC time (optional, don't fail if not available)
        readInverterRTC(inverterData);

        finalizePowerCalculations(inverterData);
        logInverterData(inverterData, millis() - inverterData.millis);
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
    SolaxInverterCategory inverterCategory;    // Detected inverter category (HYBRID/MIC)
    SolaxInverterGeneration inverterGeneration; // Detected inverter generation (GEN2-GEN6)
    bool isThreePhase;                         // True for X3 (three-phase), false for X1 (single-phase)
    
    // Půlnoční hodnoty čítačů pro výpočet denních statistik
    int lastKnownDay = -1;                     // Poslední známý den (1-31) pro detekci přechodu přes půlnoc
    double midnightPvTotal = 0;                // Celková výroba PV o půlnoci (kWh)
    double midnightGridSellTotal = 0;          // Celkový export do sítě o půlnoci (kWh)
    double midnightGridBuyTotal = 0;           // Celkový import ze sítě o půlnoci (kWh)

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
        
        // Zkusit primární adresu 0x00 pro SN (standardní Solax střídače)
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, 0x00, 0x14);
        if (!response.isValid)
        {
            // Fallback: alternativní adresa 0x300 (některé MIC střídače)
            LOGD("SN read failed at 0x00, trying fallback address 0x300");
            response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, 0x300, 0x07);
            if (!response.isValid)
            {
                // Fallback 2: adresa 0x1A10 (některé starší MIC střídače)
                LOGD("SN read failed at 0x300, trying fallback address 0x1A10");
                response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, 0x1A10, 0x07);
                if (!response.isValid)
                {
                    LOGW("Failed to read SN from all known addresses (0x00, 0x300, 0x1A10)");
                    return false;
                }
                sn = response.readString(0x1A10, 14);
            }
            else
            {
                sn = response.readString(0x300, 14);
            }
        }
        else
        {
            sn = response.readString(0x00, 14);
        }
        
        // Některé střídače vrací SN s prohozením bytů - opravit pokud nezačíná na M nebo X
        if (!sn.isEmpty() && !sn.startsWith("M") && !sn.startsWith("X") && !sn.startsWith("H") && !sn.startsWith("L"))
        {
            // Swap bytes - některé starší střídače mají prohozené byty v SN
            String swapped = "";
            for (int pos = 0; pos < sn.length() - 1; pos += 2)
            {
                swapped += sn[pos + 1];
                swapped += sn[pos];
            }
            if (sn.length() % 2 == 1)
            {
                swapped += sn[sn.length() - 1];
            }
            LOGD("SN byte swap: %s -> %s", sn.c_str(), swapped.c_str());
            sn = swapped;
        }

        data.status = DONGLE_STATUS_OK;
        data.sn = sn;
        LOGD("Inverter SN: %s", sn.c_str());
        
        // Detekce typu střídače z SN
        detectInverterType(sn);
        
        // Nastav hasBattery podle kategorie
        if (inverterCategory == SOLAX_CATEGORY_MIC)
        {
            data.hasBattery = false;
            LOGD("MIC inverter detected - setting hasBattery=false");
        }
        
        return true;
    }

    bool readInverterRTC(InverterData_t &data)
    {
        if (inverterCategory == SOLAX_CATEGORY_MIC)
        {
            return readMicInverterRTC(data);
        }
        
        // HYBRID: Solax RTC registers: 0x85-0x8A (holding registers) - for reading
        // 0x85: Second, 0x86: Minute, 0x87: Hour, 0x88: Day, 0x89: Month, 0x8A: Year-2000
        // Note: Writing RTC uses registers 0x00-0x05
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, 0x85, 6);
        if (!response.isValid)
        {
            LOGW("HYBRID: Failed to read RTC from 0x85");
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
        LOGD("HYBRID RTC: %04d-%02d-%02d %02d:%02d:%02d", 
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    }
    
    /**
     * Čte RTC z MIC střídače.
     * MIC RTC registry: 0x318-0x31D (holding registers)
     * 0x318: Year (e.g. 2025), 0x319: Month, 0x31A: Day, 0x31B: Hour, 0x31C: Minute, 0x31D: Second
     */
    bool readMicInverterRTC(InverterData_t &data)
    {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, 0x318, 6);
        if (!response.isValid)
        {
            LOGW("MIC: Failed to read RTC from 0x318");
            return false;
        }

        struct tm timeinfo = {};
        uint16_t year = response.readUInt16(0x00);     // 0x318: Year (plný rok, např. 2025)
        timeinfo.tm_mon = response.readUInt16(0x01) - 1;   // 0x319: Month (1-12 -> 0-11)
        timeinfo.tm_mday = response.readUInt16(0x02);  // 0x31A: Day
        timeinfo.tm_hour = response.readUInt16(0x03);  // 0x31B: Hour
        timeinfo.tm_min = response.readUInt16(0x04);   // 0x31C: Minute
        timeinfo.tm_sec = response.readUInt16(0x05);   // 0x31D: Second
        timeinfo.tm_year = year - 1900;  // tm_year = years since 1900
        timeinfo.tm_isdst = -1;

        data.inverterTime = mktime(&timeinfo);
        LOGD("MIC RTC: %04d-%02d-%02d %02d:%02d:%02d", 
             year, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    }
    
    /**
     * Aktualizuje půlnoční čítače a vypočítá denní hodnoty (today).
     * Volá se po načtení totalů ze střídače.
     * 
     * @param data Reference na InverterData_t s aktuálními totaly
     * @param currentDay Aktuální den v měsíci (1-31), nebo -1 pokud není znám
     */
    void updateDailyCounters(InverterData_t &data, int currentDay)
    {
        // Zjistíme aktuální den - preferujeme RTC střídače, fallback na systémový čas
        if (currentDay < 0)
        {
            if (data.inverterTime > 0)
            {
                struct tm *timeinfo = localtime(&data.inverterTime);
                currentDay = timeinfo->tm_mday;
            }
            else
            {
                // Fallback na systémový čas ESP
                time_t now = time(nullptr);
                if (now > 1000000000)  // Platný čas (po roce 2001)
                {
                    struct tm *timeinfo = localtime(&now);
                    currentDay = timeinfo->tm_mday;
                }
            }
        }
        
        // Detekce přechodu přes půlnoc (změna dne)
        if (currentDay > 0 && currentDay != lastKnownDay)
        {
            if (lastKnownDay > 0)
            {
                LOGI("Midnight detected: day changed from %d to %d, resetting daily counters", lastKnownDay, currentDay);
            }
            else
            {
                LOGI("First run: initializing midnight counters for day %d", currentDay);
            }
            
            // Uložíme aktuální hodnoty jako půlnoční
            midnightPvTotal = data.pvTotal;
            midnightGridSellTotal = data.gridSellTotal;
            midnightGridBuyTotal = data.gridBuyTotal;
            lastKnownDay = currentDay;
            
            LOGD("Midnight counters set: pvTotal=%.2f, sellTotal=%.2f, buyTotal=%.2f", 
                 midnightPvTotal, midnightGridSellTotal, midnightGridBuyTotal);
        }
        
        // Výpočet denních hodnot z rozdílu (aktuální - půlnoční)
        if (lastKnownDay > 0 && midnightPvTotal > 0)
        {
            // pvToday už máme ze střídače (registr 0x425), ale pro kontrolu můžeme porovnat
            double calculatedPvToday = data.pvTotal - midnightPvTotal;
            if (calculatedPvToday < 0) calculatedPvToday = 0;  // Ochrana proti přetečení čítače
            
            // gridSellToday a gridBuyToday počítáme z totalů
            data.gridSellToday = data.gridSellTotal - midnightGridSellTotal;
            if (data.gridSellToday < 0) data.gridSellToday = 0;
            
            data.gridBuyToday = data.gridBuyTotal - midnightGridBuyTotal;
            if (data.gridBuyToday < 0) data.gridBuyToday = 0;
            
            // loadToday = pvToday - gridSellToday + gridBuyToday
            data.loadToday = data.pvToday - data.gridSellToday + data.gridBuyToday;
            if (data.loadToday < 0) data.loadToday = 0;
            
            LOGD("Daily stats: pvToday=%.2f (calc=%.2f), sellToday=%.2f, buyToday=%.2f, loadToday=%.2f", 
                 data.pvToday, calculatedPvToday, data.gridSellToday, data.gridBuyToday, data.loadToday);
        }
    }

    /**
     * Čte data z MIC střídače (X1-Boost, X1-Mini, X3-MIC) - bez baterie.
     * 
     * Registrová mapa se liší podle generace:
     * - GEN2 (X1-Boost XB3, X1-Mini XM3): Input registry od 0x400
     * - GEN4 (X1-Boost-G4 XB4, X1-Mini-G4 XM4): Input registry od 0x400 (jiné offsety)
     * 
     * Reference: https://github.com/wills106/homeassistant-solax-modbus/blob/main/custom_components/solax_modbus/plugin_solax.py
     */
    bool readMicInverterData(InverterData_t &data)
    {
        data.status = DONGLE_STATUS_OK;
        data.hasBattery = false;  // MIC nemá baterii
        
        if (inverterGeneration == SOLAX_GEN2)
        {
            return readMicGen2InverterData(data);
        }
        else if (inverterGeneration == SOLAX_GEN4)
        {
            return readMicGen4InverterData(data);
        }
        else
        {
            LOGW("MIC: Unknown generation %d, trying GEN2 register map", inverterGeneration);
            return readMicGen2InverterData(data);
        }
    }
    
    /**
     * Čte data z MIC GEN2 střídače (X1-Boost XB3/XAU/XBE/XBU, X1-Mini XM3/XMA/XAT)
     * 
     * Registrová mapa MIC GEN2 (input registry od 0x400):
     * 0x400: PV Voltage 1 (0.1V)      0x401: PV Voltage 2 (0.1V)
     * 0x402: PV Current 1 (0.1A)      0x403: PV Current 2 (0.1A)
     * 0x404: Inverter Voltage (0.1V)  0x405-0x406: L2/L3 Voltage pro X3
     * 0x407: Inverter Frequency (0.01Hz)
     * 0x40A: Inverter Current (0.1A)
     * 0x40D: Inverter Temperature (°C)
     * 0x40E: Inverter Power (W)
     * 0x40F: Run Mode
     * 0x414: PV Power 1 (W)           0x415: PV Power 2 (W)
     * 0x423-0x424: Total Yield (U32, 0.1kWh)
     * 0x425-0x426: Today's Yield (U32, 0.1kWh)
     * 0x43B-0x43C: Measured Power / Export (S32, W) - pro GEN2 s měřičem
     * 0x43D-0x43E: Total Grid Export (U32, 0.01kWh)
     * 0x43F-0x440: Total Grid Import (U32, 0.01kWh)
     */
    bool readMicGen2InverterData(InverterData_t &data)
    {
        // GEN2 MIC má registry od 0x400, čteme 0x400-0x450 (80 registrů)
        const uint16_t BASE_ADDR = 0x400;
        const uint16_t REG_COUNT = 0x50;  // 80 registrů
        
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, BASE_ADDR, REG_COUNT);
        if (!response.isValid)
        {
            LOGW("MIC GEN2: Failed to read input registers 0x400-0x44F");
            return false;
        }
        
        // PV Voltage & Current (registry 0x400-0x403, offset 0x00-0x03)
        uint16_t pv1Voltage = response.readUInt16(0x00);  // 0x400: 0.1V scale
        uint16_t pv2Voltage = response.readUInt16(0x01);  // 0x401: 0.1V scale
        uint16_t pv1Current = response.readUInt16(0x02);  // 0x402: 0.1A scale
        uint16_t pv2Current = response.readUInt16(0x03);  // 0x403: 0.1A scale
        
        // Inverter Voltage & Frequency (registry 0x404, 0x407)
        uint16_t inverterVoltage = response.readUInt16(0x04);  // 0x404: 0.1V scale
        uint16_t inverterFrequency = response.readUInt16(0x07);  // 0x407: 0.01Hz scale
        
        // Inverter Current (0x40A, offset 0x0A)
        uint16_t inverterCurrent = response.readUInt16(0x0A);  // 0.1A scale
        
        // Inverter Temperature (0x40D, offset 0x0D)
        data.inverterTemperature = response.readInt16(0x0D);  // °C
        
        // Inverter Power (0x40E, offset 0x0E)
        int16_t inverterPower = response.readInt16(0x0E);  // W
        
        // Run Mode (0x40F, offset 0x0F)
        uint16_t runMode = response.readUInt16(0x0F);
        
        // PV Power (registry 0x414-0x415, offset 0x14-0x15)
        data.pv1Power = response.readUInt16(0x14);  // 0x414: W
        data.pv2Power = response.readUInt16(0x15);  // 0x415: W
        
        LOGD("MIC GEN2 PV1: %dW (%.1fV, %.1fA), PV2: %dW (%.1fV, %.1fA)", 
             data.pv1Power, pv1Voltage/10.0f, pv1Current/10.0f,
             data.pv2Power, pv2Voltage/10.0f, pv2Current/10.0f);
        LOGD("MIC GEN2 Inverter: %dW, %.1fV, %.1fA, %.2fHz, %d°C, RunMode: %d", 
             inverterPower, inverterVoltage/10.0f, inverterCurrent/10.0f,
             inverterFrequency/100.0f, data.inverterTemperature, runMode);
        
        // Total Yield (0x423-0x424, offset 0x23-0x24) - U32 LSB, 0.1kWh scale
        data.pvTotal = response.readUInt32LSB(0x23) / 10.0f;  // kWh
        
        // Today's Yield (0x425-0x426, offset 0x25-0x26) - U32 LSB, 0.1kWh scale
        data.pvToday = response.readUInt32LSB(0x25) / 10.0f;  // kWh
        
        LOGD("MIC GEN2 Yield: Total=%.1fkWh, Today=%.1fkWh", data.pvTotal, data.pvToday);
        
        // Pro X1 (single-phase): inverter power = grid power L1
        // MIC bez měřiče nemá přesné grid buy/sell data
        data.gridPowerL1 = inverterPower;  // Výkon do sítě = výkon střídače
        data.gridPowerL2 = 0;
        data.gridPowerL3 = 0;
        
        data.inverterOutpuPowerL1 = inverterPower;
        data.inverterOutpuPowerL2 = 0;
        data.inverterOutpuPowerL3 = 0;
        
        // Pokusíme se číst Measured Power (export/import) - některé GEN2 to mají
        // Registry 0x43B-0x43C (offset 0x3B-0x3C)
        int32_t measuredPower = response.readInt32LSB(0x3B);  // W (záporné = export)
        
        // Grid Export/Import totals (0x43D-0x440, offset 0x3D-0x40)
        float gridExportTotal = response.readUInt32LSB(0x3D) / 100.0f;  // kWh
        float gridImportTotal = response.readUInt32LSB(0x3F) / 100.0f;  // kWh
        
        if (gridExportTotal > 0 || gridImportTotal > 0)
        {
            LOGD("MIC GEN2 Grid: MeasuredPower=%dW, ExportTotal=%.2fkWh, ImportTotal=%.2fkWh", 
                 measuredPower, gridExportTotal, gridImportTotal);
            data.gridSellTotal = gridExportTotal;
            data.gridBuyTotal = gridImportTotal;
        }
        else
        {
            // Fallback - aproximace
            data.gridSellTotal = data.pvTotal;
            data.gridBuyTotal = 0;
        }
        data.gridSellToday = 0;  // Nemáme denní data
        data.gridBuyToday = 0;
        
        // MIC nemá baterii - nulové hodnoty
        data.soc = 0;
        data.batteryPower = 0;
        data.batteryVoltage = 0;
        data.batteryTemperature = 0;
        data.batteryChargedToday = 0;
        data.batteryDischargedToday = 0;
        data.batteryCapacityWh = 0;
        data.maxChargePowerW = 0;
        data.maxDischargePowerW = 0;
        data.minSoc = 0;
        data.maxSoc = 0;
        
        // Load power - výroba minus export (pokud máme měřič)
        if (measuredPower != 0)
        {
            // measuredPower záporné = export do sítě
            data.loadPower = (data.pv1Power + data.pv2Power) + measuredPower;  
            if (data.loadPower < 0) data.loadPower = 0;
        }
        else
        {
            data.loadPower = 0;  // Bez měřiče nevíme spotřebu
        }
        
        // Doplnění chybějících polí
        data.pv3Power = 0;
        data.pv4Power = 0;
        data.batteryChargedTotal = 0;
        data.batteryDischargedTotal = 0;
        data.loadTotal = 0;
        
        data.inverterMode = INVERTER_MODE_SELF_USE;
        
        // Aktualizace denních čítačů (vypočítá gridSellToday, gridBuyToday, loadToday)
        updateDailyCounters(data, -1);  // -1 = použij RTC nebo systémový čas
        
        return true;
    }
    
    /**
     * Čte data z MIC GEN4 střídače (X1-Boost-G4 XB4/ZA4, X1-Mini-G4 XM4, X1-SMART-G2 XST)
     * 
     * Registrová mapa MIC GEN4 (input registry od 0x400):
     * 0x400: Inverter Voltage (0.1V)
     * 0x403: Inverter Current (0.1A)
     * 0x406: Inverter Frequency (0.01Hz)
     * 0x408: CT Power (S16, W)
     * 0x409: Measured Power (W)
     * 0x40A-0x40C: PV Voltage 1/2/3 (0.1V)
     * 0x40D-0x40F: PV Current 1/2/3 (0.1A)
     * 0x410-0x412: PV Power 1/2/3 (W)
     * 0x413: Inverter Temperature (°C)
     * 0x415: Run Mode
     * 0x42B-0x42C: Total Yield (U32, 0.1kWh)
     * 0x42F-0x430: Total Grid Export (U32, 0.1kWh)
     * 0x431-0x432: Total Grid Import (U32, 0.1kWh)
     * 0x437: Today's Yield (U16, 0.1kWh)
     */
    bool readMicGen4InverterData(InverterData_t &data)
    {
        const uint16_t BASE_ADDR = 0x400;
        const uint16_t REG_COUNT = 0x40;  // 64 registrů
        
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, BASE_ADDR, REG_COUNT);
        if (!response.isValid)
        {
            LOGW("MIC GEN4: Failed to read input registers 0x400-0x43F");
            return false;
        }
        
        // Inverter Voltage (0x400, offset 0x00)
        uint16_t inverterVoltage = response.readUInt16(0x00);  // 0.1V scale
        
        // Inverter Current (0x403, offset 0x03)
        uint16_t inverterCurrent = response.readUInt16(0x03);  // 0.1A scale
        
        // Inverter Frequency (0x406, offset 0x06)
        uint16_t inverterFrequency = response.readUInt16(0x06);  // 0.01Hz scale
        
        // CT Power (0x408, offset 0x08) - měření z CT senzoru
        int16_t ctPower = response.readInt16(0x08);  // W
        
        // Measured Power (0x409, offset 0x09)
        int16_t measuredPower = response.readInt16(0x09);  // W
        
        // PV Voltage (0x40A-0x40C, offset 0x0A-0x0C)
        uint16_t pv1Voltage = response.readUInt16(0x0A);  // 0.1V scale
        uint16_t pv2Voltage = response.readUInt16(0x0B);  // 0.1V scale
        
        // PV Current (0x40D-0x40F, offset 0x0D-0x0F)
        uint16_t pv1Current = response.readUInt16(0x0D);  // 0.1A scale
        uint16_t pv2Current = response.readUInt16(0x0E);  // 0.1A scale
        
        // PV Power (0x410-0x412, offset 0x10-0x12)
        data.pv1Power = response.readUInt16(0x10);  // W
        data.pv2Power = response.readUInt16(0x11);  // W
        
        // Inverter Temperature (0x413, offset 0x13)
        data.inverterTemperature = response.readInt16(0x13);  // °C
        
        // Run Mode (0x415, offset 0x15)
        uint16_t runMode = response.readUInt16(0x15);
        
        LOGD("MIC GEN4 PV1: %dW (%.1fV, %.1fA), PV2: %dW (%.1fV, %.1fA)", 
             data.pv1Power, pv1Voltage/10.0f, pv1Current/10.0f,
             data.pv2Power, pv2Voltage/10.0f, pv2Current/10.0f);
        LOGD("MIC GEN4 Inverter: CT=%dW, Measured=%dW, %.1fV, %.1fA, %.2fHz, %d°C, RunMode: %d", 
             ctPower, measuredPower, inverterVoltage/10.0f, inverterCurrent/10.0f,
             inverterFrequency/100.0f, data.inverterTemperature, runMode);
        
        // Total Yield (0x42B-0x42C, offset 0x2B-0x2C)
        data.pvTotal = response.readUInt32LSB(0x2B) / 10.0f;  // kWh
        
        // Total Grid Export (0x42F-0x430, offset 0x2F-0x30)
        data.gridSellTotal = response.readUInt32LSB(0x2F) / 10.0f;  // kWh
        
        // Total Grid Import (0x431-0x432, offset 0x31-0x32)
        data.gridBuyTotal = response.readUInt32LSB(0x31) / 10.0f;  // kWh
        
        // Today's Yield (0x437, offset 0x37)
        data.pvToday = response.readUInt16(0x37) / 10.0f;  // kWh
        
        LOGD("MIC GEN4 Yield: Total=%.1fkWh, Today=%.1fkWh, Export=%.1fkWh, Import=%.1fkWh", 
             data.pvTotal, data.pvToday, data.gridSellTotal, data.gridBuyTotal);
        
        // Grid power
        data.gridPowerL1 = measuredPower;
        data.gridPowerL2 = 0;
        data.gridPowerL3 = 0;
        
        data.inverterOutpuPowerL1 = data.pv1Power + data.pv2Power;
        data.inverterOutpuPowerL2 = 0;
        data.inverterOutpuPowerL3 = 0;
        
        data.gridSellToday = 0;
        data.gridBuyToday = 0;
        
        // MIC nemá baterii
        data.soc = 0;
        data.batteryPower = 0;
        data.batteryVoltage = 0;
        data.batteryTemperature = 0;
        data.batteryChargedToday = 0;
        data.batteryDischargedToday = 0;
        data.batteryCapacityWh = 0;
        data.maxChargePowerW = 0;
        data.maxDischargePowerW = 0;
        data.minSoc = 0;
        data.maxSoc = 0;
        
        // Load = výroba - export (ctPower ukazuje tok do sítě)
        data.loadPower = (data.pv1Power + data.pv2Power) - ctPower;
        if (data.loadPower < 0) data.loadPower = 0;
        
        // Doplnění chybějících polí
        data.pv3Power = 0;  // GEN4 může mít PV3, ale zatím neimplementováno
        data.pv4Power = 0;
        data.batteryChargedTotal = 0;
        data.batteryDischargedTotal = 0;
        data.loadTotal = 0;
        
        data.inverterMode = INVERTER_MODE_SELF_USE;
        
        // Aktualizace denních čítačů (vypočítá gridSellToday, gridBuyToday, loadToday)
        updateDailyCounters(data, -1);
        
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
        
        // Pro GEN5/GEN6 zkusíme přečíst druhou baterii
        // Registry Battery 2: 0x127 voltage, 0x128 current, 0x129 power, 0x12D SOC, 0x132 temp
        if (isGen5OrGen6() && inverterCategory == SOLAX_CATEGORY_HYBRID)
        {
            readBattery2Data(data);
        }
        
        return true;
    }
    
    /**
     * Čte data z druhé baterie (Battery 2) pro GEN5/GEN6 HYBRID střídače
     * Registry: 0x127-0x132
     * Pokud je druhá baterie detekována (napětí > 0), agreguje hodnoty:
     * - batteryPower = bat1 + bat2
     * - soc = průměr
     * - batteryTemperature = maximum
     */
    void readBattery2Data(InverterData_t &data)
    {
        // Čteme registry 0x127 až 0x132 (6 registrů)
        // 0x127: Battery 2 Voltage (0.1V, S16)
        // 0x128: Battery 2 Current (0.1A, S16)  
        // 0x129: Battery 2 Power (W, S16)
        // 0x12A-0x12C: reserved/unused
        // 0x12D: Battery 2 SOC (%)
        // 0x12E-0x131: reserved
        // 0x132: Battery 2 Temperature (°C, S16)
        
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x127, 0x132 - 0x127 + 1);
        if (!response.isValid)
        {
            LOGD("Battery 2 read failed, assuming single battery");
            return;
        }
        
        // Offset v odpovědi je relativní k 0x127
        float battery2Voltage = response.readInt16(0x127) / 10.0f;
        
        // Detekce druhé baterie podle napětí
        if (battery2Voltage <= 0.0f)
        {
            LOGD("Battery 2 not detected (voltage=%.1fV)", battery2Voltage);
            return;
        }
        
        // Druhá baterie je připojena - čteme ostatní hodnoty
        int16_t battery2Power = response.readInt16(0x129);
        uint16_t battery2Soc = response.readUInt16(0x12D);
        int16_t battery2Temperature = response.readInt16(0x132);
        
        LOGD("Battery 2 detected: %.1fV, %dW, SOC=%d%%, Temp=%d°C", 
             battery2Voltage, battery2Power, battery2Soc, battery2Temperature);
        
        // Uložíme původní hodnoty Battery 1
        int16_t battery1Power = data.batteryPower;
        int battery1Soc = data.soc;
        int battery1Temperature = data.batteryTemperature;
        
        // Agregace hodnot
        // Power: součet obou baterií
        data.batteryPower = battery1Power + battery2Power;
        
        // SOC: průměr obou baterií
        data.soc = (battery1Soc + battery2Soc) / 2;
        
        // Teplota: maximum z obou (vyšší teplota je důležitější pro sledování)
        data.batteryTemperature = max(battery1Temperature, (int)battery2Temperature);
        
        // Napětí: ponecháme Battery 1 (nebo bychom mohli průměrovat)
        // data.batteryVoltage zůstává z Battery 1
        
        LOGD("Dual battery aggregated: Power=%dW (B1:%d + B2:%d), SOC=%d%% (avg B1:%d B2:%d), Temp=%d°C (max B1:%d B2:%d)",
             data.batteryPower, battery1Power, battery2Power,
             data.soc, battery1Soc, battery2Soc,
             data.batteryTemperature, battery1Temperature, battery2Temperature);
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
        
        if (isThreePhase)
        {
            // X3 (třífázový) - čteme výkon z registrů 0x6C, 0x70, 0x74
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
        }
        else
        {
            // X1 (jednofázový) - ponecháme hodnotu z registru 0x02 (vyčteno v readMainInverterData)
            // Registry 0x6C, 0x70, 0x74 jsou pro X3 a u X1 vrací 0
            LOGD("X1 inverter: keeping inverterOutpuPowerL1 from register 0x02 = %d", data.inverterOutpuPowerL1);
        }

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
        // Always prefer user-specified IP address over cached one
        if (!ipAddress.isEmpty())
        {
            IPAddress newIp;
            if (newIp.fromString(ipAddress))
            {
                if (ip != newIp)
                {
                    LOGD("Using IP from settings: %s (was: %s)", ipAddress.c_str(), ip.toString().c_str());
                }
                ip = newIp;
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

        LOGD("Using IP: %s", ip.toString().c_str());
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

    /**
     * Detekuje typ střídače z SN a nastaví inverterCategory a inverterGeneration.
     * Volá se automaticky v readInverterInfo() po načtení SN.
     * Prefixy podle: https://github.com/wills106/homeassistant-solax-modbus/blob/main/custom_components/solax_modbus/plugin_solax.py
     */
    void detectInverterType(const String &serialNumber)
    {
        if (serialNumber.length() < 3)
        {
            LOGW("SN too short for type detection: %s", serialNumber.c_str());
            inverterCategory = SOLAX_CATEGORY_HYBRID;  // Default
            inverterGeneration = SOLAX_GEN4;
            return;
        }
        
        String prefix3 = serialNumber.substring(0, 3);
        String prefix2 = serialNumber.substring(0, 2);
        LOGD("Detecting inverter type from SN: %s (prefix3=%s, prefix2=%s)", 
             serialNumber.c_str(), prefix3.c_str(), prefix2.c_str());
        
        // === MIC X1 (single-phase, no battery) ===
        
        // MIC GEN4: X1-Boost G4 (XB4, ZA4), X1-Mini G4 (XM4), X1-SMART-G2 (XST)
        if (prefix3 == "XB4" || prefix3 == "ZA4" || prefix3 == "XM4" || prefix3 == "XST")
        {
            inverterCategory = SOLAX_CATEGORY_MIC;
            inverterGeneration = SOLAX_GEN4;
            LOGI("Detected MIC GEN4 X1 inverter: %s", serialNumber.c_str());
            return;
        }
        
        // MIC GEN2: X1-Boost (XAU, XB3, XBE, XBU), X1-Mini (XMA, XM3, XAT)
        if (prefix3 == "XAU" || prefix3 == "XB3" || prefix3 == "XBE" || prefix3 == "XBU" ||
            prefix3 == "XMA" || prefix3 == "XM3" || prefix3 == "XAT")
        {
            inverterCategory = SOLAX_CATEGORY_MIC;
            inverterGeneration = SOLAX_GEN2;
            LOGI("Detected MIC GEN2 X1 inverter: %s", serialNumber.c_str());
            return;
        }
        
        // === MIC X3 (three-phase, no battery) ===
        // X3-MIC má prefixy: MC..., MU..., MP...
        if (prefix2 == "MC" || prefix2 == "MU" || prefix2 == "MP")
        {
            inverterCategory = SOLAX_CATEGORY_MIC;
            // GEN2 pro MC806T, MC106T, MC204T, MC205T, MC206T, MC208T, MC210T, MC212T, MC215T, MP156T, MPT...
            // GEN1 pro ostatní (MC103T, MC203T, MC402T, MC502T, MC602T, MC702T, MC802T, MC803T, MC902T...)
            if (serialNumber.length() >= 5)
            {
                String prefix5 = serialNumber.substring(0, 5);
                if (prefix5 == "MC806" || prefix5 == "MU806" || prefix5 == "MC106" || 
                    prefix5 == "MC204" || prefix5 == "MC205" || prefix5 == "MC206" || 
                    prefix5 == "MC208" || prefix5 == "MC210" || prefix5 == "MC212" || 
                    prefix5 == "MC215" || prefix5 == "MP156" || prefix3 == "MPT")
                {
                    inverterGeneration = SOLAX_GEN2;
                }
                else
                {
                    inverterGeneration = SOLAX_GEN_UNKNOWN;  // GEN1 nebo neznámý
                }
            }
            else
            {
                inverterGeneration = SOLAX_GEN_UNKNOWN;
            }
            LOGI("Detected MIC X3 inverter: %s", serialNumber.c_str());
            return;
        }
        
        // === HYBRID (with battery) ===
        
        // HYBRID GEN5: X3-Ultra, X3-IES (H35..., H3B...)
        if (prefix3 == "H35" || prefix3 == "H3B")
        {
            inverterCategory = SOLAX_CATEGORY_HYBRID;
            inverterGeneration = SOLAX_GEN5;
            isThreePhase = true;
            LOGI("Detected HYBRID GEN5 inverter (X3-Ultra/IES): %s", serialNumber.c_str());
            return;
        }
        
        // HYBRID GEN5: X1-IES (H53, H55, H56, H58)
        if (prefix3 == "H53" || prefix3 == "H55" || prefix3 == "H56" || prefix3 == "H58")
        {
            inverterCategory = SOLAX_CATEGORY_HYBRID;
            inverterGeneration = SOLAX_GEN5;
            isThreePhase = false;
            LOGI("Detected HYBRID GEN5 X1-IES inverter: %s", serialNumber.c_str());
            return;
        }
        
        // HYBRID GEN6: X3-HYB-G4 PRO (10K...), X1-VAST (10M...)
        if (prefix3 == "10K" || prefix3 == "10M")
        {
            inverterCategory = SOLAX_CATEGORY_HYBRID;
            inverterGeneration = SOLAX_GEN6;
            isThreePhase = (prefix3 == "10K");
            LOGI("Detected HYBRID GEN6 inverter: %s", serialNumber.c_str());
            return;
        }
        
        // HYBRID GEN4: X1-Hybrid G4 (H43, H44, H45, H46, H47)
        if (prefix3 == "H43" || prefix3 == "H44" || prefix3 == "H45" || prefix3 == "H46" || prefix3 == "H47")
        {
            inverterCategory = SOLAX_CATEGORY_HYBRID;
            inverterGeneration = SOLAX_GEN4;
            isThreePhase = false;
            LOGI("Detected HYBRID GEN4 X1 inverter: %s", serialNumber.c_str());
            return;
        }
        
        // HYBRID GEN4: X3-Hybrid G4 (H31, H34)
        if (prefix3 == "H31" || prefix3 == "H34")
        {
            inverterCategory = SOLAX_CATEGORY_HYBRID;
            inverterGeneration = SOLAX_GEN4;
            isThreePhase = true;
            LOGI("Detected HYBRID GEN4 X3 inverter: %s", serialNumber.c_str());
            return;
        }
        
        // HYBRID GEN3: X1-Hybrid G3 (H1E, H1I, HCC, HUE, XRE)
        if (prefix3 == "H1E" || prefix3 == "H1I" || prefix3 == "HCC" || prefix3 == "HUE" || prefix3 == "XRE")
        {
            inverterCategory = SOLAX_CATEGORY_HYBRID;
            inverterGeneration = SOLAX_GEN3;
            isThreePhase = false;
            LOGI("Detected HYBRID GEN3 X1 inverter: %s", serialNumber.c_str());
            return;
        }
        
        // HYBRID GEN3: X3-Hybrid G3 (H3D, H3E, H3L, H3P, H3U)
        if (prefix3 == "H3D" || prefix3 == "H3E" || prefix3 == "H3L" || prefix3 == "H3P" || prefix3 == "H3U")
        {
            inverterCategory = SOLAX_CATEGORY_HYBRID;
            inverterGeneration = SOLAX_GEN3;
            isThreePhase = true;
            LOGI("Detected HYBRID GEN3 X3 inverter: %s", serialNumber.c_str());
            return;
        }
        
        // HYBRID GEN2: X1-Hybrid G2 (L30, L37, L50, U30, U50)
        if (prefix3 == "L30" || prefix3 == "L37" || prefix3 == "L50" || prefix3 == "U30" || prefix3 == "U50")
        {
            inverterCategory = SOLAX_CATEGORY_HYBRID;
            inverterGeneration = SOLAX_GEN2;
            isThreePhase = false;
            LOGI("Detected HYBRID GEN2 X1 inverter: %s", serialNumber.c_str());
            return;
        }
        
        // AC/RetroFit (F43, F45, F46, F47, PRI, PRE, XAC)
        if (prefix3 == "F43" || prefix3 == "F45" || prefix3 == "F46" || prefix3 == "F47" || 
            prefix3 == "PRI" || prefix3 == "PRE" || prefix3 == "XAC")
        {
            // AC coupled, ale má baterii
            inverterCategory = SOLAX_CATEGORY_HYBRID;
            inverterGeneration = (prefix3.startsWith("F") || prefix3 == "PRE") ? SOLAX_GEN4 : SOLAX_GEN3;
            isThreePhase = !(prefix3.startsWith("F"));  // F4x jsou X1, ostatní X3
            LOGI("Detected AC/RetroFit inverter: %s", serialNumber.c_str());
            return;
        }
        
        // Ostatní H3x prefixy - pravděpodobně HYBRID
        if (prefix2 == "H3" || prefix2 == "H4" || prefix2 == "H5")
        {
            inverterCategory = SOLAX_CATEGORY_HYBRID;
            inverterGeneration = SOLAX_GEN4;
            isThreePhase = (prefix2 == "H3");  // H3x jsou X3, H4x/H5x jsou X1
            LOGI("Detected HYBRID inverter (H-prefix): %s", serialNumber.c_str());
            return;
        }
        
        // Default: HYBRID GEN4 (původní chování)
        inverterCategory = SOLAX_CATEGORY_HYBRID;
        inverterGeneration = SOLAX_GEN4;
        isThreePhase = false;  // Default na X1 pro bezpečnější chování
        LOGI("Unknown SN prefix, defaulting to HYBRID GEN4: %s", serialNumber.c_str());
    }
    
    /**
     * Kontrola jestli je střídač GEN5 (pro škálování teploty)
     */
    bool isGen5(const String &serialNumber)
    {
        return inverterGeneration == SOLAX_GEN5;
    }
    
    /**
     * Kontrola jestli je střídač GEN5 nebo GEN6 (pro podporu duální baterie)
     */
    bool isGen5OrGen6()
    {
        return inverterGeneration == SOLAX_GEN5 || inverterGeneration == SOLAX_GEN6;
    }
};