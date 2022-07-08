// This #include statement was automatically added by the Particle IDE.
#include <Adafruit_BMP085.h>

// This #include statement was automatically added by the Spark IDE.
#include <Adafruit_DHT.h>

/* HARDWARE Layout
    D4 - DHT22 
    
    D1 - SCL (BMP180)
    D0 - SDA (BMP180)
    
    TX - MH-Z16 C02 Sensor
    RX - MH-Z16 C02 Sensor
    
*/
#define DHTPIN 4            // Temperature and humidity sensor will connect to digital pin 4
#define LED 7               // This one is the built-in tiny one to the right of the USB jack
DHT dht(DHTPIN, DHT22);     // Create a DHT 22 object on the assigned pin
Adafruit_BMP085 bmp;

// 5 minutes between readings
#define publishDelay 300000
// #define publishDelay 10000

// C02 sensor suggests 3 minutes to warm up, before taking readings
#define C02_STARTUP_DELAY 180000L
// #define C02_STARTUP_DELAY 150

// See the MH-z16 datasheet
const uint8_t C02_MEASURE_COMMAND[9] =  { 0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79 };
const char* SENSOR_NAME = "hindu_doctor";
const char* SENSOR_SECRET = "343138333038";

unsigned int lastPublish = 0;
unsigned int C02Resets = 0;

void setup() {
    Time.zone(-5);
    
    Serial.begin(9600);
    Serial.printlnf("Sensor %s coming online...", SENSOR_NAME);
    
    Spark.publish("log", String::format("Setup Run, %s coming online", SENSOR_NAME));
    pinMode(LED, OUTPUT);
    digitalWrite(LED, HIGH);

    // Open a serial channel to the MH-Z16 C02 Sensor
    Serial1.begin(9600);
    
    dht.begin(); // Fire up the DHT 22
    delay(2000); // Wait 2 seconds, as recommended by the DHT datasheet
    
    if (!bmp.begin()) {
        Spark.publish("log", "BMP Failed to initialize!");
        Serial.println("BMP Failed to initialize!");
    }
    digitalWrite(LED, LOW);
}

void loop() {
    unsigned long now = millis();
    if ((now - lastPublish) < publishDelay) {
        // Not time to publish yet
        Serial.printlnf("Delay Loop, Waiting %d more milliseconds before publishing", publishDelay - (now - lastPublish));
        delay(50);
        return;
    }
    
    // Signal that we're recording data
    digitalWrite(LED, HIGH);

    // Read our temp and humidity
    // Spark.publish("log", "Reading DHT Temperature");
    double temperature = dht.getTempCelcius(); //Get temperature from the DHT 22

    // Spark.publish("log", "Reading DHT Humidity");
    double humidity = dht.getHumidity(); // Get humidity from the DHT 22

    // Spark.publish("log", "Reading BMP180 Pressure");
    int pressure = bmp.readPressure(); // Get Barometric Pressure from the BMP180
    
    int PPM = -1;
    if (now > C02_STARTUP_DELAY) {
        sendReadCommand();
        delay(1500);
        PPM = readResponse();
        if (PPM > 15000) {
            // Definitely erroneous, probably the first reading so just reset it to the sentinal and wait for the next reading
            Spark.publish("log", "Incorrect seeming C02 Value, retrying poll");
            Serial.println("Incorrect seeming C02 Value, retrying poll");
            PPM = -1;
        }
        // Queue up the next reading
        // sendReadCommand();
        /*
        if (PPM == -1) {
            Spark.publish("log", "Communication error with C02 Sensor, retrying poll");
            Serial.println("Communication error with C02 Sensor, retrying poll");
            resetComm();
            digitalWrite(LED, LOW);
            return;
        }*/
        
    } else {
        Serial.printlnf("Not reading from C02 Sensor, Still warming up for %d more milliseconds", C02_STARTUP_DELAY - now);
    }
    
    if (temperature < -70.0) {
        Spark.publish("log", "Incorrect seeming Temperature, retrying poll");
        Serial.println("Incorrect seeming Temperature, retrying poll");
        digitalWrite(LED, LOW);
        return; // Very likely the sensor being wrong, poll it again
    }
    
    
    if ((humidity < 0.05) || (humidity > 100)) {
        Spark.publish("log", "Incorrect seeming Humidity, retrying poll");
        Serial.println("Incorrect seeming Humidity, retrying poll");
        digitalWrite(LED, LOW);
        return; // Very likely the sensor being wrong, poll it again
    }
    
    if (PPM > 0) {
        Serial.printlnf("C02 Value: %d", PPM);
        Spark.publish("reading", String::format("{\"SENSOR\": \"%s\", \"SECRET\": \"%s\", \"TYPE\": \"carbon_dioxide_amount\", \"VALUE\": \"%d\"}", SENSOR_NAME, SENSOR_SECRET, PPM), 60, PRIVATE);
    }
    
    Serial.printlnf("Temperature Value: %f", temperature);
    Spark.publish("reading", String::format("{\"SENSOR\": \"%s\", \"SECRET\": \"%s\", \"TYPE\": \"temperature\", \"VALUE\": \"%f\"}", SENSOR_NAME, SENSOR_SECRET, temperature), 60, PRIVATE);
    
    Serial.printlnf("Humidity Value: %f", humidity);
    Spark.publish("reading", String::format("{\"SENSOR\": \"%s\", \"SECRET\": \"%s\", \"TYPE\": \"humidity\", \"VALUE\": \"%f\"}", SENSOR_NAME, SENSOR_SECRET, humidity), 60, PRIVATE);
    
    Serial.printlnf("Atmospheric Pressure Value: %f", pressure);
    Spark.publish("reading", String::format("{\"SENSOR\": \"%s\", \"SECRET\": \"%s\", \"TYPE\": \"barometric_pressure\", \"VALUE\": \"%d\"}", SENSOR_NAME, SENSOR_SECRET, pressure), 60, PRIVATE);
    

    digitalWrite(LED, LOW);
    
    lastPublish = now;
}

void sendReadCommand() {
    for (int i = 0; i <= 9; i++) {
        Serial1.write(C02_MEASURE_COMMAND[i]);
    }
}

int readResponse() {
    char response[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    
    for (int i = 0; i <= 8; i++) {
        response[i] = Serial1.read();
    }
    
    int checksum = calcChecksum(response);
    if (checksum < 0) {
        Serial.println("Checksum Calculation failed!");
        return -1;
    }
    return calculatePPM(response);
}

char calcChecksum(char* cmd) {
    // This isn't an MH-Z16 command/response
    if (cmd[0] != 0xff) {
        return -1;
    }
    
    // Calculate the checksum as per the datasheet
    char checksum = 0;
    for (int i = 1; i < 8; i++) {
        checksum += cmd[i];
    }
    checksum = 0xFF - checksum;
    checksum += 1;
    return checksum;
}

int calculatePPM(char* cmd) {
    const int HIGH_LEVEL = 2;
    const int LOW_LEVEL = 3;
    
    return (cmd[HIGH_LEVEL]*256)+cmd[LOW_LEVEL];
}

void resetComm() {
    delay(500);
    Serial1.flush();
    while (Serial1.available()) {
        Serial1.read();
    }
}

