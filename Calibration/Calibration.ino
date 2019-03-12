#include <HX711.h>
#define HX711_SCK_PIN      A4
#define HX711_DOUT_PIN     A5
HX711 scale;

void setup() {
  Serial.begin(57600);
  Serial.println("       HB-UNI-Sen-Weight Calibration Sketch");
  Serial.println("1.) Remove all weight from scale, then press return");
  scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  while (Serial.available() == 0) { }
  while (Serial.available() > 0) {
    String t = Serial.readString();
  }
  scale.set_scale();
  scale.set_offset(0);
  Serial.print("Offset = ");
  long a = scale.read_average(10);
  Serial.println(a);
  scale.set_offset(a);
  scale.set_offset(a);
  Serial.println("\n2.) Enter known load value, put it on the scale, then press return");
  while (Serial.available() == 0) { }
  float m = Serial.parseFloat();
  Serial.print("Input = ");
  Serial.println(m, 2);
  Serial.print("Scale = ");
  float sc = scale.get_units(10) / m;
  Serial.println(sc, 2);
  Serial.println("\nInsert these lines into the HB-UNI-Sen-WEIGHT.ino sketch:");
  Serial.print("#define HX711_CALIBRATION ");Serial.print(sc,2);Serial.println("f");
  Serial.print("#define HX711_OFFSET      ");Serial.print(a);Serial.println("L");
}

void loop() {

}

