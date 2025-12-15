#pragma once

#include <Arduino.h>
#include "RemoteLogger.hpp"
#include <Preferences.h>
#include "FlashMutex.hpp"

/**
 * Supported languages
 */
typedef enum {
    LANG_EN = 0,  // English (default)
    LANG_DE = 1,  // German
    LANG_CS = 2,  // Czech
    LANG_COUNT
} Language_t;

/**
 * String IDs for localization
 */
typedef enum {
    // General
    STR_LOADING,
    STR_WAITING,
    STR_ERROR,
    STR_OK,
    STR_CANCEL,
    STR_SAVE,
    STR_RESET,
    STR_YES,
    STR_NO,
    STR_ON,
    STR_OFF,
    
    // Dashboard - Status
    STR_CONNECTION_ERROR,
    STR_HTTP_ERROR,
    STR_JSON_ERROR,
    STR_WIFI_DISCONNECTED,
    STR_UNKNOWN_ERROR,
    STR_NO_WIFI,
    
    // Dashboard - Intelligence
    STR_INTELLIGENCE,
    STR_INVERTER_MODE,
    STR_NORMAL,
    STR_CHARGE,
    STR_DISCHARGE,
    STR_HOLD,
    STR_PRODUCTION,
    STR_CONSUMPTION,
    
    // Dashboard - Units
    STR_KWH,
    STR_KW,
    STR_PERCENT,
    
    // WiFi Setup
    STR_WIFI_SETUP,
    STR_SELECT_WIFI,
    STR_PASSWORD,
    STR_INVERTER_IP,
    STR_INVERTER_SN,
    STR_CONNECTION_TYPE,
    STR_SPOT_PROVIDER,
    STR_TIMEZONE,
    STR_LANGUAGE,
    STR_CONNECT,
    
    // Intelligence Setup
    STR_INTELLIGENCE_SETUP,
    STR_ENABLE_INTELLIGENCE,
    STR_BATTERY_COST,
    STR_MIN_SOC,
    STR_MAX_SOC,
    STR_BUY_COEFFICIENT,
    STR_BUY_CONSTANT,
    STR_SELL_COEFFICIENT,
    STR_SELL_CONSTANT,
    STR_BATTERY_CAPACITY,
    STR_MAX_CHARGE_POWER,
    STR_MAX_DISCHARGE_POWER,
    STR_RESET_PREDICTIONS,
    
    // Splash
    STR_DISCOVERING_DONGLES,
    STR_CONNECTING,
    STR_LOADING_DATA,
    STR_FAILED_LOAD_DATA,
    STR_FAILED_CONNECT,
    
    // Days of week
    STR_MONDAY,
    STR_TUESDAY,
    STR_WEDNESDAY,
    STR_THURSDAY,
    STR_FRIDAY,
    STR_SATURDAY,
    STR_SUNDAY,
    
    // Misc
    STR_TODAY,
    STR_TOMORROW,
    STR_NOW,
    STR_SAVINGS,
    
    // WiFi Setup - Card titles
    STR_WIFI_NETWORK,
    STR_INVERTER,
    STR_SPOT_PRICE,
    STR_SETUP,
    STR_IP_ADDRESS,
    STR_SERIAL_NUMBER,
    
    // Intelligence Setup - Card titles
    STR_BATTERY,
    STR_BUY_PRICE,
    STR_SELL_PRICE,
    STR_INTELLIGENCE_SETTINGS,
    STR_ENABLE,
    STR_COST_PER_KWH,
    STR_MIN_SOC_PERCENT,
    STR_MAX_SOC_PERCENT,
    STR_CAPACITY_KWH,
    STR_MAX_CHARGE_KW,
    STR_MAX_DISCHARGE_KW,
    STR_K_MULTIPLIER,
    STR_Q_FIXED,
    STR_BUY_INFO,
    STR_SELL_INFO,
    STR_PRICE_FORMULA,
    
    // Intelligence reasons (with price)
    STR_BATTERY_CHEAPER_THAN,     // "battery cheaper than %.1f %s"
    STR_LOW_PRICE,                // "low price %.1f %s"
    STR_HIGH_PRICE,               // "high price %.1f %s"
    STR_WAITING_BETTER_PRICE,     // "waiting for better price"
    
    // Intelligence reasons (without price)
    STR_USING_BATTERY,            // "using battery"
    STR_LOW_ELECTRICITY_PRICE,    // "low electricity price"
    STR_HIGH_ELECTRICITY_PRICE,   // "high electricity price"
    STR_HOLDING_FOR_LATER,        // "holding for later"
    
    // Currencies (localized)
    STR_CURRENCY_CZK,             // "CZK" / "Kč"
    STR_CURRENCY_EUR_CENT,        // "ct" (euro cents)
    STR_CURRENCY_PLN_GR,          // "gr" (grosze)
    STR_CURRENCY_SEK_ORE,         // "öre"
    
    // General settings
    STR_GENERAL,                  // "General" / "Obecné"
    STR_DISPLAY_TIMEOUT,          // "Display off" / "Vypnutí displeje"
    STR_NEVER,                    // "Never" / "Nikdy"
    
    // Beta warning
    STR_BETA_WARNING,             // Beta feature warning text
    
    STR_COUNT  // Must be last
} StringId_t;

/**
 * Localization singleton class
 */
class Localization {
private:
    static Language_t currentLanguage;
    static bool initialized;
    
    // English strings
    static const char* const stringsEN[];
    // German strings
    static const char* const stringsDE[];
    // Czech strings
    static const char* const stringsCS[];
    
public:
    /**
     * Initialize localization system and load saved language
     */
    static void init() {
        if (initialized) return;
        
        FlashGuard guard;
        if (guard.isLocked()) {
            Preferences prefs;
            if (prefs.begin("locale", true)) {
                currentLanguage = (Language_t)prefs.getInt("lang", LANG_EN);
                if (currentLanguage >= LANG_COUNT) {
                    currentLanguage = LANG_EN;
                }
                prefs.end();
            }
        }
        initialized = true;
        LOGD("Localization initialized, language: %d", currentLanguage);
    }
    
    /**
     * Get current language
     */
    static Language_t getLanguage() {
        if (!initialized) init();
        return currentLanguage;
    }
    
    /**
     * Set current language and save to NVS
     */
    static void setLanguage(Language_t lang) {
        if (lang >= LANG_COUNT) return;
        currentLanguage = lang;
        
        FlashGuard guard;
        if (guard.isLocked()) {
            Preferences prefs;
            if (prefs.begin("locale", false)) {
                prefs.putInt("lang", (int)lang);
                prefs.end();
            }
        }
        LOGD("Language set to: %d", lang);
    }
    
    /**
     * Get translated string by ID
     */
    static const char* tr(StringId_t id) {
        if (!initialized) init();
        if (id >= STR_COUNT) return "???";
        
        switch (currentLanguage) {
            case LANG_DE:
                return stringsDE[id];
            case LANG_CS:
                return stringsCS[id];
            case LANG_EN:
            default:
                return stringsEN[id];
        }
    }
    
    /**
     * Get language name for display
     */
    static const char* getLanguageName(Language_t lang) {
        switch (lang) {
            case LANG_EN: return "English";
            case LANG_DE: return "Deutsch";
            case LANG_CS: return "Čeština";
            default: return "Unknown";
        }
    }
    
    /**
     * Get all language names as newline-separated string for dropdown
     */
    static String getLanguageOptions() {
        String options;
        for (int i = 0; i < LANG_COUNT; i++) {
            options += getLanguageName((Language_t)i);
            if (i < LANG_COUNT - 1) options += "\n";
        }
        return options;
    }
};

// Static member initialization
Language_t Localization::currentLanguage = LANG_EN;
bool Localization::initialized = false;

// English strings
const char* const Localization::stringsEN[] = {
    // General
    "Loading...",           // STR_LOADING
    "Waiting",              // STR_WAITING
    "Error",                // STR_ERROR
    "OK",                   // STR_OK
    "Cancel",               // STR_CANCEL
    "Save",                 // STR_SAVE
    "Reset",                // STR_RESET
    "Yes",                  // STR_YES
    "No",                   // STR_NO
    "On",                   // STR_ON
    "Off",                  // STR_OFF
    
    // Dashboard - Status
    "Connection error",     // STR_CONNECTION_ERROR
    "HTTP error",           // STR_HTTP_ERROR
    "JSON error",           // STR_JSON_ERROR
    "WiFi disconnected",    // STR_WIFI_DISCONNECTED
    "Unknown error",        // STR_UNKNOWN_ERROR
    "No WiFi",              // STR_NO_WIFI
    
    // Dashboard - Intelligence
    "Intelligence (BETA)",  // STR_INTELLIGENCE
    "Inverter Mode",        // STR_INVERTER_MODE
    "NORMAL",               // STR_NORMAL
    "CHARGE",               // STR_CHARGE
    "DISCHARGE",            // STR_DISCHARGE
    "HOLD",                 // STR_HOLD
    "Production",           // STR_PRODUCTION
    "Consumption",          // STR_CONSUMPTION
    
    // Dashboard - Units
    "kWh",                  // STR_KWH
    "kW",                   // STR_KW
    "%",                    // STR_PERCENT
    
    // WiFi Setup
    "WiFi Setup",           // STR_WIFI_SETUP
    "Select WiFi",          // STR_SELECT_WIFI
    "Password:",            // STR_PASSWORD
    "Inverter IP",          // STR_INVERTER_IP
    "Inverter S/N",         // STR_INVERTER_SN
    "Connection Type",      // STR_CONNECTION_TYPE
    "Spot Provider",        // STR_SPOT_PROVIDER
    "Time Zone:",           // STR_TIMEZONE
    "Language:",            // STR_LANGUAGE
    "Connect",              // STR_CONNECT
    
    // Intelligence Setup
    "Intelligence (BETA) Setup", // STR_INTELLIGENCE_SETUP
    "Enable Intelligence (BETA)", // STR_ENABLE_INTELLIGENCE
    "Battery Cost",         // STR_BATTERY_COST
    "Min SOC",              // STR_MIN_SOC
    "Max SOC",              // STR_MAX_SOC
    "Buy Coefficient",      // STR_BUY_COEFFICIENT
    "Buy Constant",         // STR_BUY_CONSTANT
    "Sell Coefficient",     // STR_SELL_COEFFICIENT
    "Sell Constant",        // STR_SELL_CONSTANT
    "Battery Capacity",     // STR_BATTERY_CAPACITY
    "Max Charge Power",     // STR_MAX_CHARGE_POWER
    "Max Discharge Power",  // STR_MAX_DISCHARGE_POWER
    "Reset Predictions",    // STR_RESET_PREDICTIONS
    
    // Splash
    "Discovering dongles...", // STR_DISCOVERING_DONGLES
    "Connecting...",        // STR_CONNECTING
    "Loading data...",      // STR_LOADING_DATA
    "Failed to load data :-(", // STR_FAILED_LOAD_DATA
    "Failed to connect :-(", // STR_FAILED_CONNECT
    
    // Days of week
    "Monday",               // STR_MONDAY
    "Tuesday",              // STR_TUESDAY
    "Wednesday",            // STR_WEDNESDAY
    "Thursday",             // STR_THURSDAY
    "Friday",               // STR_FRIDAY
    "Saturday",             // STR_SATURDAY
    "Sunday",               // STR_SUNDAY
    
    // Misc
    "Today",                // STR_TODAY
    "Tomorrow",             // STR_TOMORROW
    "Now",                  // STR_NOW
    "Savings",              // STR_SAVINGS
    
    // WiFi Setup - Card titles
    "WiFi Network",         // STR_WIFI_NETWORK
    "Inverter",             // STR_INVERTER
    "Spot Price",           // STR_SPOT_PRICE
    "Setup",                // STR_SETUP
    "IP Address:",          // STR_IP_ADDRESS
    "Serial Number:",       // STR_SERIAL_NUMBER
    
    // Intelligence Setup - Card titles
    "Battery",              // STR_BATTERY
    "Buy Price",            // STR_BUY_PRICE
    "Sell Price",           // STR_SELL_PRICE
    "Intelligence (BETA) Settings", // STR_INTELLIGENCE_SETTINGS
    "Enable",               // STR_ENABLE
    "Cost per kWh:",        // STR_COST_PER_KWH
    "Min SOC %:",           // STR_MIN_SOC_PERCENT
    "Max SOC %:",           // STR_MAX_SOC_PERCENT
    "Capacity kWh:",        // STR_CAPACITY_KWH
    "Max charge kW:",       // STR_MAX_CHARGE_KW
    "Max disch. kW:",       // STR_MAX_DISCHARGE_KW
    "K (multiplier):",      // STR_K_MULTIPLIER
    "Q (fixed):",           // STR_Q_FIXED
    "K=1.21 for 21% VAT\nQ = distribution fees", // STR_BUY_INFO
    "Usually K<1 (lower buyback)\nQ can be negative", // STR_SELL_INFO
    "price = K * spot + Q", // STR_PRICE_FORMULA
    
    // Intelligence reasons (with price)
    "battery cheaper than",   // STR_BATTERY_CHEAPER_THAN
    "low price",              // STR_LOW_PRICE
    "high price",             // STR_HIGH_PRICE
    "waiting for better price", // STR_WAITING_BETTER_PRICE
    
    // Intelligence reasons (without price)
    "using battery",          // STR_USING_BATTERY
    "low electricity price",  // STR_LOW_ELECTRICITY_PRICE
    "high electricity price", // STR_HIGH_ELECTRICITY_PRICE
    "holding for later",      // STR_HOLDING_FOR_LATER
    
    // Currencies
    "CZK",                    // STR_CURRENCY_CZK
    "ct",                     // STR_CURRENCY_EUR_CENT
    "gr",                     // STR_CURRENCY_PLN_GR
    "öre",                    // STR_CURRENCY_SEK_ORE
    
    // General settings
    "General",                // STR_GENERAL
    "Display off",            // STR_DISPLAY_TIMEOUT
    "Never",                  // STR_NEVER
    
    // Beta warning
    "BETA: This feature is experimental and may affect your inverter settings. Use at your own risk. The author is not responsible for any damage.",  // STR_BETA_WARNING
};

// German strings
const char* const Localization::stringsDE[] = {
    // General
    "Laden...",             // STR_LOADING
    "Warten",               // STR_WAITING
    "Fehler",               // STR_ERROR
    "OK",                   // STR_OK
    "Abbrechen",            // STR_CANCEL
    "Speichern",            // STR_SAVE
    "Zurücksetzen",         // STR_RESET
    "Ja",                   // STR_YES
    "Nein",                 // STR_NO
    "An",                   // STR_ON
    "Aus",                  // STR_OFF
    
    // Dashboard - Status
    "Verbindungsfehler",    // STR_CONNECTION_ERROR
    "HTTP-Fehler",          // STR_HTTP_ERROR
    "JSON-Fehler",          // STR_JSON_ERROR
    "WLAN getrennt",        // STR_WIFI_DISCONNECTED
    "Unbekannter Fehler",   // STR_UNKNOWN_ERROR
    "Kein WLAN",            // STR_NO_WIFI
    
    // Dashboard - Intelligence
    "Intelligenz (BETA)",   // STR_INTELLIGENCE
    "Wechselrichter-Modus", // STR_INVERTER_MODE
    "NORMAL",               // STR_NORMAL
    "LADEN",                // STR_CHARGE
    "ENTLADEN",             // STR_DISCHARGE
    "HALTEN",               // STR_HOLD
    "Produktion",           // STR_PRODUCTION
    "Verbrauch",            // STR_CONSUMPTION
    
    // Dashboard - Units
    "kWh",                  // STR_KWH
    "kW",                   // STR_KW
    "%",                    // STR_PERCENT
    
    // WiFi Setup
    "WLAN-Einrichtung",     // STR_WIFI_SETUP
    "WLAN auswählen",       // STR_SELECT_WIFI
    "Passwort:",            // STR_PASSWORD
    "Wechselrichter IP",    // STR_INVERTER_IP
    "Wechselrichter S/N",   // STR_INVERTER_SN
    "Verbindungstyp",       // STR_CONNECTION_TYPE
    "Spot-Anbieter",        // STR_SPOT_PROVIDER
    "Zeitzone:",            // STR_TIMEZONE
    "Sprache:",             // STR_LANGUAGE
    "Verbinden",            // STR_CONNECT
    
    // Intelligence Setup
    "Intelligenz (BETA) Setup", // STR_INTELLIGENCE_SETUP
    "Intelligenz (BETA) aktivieren", // STR_ENABLE_INTELLIGENCE
    "Batteriekosten",       // STR_BATTERY_COST
    "Min SOC",              // STR_MIN_SOC
    "Max SOC",              // STR_MAX_SOC
    "Kaufkoeffizient",      // STR_BUY_COEFFICIENT
    "Kaufkonstante",        // STR_BUY_CONSTANT
    "Verkaufskoeffizient",  // STR_SELL_COEFFICIENT
    "Verkaufskonstante",    // STR_SELL_CONSTANT
    "Batteriekapazität",    // STR_BATTERY_CAPACITY
    "Max Ladeleistung",     // STR_MAX_CHARGE_POWER
    "Max Entladeleistung",  // STR_MAX_DISCHARGE_POWER
    "Vorhersagen zurücksetzen", // STR_RESET_PREDICTIONS
    
    // Splash
    "Dongles werden gesucht...", // STR_DISCOVERING_DONGLES
    "Verbinden...",         // STR_CONNECTING
    "Daten laden...",       // STR_LOADING_DATA
    "Daten laden fehlgeschlagen :-(", // STR_FAILED_LOAD_DATA
    "Verbindung fehlgeschlagen :-(", // STR_FAILED_CONNECT
    
    // Days of week
    "Montag",               // STR_MONDAY
    "Dienstag",             // STR_TUESDAY
    "Mittwoch",             // STR_WEDNESDAY
    "Donnerstag",           // STR_THURSDAY
    "Freitag",              // STR_FRIDAY
    "Samstag",              // STR_SATURDAY
    "Sonntag",              // STR_SUNDAY
    
    // Misc
    "Heute",                // STR_TODAY
    "Morgen",               // STR_TOMORROW
    "Jetzt",                // STR_NOW
    "Ersparnis",            // STR_SAVINGS
    
    // WiFi Setup - Card titles
    "WLAN-Netzwerk",        // STR_WIFI_NETWORK
    "Wechselrichter",       // STR_INVERTER
    "Spotpreis",            // STR_SPOT_PRICE
    "Einrichtung",          // STR_SETUP
    "IP-Adresse:",          // STR_IP_ADDRESS
    "Seriennummer:",        // STR_SERIAL_NUMBER
    
    // Intelligence Setup - Card titles
    "Batterie",             // STR_BATTERY
    "Kaufpreis",            // STR_BUY_PRICE
    "Verkaufspreis",        // STR_SELL_PRICE
    "Intelligenz (BETA) Einstellungen", // STR_INTELLIGENCE_SETTINGS
    "Aktivieren",           // STR_ENABLE
    "Kosten pro kWh:",      // STR_COST_PER_KWH
    "Min SOC %:",           // STR_MIN_SOC_PERCENT
    "Max SOC %:",           // STR_MAX_SOC_PERCENT
    "Kapazität kWh:",       // STR_CAPACITY_KWH
    "Max Laden kW:",        // STR_MAX_CHARGE_KW
    "Max Entl. kW:",        // STR_MAX_DISCHARGE_KW
    "K (Multiplikator):",   // STR_K_MULTIPLIER
    "Q (fest):",            // STR_Q_FIXED
    "K=1,21 für 21% MwSt\nQ = Verteilungsgebühren", // STR_BUY_INFO
    "Meist K<1 (niedrigerer Rückkauf)\nQ kann negativ sein", // STR_SELL_INFO
    "Preis = K * Spot + Q", // STR_PRICE_FORMULA
    
    // Intelligence reasons (with price)
    "Batterie günstiger als", // STR_BATTERY_CHEAPER_THAN
    "niedriger Preis",        // STR_LOW_PRICE
    "hoher Preis",            // STR_HIGH_PRICE
    "warte auf besseren Preis", // STR_WAITING_BETTER_PRICE
    
    // Intelligence reasons (without price)
    "Batterie verwenden",     // STR_USING_BATTERY
    "niedriger Strompreis",   // STR_LOW_ELECTRICITY_PRICE
    "hoher Strompreis",       // STR_HIGH_ELECTRICITY_PRICE
    "halten für später",      // STR_HOLDING_FOR_LATER
    
    // Currencies
    "CZK",                    // STR_CURRENCY_CZK
    "ct",                     // STR_CURRENCY_EUR_CENT
    "gr",                     // STR_CURRENCY_PLN_GR
    "öre",                    // STR_CURRENCY_SEK_ORE
    
    // General settings
    "Allgemein",              // STR_GENERAL
    "Display aus",            // STR_DISPLAY_TIMEOUT
    "Nie",                    // STR_NEVER
    
    // Beta warning
    "BETA: Diese Funktion ist experimentell und kann Ihre Wechselrichter-Einstellungen beeinflussen. Nutzung auf eigene Gefahr. Der Autor übernimmt keine Haftung.",  // STR_BETA_WARNING
};

// Czech strings
const char* const Localization::stringsCS[] = {
    // General
    "Načítám...",           // STR_LOADING
    "Čekám",                // STR_WAITING
    "Chyba",                // STR_ERROR
    "OK",                   // STR_OK
    "Zrušit",               // STR_CANCEL
    "Uložit",               // STR_SAVE
    "Reset",                // STR_RESET
    "Ano",                  // STR_YES
    "Ne",                   // STR_NO
    "Zap",                  // STR_ON
    "Vyp",                  // STR_OFF
    
    // Dashboard - Status
    "Chyba připojení",      // STR_CONNECTION_ERROR
    "HTTP chyba",           // STR_HTTP_ERROR
    "JSON chyba",           // STR_JSON_ERROR
    "WiFi odpojeno",        // STR_WIFI_DISCONNECTED
    "Neznámá chyba",        // STR_UNKNOWN_ERROR
    "Bez WiFi",             // STR_NO_WIFI
    
    // Dashboard - Intelligence
    "Inteligence (BETA)",   // STR_INTELLIGENCE
    "Režim střídače",       // STR_INVERTER_MODE
    "NORMÁLNÍ",             // STR_NORMAL
    "NABÍJENÍ",             // STR_CHARGE
    "VYBÍJENÍ",             // STR_DISCHARGE
    "DRŽET",                // STR_HOLD
    "Výroba",               // STR_PRODUCTION
    "Spotřeba",             // STR_CONSUMPTION
    
    // Dashboard - Units
    "kWh",                  // STR_KWH
    "kW",                   // STR_KW
    "%",                    // STR_PERCENT
    
    // WiFi Setup
    "Nastavení WiFi",       // STR_WIFI_SETUP
    "Vybrat WiFi",          // STR_SELECT_WIFI
    "Heslo:",               // STR_PASSWORD
    "IP střídače",          // STR_INVERTER_IP
    "S/N střídače",         // STR_INVERTER_SN
    "Typ připojení",        // STR_CONNECTION_TYPE
    "Poskytovatel cen",     // STR_SPOT_PROVIDER
    "Časová zóna:",         // STR_TIMEZONE
    "Jazyk:",               // STR_LANGUAGE
    "Připojit",             // STR_CONNECT
    
    // Intelligence Setup
    "Nastavení inteligence (BETA)", // STR_INTELLIGENCE_SETUP
    "Povolit inteligenci (BETA)", // STR_ENABLE_INTELLIGENCE
    "Cena baterie",         // STR_BATTERY_COST
    "Min SOC",              // STR_MIN_SOC
    "Max SOC",              // STR_MAX_SOC
    "Koeficient nákupu",    // STR_BUY_COEFFICIENT
    "Konstanta nákupu",     // STR_BUY_CONSTANT
    "Koeficient prodeje",   // STR_SELL_COEFFICIENT
    "Konstanta prodeje",    // STR_SELL_CONSTANT
    "Kapacita baterie",     // STR_BATTERY_CAPACITY
    "Max výkon nabíjení",   // STR_MAX_CHARGE_POWER
    "Max výkon vybíjení",   // STR_MAX_DISCHARGE_POWER
    "Smazat predikce",      // STR_RESET_PREDICTIONS
    
    // Splash
    "Hledám zařízení...",   // STR_DISCOVERING_DONGLES
    "Připojuji...",         // STR_CONNECTING
    "Načítám data...",      // STR_LOADING_DATA
    "Nepodařilo se načíst data :-(", // STR_FAILED_LOAD_DATA
    "Připojení selhalo :-(", // STR_FAILED_CONNECT
    
    // Days of week
    "Pondělí",              // STR_MONDAY
    "Úterý",                // STR_TUESDAY
    "Středa",               // STR_WEDNESDAY
    "Čtvrtek",              // STR_THURSDAY
    "Pátek",                // STR_FRIDAY
    "Sobota",               // STR_SATURDAY
    "Neděle",               // STR_SUNDAY
    
    // Misc
    "Dnes",                 // STR_TODAY
    "Zítra",                // STR_TOMORROW
    "Teď",                  // STR_NOW
    "Úspora",               // STR_SAVINGS
    
    // WiFi Setup - Card titles
    "WiFi síť",             // STR_WIFI_NETWORK
    "Střídač",              // STR_INVERTER
    "Spotová cena",         // STR_SPOT_PRICE
    "Nastavení",            // STR_SETUP
    "IP adresa:",           // STR_IP_ADDRESS
    "Sériové číslo:",       // STR_SERIAL_NUMBER

    
    // Intelligence Setup - Card titles
    "Baterie",              // STR_BATTERY
    "Nákupní cena",         // STR_BUY_PRICE
    "Prodejní cena",        // STR_SELL_PRICE
    "Nastavení inteligence (BETA)", // STR_INTELLIGENCE_SETTINGS
    "Povolit",              // STR_ENABLE
    "Cena za kWh:",         // STR_COST_PER_KWH
    "Min SOC %:",           // STR_MIN_SOC_PERCENT
    "Max SOC %:",           // STR_MAX_SOC_PERCENT
    "Kapacita kWh:",        // STR_CAPACITY_KWH
    "Max nabíjení kW:",     // STR_MAX_CHARGE_KW
    "Max vybíjení kW:",     // STR_MAX_DISCHARGE_KW
    "K (násobitel):",       // STR_K_MULTIPLIER
    "Q (pevná):",           // STR_Q_FIXED
    "K=1,21 pro 21% DPH\nQ = distribuční poplatky", // STR_BUY_INFO
    "Obvykle K<1 (nižší výkup)\nQ může být záporné", // STR_SELL_INFO
    "cena = K * spot + Q",  // STR_PRICE_FORMULA
    
    // Intelligence reasons (with price)
    "baterie levnější než",   // STR_BATTERY_CHEAPER_THAN
    "nízká cena",             // STR_LOW_PRICE
    "vysoká cena",            // STR_HIGH_PRICE
    "čekám na lepší cenu",    // STR_WAITING_BETTER_PRICE
    
    // Intelligence reasons (without price)
    "využívám baterii",       // STR_USING_BATTERY
    "nízká cena elektřiny",   // STR_LOW_ELECTRICITY_PRICE
    "vysoká cena elektřiny",  // STR_HIGH_ELECTRICITY_PRICE
    "držím pro později",      // STR_HOLDING_FOR_LATER
    
    // Currencies
    "Kč",                     // STR_CURRENCY_CZK
    "ct",                     // STR_CURRENCY_EUR_CENT
    "gr",                     // STR_CURRENCY_PLN_GR
    "öre",                    // STR_CURRENCY_SEK_ORE
    
    // General settings
    "Obecné",                 // STR_GENERAL
    "Vypnutí displeje",       // STR_DISPLAY_TIMEOUT
    "Nikdy",                  // STR_NEVER
    
    // Beta warning
    "BETA: Tato funkce je experimentální a může ovlivnit nastavení střídače. Používáte na vlastní nebezpečí. Autor nenese odpovědnost za případné škody.",  // STR_BETA_WARNING
};

// Convenience macro for translation
#define TR(id) Localization::tr(id)
