#include "hw/imu.h"
#include "hw/pins.h"
#include <Wire.h>
#include <SensorQMI8658.hpp>

static SensorQMI8658 s_qmi;

bool hwImuInit() {
  if (!s_qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL)) {
    Serial.println("hwImu: QMI8658 begin failed");
    return false;
  }
  Serial.printf("hwImu: WHO_AM_I=0x%02X chipID=0x%02X\n",
                s_qmi.whoAmI(), s_qmi.getChipID());
  // Reset to clean default state — without this on the 2.16, the chip
  // returned saturated X/Y axis readings (stuck at +2g/+2g).
  s_qmi.reset();
  s_qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                            SensorQMI8658::ACC_ODR_125Hz);
  s_qmi.enableAccelerometer();
  return true;
}

void hwImuAccel(float* ax, float* ay, float* az) {
  IMUdata d;
  s_qmi.getAccelerometer(d.x, d.y, d.z);
  *ax = d.x;
  *ay = d.y;
  // +Z points through the back panel on this board: screen-up reads
  // d.z ≈ +1g, screen-down reads ≈ −1g. Keep the sign — isFaceDown()
  // expects az < −0.7 for face-DOWN. (Earlier code flipped to −d.z based
  // on a one-off measurement; on the 2.16 boards we have, that inverts
  // the detector and naps the screen face-UP on a desk.)
  *az = d.z;
}
