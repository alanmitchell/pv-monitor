/*
Sketch to collect Photovoltaic panel charging power and batter voltage, along with
temperature values from a string of DS18B20 one-wire sensors and the internal 
temperature on the Blues Wireless Notecard.  Data is sent to the Blues NoteHub.
 */

// Include the libraries we need
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <Notecard.h>

#define INTERVAL 3600              // Seconds between data transmission
#define CARD_TEMP_CALIB 0.0        // deg F calibration to apply to Notecard temp reading
#define CURRENT_CALIB_MULT 0.99    // multiplier to calibrate current sensor
#define ONE_WIRE_BUS 5             // pin used for one-wire bus
#define TEMPERATURE_PRECISION 12   // bits of precision for temperature reads

#define myProductID "us.ahfc.tboyes:pv_monitoring"
Notecard notecard;

// INA219 sensor object for measuring current and voltage at the PV Panel
Adafruit_INA219 ina219;

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// 1-wire address of temperature sensor being processed
DeviceAddress dsAddress;

// Variables tracking average power and voltage
float volt_avg = 0.0;
float power_avg = 0.0;
float power_min = 1e6;
float power_max = 0.0;
unsigned long n = 0;

// Last time data was posted
unsigned long last_time = 0;

void setup(void)
{

  Serial.begin(9600);

  Wire.begin();
  notecard.begin();

  J *req = notecard.newRequest("hub.set");
  JAddStringToObject(req, "product", myProductID);
  JAddStringToObject(req, "mode", "periodic");
  JAddNumberToObject(req, "outbound",240);
  JAddNumberToObject(req, "inbound", 240);
  
  notecard.sendRequest(req);

  ina219.begin();
  // with the 0.00375 ohm shunt, max 130 mV shunt voltage, max current is 34.7 Amps
  ina219.setCalibration_32V_1A();

  // Start up the one-wire library
  sensors.begin();

}

// function to print a device address
void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++) {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

String abbrevAddress(DeviceAddress deviceAddress) {
  
  String result = "";
  
  for (uint8_t i= 6; i < 8; i++) {
    if(deviceAddress[i] < 0x10) {
      result += '0';
    }
    result += String(deviceAddress[i], HEX);
  }
  result.toUpperCase();
  return result;
}

/*
   Main function, calls the temperatures in a loop.
*/
void loop(void)
{

  float shuntvoltage = 0;
  float busvoltage = 0;
  float current_A = 0;
  float power = 0;

  shuntvoltage = ina219.getShuntVoltage_mV();
  busvoltage = ina219.getBusVoltage_V();
  current_A = shuntvoltage / 0.00375 / 1000.0 * CURRENT_CALIB_MULT;
  power = busvoltage * current_A;

  volt_avg += busvoltage;
  power_avg += power;
  n += 1;
  if (power < power_min) power_min = power;
  if (power > power_max) power_max = power;

  if (millis() - last_time >= INTERVAL * 1000) {

    last_time = millis();
    Serial.println(last_time / 1000);
    
    // Calculate average voltage and power.
    volt_avg /= (float)n;
    power_avg /= (float)n;

    Serial.print("Voltage:       "); Serial.print(volt_avg); Serial.println(" V");
    Serial.print("Power Avg      "); Serial.print(power_avg); Serial.println(" W");
    Serial.print("Power Min:     "); Serial.print(power_min); Serial.println(" W");
    Serial.print("Power Max:     "); Serial.print(power_max); Serial.println(" W");
  
    double card_temp = -99;
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.temp"));
    if (rsp != NULL) {
        card_temp = JGetNumber(rsp, "value") * 1.8 + 32.0 + CARD_TEMP_CALIB;     // -2.0 is calibration
        notecard.deleteResponse(rsp);
        Serial.print("Notecard Temp:  "); Serial.print(card_temp); Serial.println(" F");
    }  
  
    J *req = notecard.newRequest("note.add");
    JAddBoolToObject(req, "sync", true);
    J *body = JCreateObject();
    JAddNumberToObject(body, "voltage", volt_avg);
    JAddNumberToObject(body, "power_avg", power_avg);
    JAddNumberToObject(body, "power_min", power_min);
    JAddNumberToObject(body, "power_max", power_max);
    if (card_temp != -99) {
      JAddNumberToObject(body, "card_temp", card_temp);
    }
  
    // find and report all temperature sensors
    // call sensors.requestTemperatures() to issue a global temperature
    // request to all devices on the bus
    sensors.requestTemperatures();
    oneWire.reset_search();
    bool found = true;
    while (found) {
      found = oneWire.search(dsAddress);
      if (found) {
        sensors.setResolution(dsAddress, TEMPERATURE_PRECISION);
        float tempF = sensors.getTempC(dsAddress) * 1.8 + 32.0;
        String base_lbl = "temp_";
        String temp_lbl = base_lbl + abbrevAddress(dsAddress);
        int buf_len = temp_lbl.length() + 1;
        char charBuf[buf_len];
        temp_lbl.toCharArray(charBuf, buf_len);
        JAddNumberToObject(body, charBuf, tempF);
        Serial.print(temp_lbl); Serial.print(": "); Serial.print(tempF); Serial.println(" F");
      }
    }
    
    JAddItemToObject(req, "body", body);
    notecard.sendRequest(req);

    // reset variables tracking power and voltage stats.
    volt_avg = 0.0;
    power_avg = 0.0;
    power_min = 1e6;
    power_max = 0.0;
    n = 0;
  
    Serial.println();
    
  }
  
  delay(1000);
  
}
