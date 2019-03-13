//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2019-02-26 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------
// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER

// https://github.com/bogde/HX711
// https://learn.sparkfun.com/tutorials/load-cell-amplifier-hx711-breakout-hookup-guide/all


#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>

#include <Register.h>
#include <MultiChannelDevice.h>

#include <HX711.h>

// Arduino Pro mini 8 Mhz
// Arduino pin for the config button
#define CONFIG_BUTTON_PIN  8
#define LED_PIN            4
#define HX711_SCK_PIN      A4
#define HX711_DOUT_PIN     A5

#define HX711_CALIBRATION 873.58f
#define HX711_OFFSET     -43556L
#define AVERAGE_READ_COUNT 20

// number of available peers per channel
#define PEERS_PER_CHANNEL  6

// all library classes are placed in the namespace 'as'
using namespace as;

//Korrekturfaktor der Clock-Ungenauigkeit, wenn keine RTC verwendet wird
#define SYSCLOCK_FACTOR    0.88

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
  {0xF3, 0x4B, 0x00},          // Device ID
  "JPWEIGHT00",                // Device Serial
  {0xF3, 0x4B},                // Device Model
  0x10,                        // Firmware Version
  0x53,                        // Device Type
  {0x01, 0x01}                 // Info Bytes
};

/**
   Configure the used hardware
*/
typedef AskSin<StatusLed<LED_PIN>, BatterySensorUni<A1,5>, Radio<AvrSPI<10, 11, 12, 13>, 2>> BaseHal;
class Hal : public BaseHal {
  public:
    void init (const HMID& id) {
      BaseHal::init(id);
      // measure battery every 1h
      battery.init(seconds2ticks(60UL * 60), sysclock);
      battery.low(26);
      battery.critical(24);
    }

    bool runready () {
      return sysclock.runready() || BaseHal::runready();
    }
} hal;

DEFREGISTER(UReg0, MASTERID_REGS, DREG_LOWBATLIMIT, 0x20, 0x21)
class UList0 : public RegList0<UReg0> {
  public:
    UList0 (uint16_t addr) : RegList0<UReg0>(addr) {}

    bool Sendeintervall (uint16_t value) const {
      return this->writeRegister(0x20, (value >> 8) & 0xff) && this->writeRegister(0x21, value & 0xff);
    }
    uint16_t Sendeintervall () const {
      return (this->readRegister(0x20, 0) << 8) + this->readRegister(0x21, 0);
    }

    void defaults () {
      clear();
      lowBatLimit(26);
      Sendeintervall(180);
    }
};

DEFREGISTER(UReg1, 0x01, 0x02, 0x03, 0x04,0x05)
class UList1 : public RegList1<UReg1> {
  public:
    UList1 (uint16_t addr) : RegList1<UReg1>(addr) {}

    bool Tara (int32_t value) const {
      return
          this->writeRegister(0x01, (value >> 24) & 0xff) &&
          this->writeRegister(0x02, (value >> 16) & 0xff) &&
          this->writeRegister(0x03, (value >> 8) & 0xff) &&
          this->writeRegister(0x04, (value) & 0xff)
          ;
    }

    int32_t Tara () const {
      return
          ((int32_t)(this->readRegister(0x01, 0)) << 24) +
          ((int32_t)(this->readRegister(0x02, 0)) << 16) +
          ((int32_t)(this->readRegister(0x03, 0)) << 8) +
          ((int32_t)(this->readRegister(0x04, 0)))
          ;
    }

    bool TaraOnRestart (uint8_t value) const {
      return this->writeRegister(0x05, 0x01, 0, value & 0xff);
    }
    bool TaraOnRestart () const {
      return this->readRegister(0x05, 0x01, 0, false);
    }

    void defaults () {
      clear();
      Tara(0);
      TaraOnRestart(false);
    }
};

class MeasureEventMsg : public Message {
  public:
    void init(uint8_t msgcnt, uint8_t channel, int32_t weight, bool batlow, uint8_t volt) {
      Message::init(0x10, msgcnt, 0x53, (msgcnt % 20 == 1) ? BIDI : BCAST, batlow ? 0x80 : 0x00, channel & 0xff );
      pload[0] = (weight >> 24) & 0x7f;
      pload[1] = (weight >> 16) & 0xff;
      pload[2] = (weight >> 8) & 0xff;
      pload[3] = (weight) & 0xff;
      pload[4] = (volt)   & 0xff;
    }
};

class MeasureChannel : public Channel<Hal, UList1, EmptyList, List4, PEERS_PER_CHANNEL, UList0>, public Alarm {
    MeasureEventMsg msg;
    int32_t         weight;
    uint8_t         last_flags = 0xff;
    HX711           hx711;

  public:
    MeasureChannel () : Channel(), Alarm(0), weight(0) {}
    virtual ~MeasureChannel () {}

    void measure() {
      hx711.power_up();
      weight = (int32_t)(hx711.get_units(AVERAGE_READ_COUNT) * 100.0);
      weight += this->getList1().Tara();
      DPRINT(F("weight: ")); DDECLN(weight);
      hx711.power_down();
    }

    virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
      measure();

      if (last_flags != flags()) {
        if (number() == 1) this->changed(true);
        last_flags = flags();
      }
      tick = delay();
      msg.init(device().nextcount(), number(), weight, device().battery().low(), device().battery().current());
      device().sendPeerEvent(msg, *this);
      sysclock.add(*this);
    }

    uint32_t delay () {
      return seconds2ticks((max(10, device().getList0().Sendeintervall()) * SYSCLOCK_FACTOR));
    }

    virtual void configChanged() {
      DPRINT(F("*Tara: ")); DDECLN(this->getList1().Tara());
    }

    void setup(Device<Hal, UList0>* dev, uint8_t number, uint16_t addr) {
      Channel::setup(dev, number, addr);
      DPRINT(F("Init HX711"));
      hx711.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
      for (uint8_t chk = 0; chk < 10; chk++) {
        DPRINT(F("."));
        if (hx711.is_ready()) {
          DPRINTLN(F(" OK"));
          hx711.set_scale(HX711_CALIBRATION * 1.0); //(to ensure that it is a float)
          bool ToR=this->getList1().TaraOnRestart();
          DPRINT("Tara on Restart = ");DDECLN(ToR);
          if (ToR == true)
            hx711.tare();
          else
            hx711.set_offset(HX711_OFFSET);
          hx711.power_down();
          break;
        }
        if (chk == 9) {
          DPRINTLN(F(" ERR"));
          while(1) {}
        }
        _delay_ms(100);
      }
      sysclock.add(*this);
    }

    uint8_t status () const {
      return 0;
    }

    uint8_t flags () const {
      uint8_t flags = this->device().battery().low() ? 0x80 : 0x00;
      return flags;
    }
};

class UType : public MultiChannelDevice<Hal, MeasureChannel, 1, UList0> {
  public:
    typedef MultiChannelDevice<Hal, MeasureChannel, 1, UList0> TSDevice;
    UType(const DeviceInfo& info, uint16_t addr) : TSDevice(info, addr) {}
    virtual ~UType () {}

    virtual void configChanged () {
      TSDevice::configChanged();
      DPRINT(F("*LOW BAT Limit : "));
      DDECLN(this->getList0().lowBatLimit());
      this->battery().low(this->getList0().lowBatLimit());
      DPRINT(F("*Sendeintervall: ")); DDECLN(this->getList0().Sendeintervall());
    }
};

UType sdev(devinfo, 0x20);
ConfigButton<UType> cfgBtn(sdev);

void setup () {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  sdev.init(hal);
  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);
  sdev.initDone();
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if ( worked == false && poll == false ) {
    if (hal.battery.critical()) {
      hal.activity.sleepForever(hal);
    }
    hal.activity.savePower<Sleep<>>(hal);
  }
}
