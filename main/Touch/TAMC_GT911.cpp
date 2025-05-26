
#include "TAMC_GT911.h"

TAMC_GT911::TAMC_GT911(uint16_t _width, uint16_t _height) : width(_width), height(_height)
{
}

void TAMC_GT911::begin(i2c_master_bus_handle_t _bus_handle, uint8_t _addr)
{
  addr = _addr;
  bus_handle = _bus_handle;

  bool found = i2c_master_probe(bus_handle, addr, -1) == ESP_OK; // Example for I2C master probe;
  log_d("I2C scan for address 0x%02X: %s", addr, found ? "found" : "not found");
  if (found)
  {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = _addr,
        .scl_speed_hz = 400000,
    };
    i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
  }
}

void TAMC_GT911::setRotation(uint8_t rot)
{
  rotation = rot;
}

void TAMC_GT911::read(void)
{
  // Serial.println("TAMC_GT911::read");
  uint8_t data[7];
  uint8_t id;
  uint16_t x, y, size;

  uint8_t pointInfo = readByteData(GT911_POINT_INFO);
  uint8_t bufferStatus = pointInfo >> 7 & 1;
  uint8_t proximityValid = pointInfo >> 5 & 1;
  uint8_t haveKey = pointInfo >> 4 & 1;
  isLargeDetect = pointInfo >> 6 & 1;
  touches = pointInfo & 0xF;
  // Serial.print("bufferStatus: ");Serial.println(bufferStatus);
  // Serial.print("largeDetect: ");Serial.println(isLargeDetect);
  // Serial.print("proximityValid: ");Serial.println(proximityValid);
  // Serial.print("haveKey: ");Serial.println(haveKey);
  // Serial.print("touches: ");Serial.println(touches);
  isTouched = touches > 0;
  if (bufferStatus == 1 && isTouched)
  {
    for (uint8_t i = 0; i < touches; i++)
    {
      readBlockData(data, GT911_POINT_1 + i * 8, 7);
      points[i] = readPoint(data);
    }
  }
  writeByteData(GT911_POINT_INFO, 0);
}

TP_Point TAMC_GT911::readPoint(uint8_t *data)
{
  uint16_t temp;
  uint8_t id = data[0];
  uint16_t x = data[1] + (data[2] << 8);
  uint16_t y = data[3] + (data[4] << 8);
  uint16_t size = data[5] + (data[6] << 8);
  switch (rotation)
  {
  case ROTATION_NORMAL:
    x = width - x;
    y = height - y;
    break;
  case ROTATION_LEFT:
    temp = x;
    x = width - y;
    y = temp;
    break;
  case ROTATION_INVERTED:
    x = x;
    y = y;
    break;
  case ROTATION_RIGHT:
    temp = x;
    x = y;
    y = height - temp;
    break;
  default:
    break;
  }
  return TP_Point(id, x, y, size);
}
void TAMC_GT911::writeByteData(uint16_t reg, uint8_t val)
{
  uint8_t data[3] = {
      highByte(reg),
      lowByte(reg),
      val};

  i2c_master_transmit(dev_handle, data, 3, -1);
}
uint8_t TAMC_GT911::readByteData(uint16_t reg)
{
  uint8_t x;

  uint8_t data[2] = {
      highByte(reg),
      lowByte(reg),
  };

  i2c_master_transmit(dev_handle, data, 2, -1);
  i2c_master_receive(dev_handle, &x, 1, -1);

  // Wire.beginTransmission(addr);
  // Wire.write(highByte(reg));
  // Wire.write(lowByte(reg));
  // Wire.endTransmission();
  // Wire.requestFrom(addr, (uint8_t)1);
  // x = Wire.read();
  return x;
}
void TAMC_GT911::writeBlockData(uint16_t reg, uint8_t *val, uint8_t size)
{
  uint8_t data[size + 2];
  data[0] = highByte(reg);
  data[1] = lowByte(reg);
  for (uint8_t i = 0; i < size; i++)
  {
    data[i + 2] = val[i];
  }
  i2c_master_transmit(dev_handle, data, size + 2, -1);
  // Wire.beginTransmission(addr);
  // Wire.write(highByte(reg));
  // Wire.write(lowByte(reg));
  // // Wire.write(val, size);
  // for (uint8_t i = 0; i < size; i++)
  // {
  //   Wire.write(val[i]);
  // }
  // Wire.endTransmission();
}
void TAMC_GT911::readBlockData(uint8_t *buf, uint16_t reg, uint8_t size)
{
  uint8_t data[2] = {
      highByte(reg),
      lowByte(reg),
  };
  i2c_master_transmit(dev_handle, data, 2, -1);
  i2c_master_receive(dev_handle, buf, size, -1);

  // Wire.beginTransmission(addr);
  // Wire.write(highByte(reg));
  // Wire.write(lowByte(reg));
  // Wire.endTransmission();
  // Wire.requestFrom(addr, size);
  // for (uint8_t i = 0; i < size; i++)
  // {
  //   buf[i] = Wire.read();
  // }
}
TP_Point::TP_Point(void)
{
  id = x = y = size = 0;
}
TP_Point::TP_Point(uint8_t _id, uint16_t _x, uint16_t _y, uint16_t _size)
{
  id = _id;
  x = _x;
  y = _y;
  size = _size;
}
bool TP_Point::operator==(TP_Point point)
{
  return ((point.x == x) && (point.y == y) && (point.size == size));
}
bool TP_Point::operator!=(TP_Point point)
{
  return ((point.x != x) || (point.y != y) || (point.size != size));
}
