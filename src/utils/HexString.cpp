#include "HexString.h"

String dataToHexString(unsigned char *data, int dataLength) {
    String hashHex = "";
    for(int i = 0; i < dataLength; i++) {
        if(data[i] < 0x10) {
            hashHex += '0';
        }
        hashHex += String(data[i], HEX);
    }
    return hashHex;
}