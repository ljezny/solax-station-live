#pragma once

#include <Arduino.h>

typedef enum { POWER, ENERGY, PERCENT } Unit_t;
typedef struct FormattedUnit
{
    String unit;
    String value;
    String formatted;
} FormattedUnit_t;

FormattedUnit_t format(Unit_t unit, float value, float limitingFactor = 1.0f, bool compact = false, int padLeft = 0) {
    FormattedUnit_t formattedUnit;
    String space = compact ? "" : " ";
    switch(unit) {
        case POWER:
            if (abs(value) >= limitingFactor * 10 * 1000 * 1000) {
                formattedUnit.value = String(value / (1000.0f * 1000.0f),0);
                formattedUnit.unit = "MW";
            } else if (abs(value) >= limitingFactor * 1 * 1000 * 1000) {
                formattedUnit.value = String(value / (1000.0f * 1000.0f),1);
                formattedUnit.unit = "MW";
            } else if (abs(value) >= limitingFactor * 10 * 1000) {
                formattedUnit.value = String(value / 1000.0f,1);
                formattedUnit.unit = "kW";
            } 
            else {
                formattedUnit.value = String(value,0);
                formattedUnit.unit = "W";
            }
            break;
        case ENERGY:
            if (abs(value) >= limitingFactor * 10 * 1000 * 1000) {
                formattedUnit.value = String(value / (1000.0f * 1000.0f),0);
                formattedUnit.unit = "MWh";
            } else if (abs(value) >= limitingFactor * 1 * 1000 * 1000) {
                formattedUnit.value = String(value / (1000.0f * 1000.0f),1);
                formattedUnit.unit = "MWh";
            } else if (abs(value) >= limitingFactor * 10 * 1000) {
                formattedUnit.value = String(value / 1000.0f,0);
                formattedUnit.unit = "kWh";
            } else if (abs(value) >= limitingFactor * 1 * 1000) {
                formattedUnit.value = String(value / 1000.0f,1);
                formattedUnit.unit = "kWh";
            } else {
                formattedUnit.value = String(value,0);
                formattedUnit.unit = "Wh";
            }
            break;
        case PERCENT:
            formattedUnit.value = String(value,0);
            formattedUnit.unit = "%";
            break;
    }
    formattedUnit.value.replace(".", ",");
    formattedUnit.formatted = formattedUnit.value + space + formattedUnit.unit;
    return formattedUnit;
}
