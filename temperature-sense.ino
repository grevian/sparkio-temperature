// This #include statement was automatically added by the Spark IDE.
#include "Adafruit_DHT/Adafruit_DHT.h"

// Put the core into Semi-Automatic mode, this keeps it from automatically connecting to the wifi
// SYSTEM_MODE(SEMI_AUTOMATIC);

/* HARDWARE Layout
    D0 - Transmit button
    D1 - Sysmode button (Jump to a reprogrammable/web API accessible mode)
    D4 - DHT I/O Pin
*/
#define TX_BUTTON 0         // The button to force transmit mode and publish the buffer
#define SYSMODE_BUTTON 1    // The button to engage system mode for reprogramming and debugging
#define DHTPIN 4            // Temperature and humidity sensor will connect to digital pin 4
#define LED 7               // This one is the built-in tiny one to the right of the USB jack
DHT dht(DHTPIN, DHT22);     // Create a DHT 22 object on the assigned pin

// Bitmask flags for the current operating mode
#define TX_MODE 1
#define OFFLINE_MODE 2
#define REALTIME_MODE 4
#define SYSTEM_MODE 8

// Our data packet for sensor samples
struct dataPacket {
    int timestamp;
    int temperature;
    int humidity;
};

/* EEPROM Layout
    0 = CONFIG              // Currently just the operating mode
    1 = DATA_WRITE_POINTER  // This doesn't really have to be a full byte, we could use an unsigned short
    2-99 = DATA
*/
int EEPROM_CONFIG_ADDRESS = 0;
int EEPROM_DATA_WRITE_POINTER = 1;
int INIT_EEPROM_DATA_ADDRESS = 2;
int MAX_EEPROM_DATA_ADDRESS = 99;
int DATAPACKET_SIZE = sizeof(dataPacket);

int SAMPLE_DELAY = 60*1; // 1 minute during debugging

// TODO Add the ability to call this from a spark.Function call
void reset_eeprom() {
    EEPROM.write(EEPROM_CONFIG_ADDRESS, SYSTEM_MODE);
    EEPROM.write(EEPROM_DATA_WRITE_POINTER, INIT_EEPROM_DATA_ADDRESS);
}

void set_system_mode() {
    Spark.publish("log", "Button Pushed, Changing to System Mode");
    set_mode("system");
}

void set_transmit_mode() {
    Spark.publish("log", "Button Pushed, Changing to Transmit Mode");
    set_mode("transmit");
}

int set_mode(String mode) {
    if (mode == "system") {
        Spark.publish("log", "Changing Mode to System Mode");
        EEPROM.write(EEPROM_CONFIG_ADDRESS, SYSTEM_MODE);
        return 1;
    } else if (mode == "offline") {
        Spark.publish("log", "Changing Mode to Offline");
        EEPROM.write(EEPROM_CONFIG_ADDRESS, OFFLINE_MODE);
        return 1;
    } else if (mode == "transmit") {
        Spark.publish("log", "Changing Mode to Transmit");
        EEPROM.write(EEPROM_CONFIG_ADDRESS, TX_MODE);
        return 1;
    }
    return -1;
}

void setup() {
    RGB.control(true);
    RGB.brightness(128);
    Time.zone(-5);
    Spark.publish("log", "Setup Run");
    pinMode(LED, OUTPUT);
    
    Spark.function("mode", set_mode);
    pinMode(SYSMODE_BUTTON, INPUT);
    pinMode(TX_BUTTON, INPUT);
    
    // Allow us to change modes from the web API, or at least jump to system mode from a local button
    attachInterrupt(SYSMODE_BUTTON, set_system_mode, RISING);
    attachInterrupt(TX_BUTTON, set_transmit_mode, RISING);
}

void loop() {
    int config_bytes = EEPROM.read(EEPROM_CONFIG_ADDRESS);
    int current_eeprom_addr = EEPROM.read(EEPROM_DATA_WRITE_POINTER);
    
    // TODO We could store more in configuration bytes if we wanted, we're only using 4 bits right now
    int mode = config_bytes;
    
    if ( (mode & SYSTEM_MODE) == SYSTEM_MODE) {
    //if (true) {
        // Just in case we want to be reprogrammed or call a web function
        RGB.color(128, 128, 0);
        if (Spark.connected() == false) {
            Spark.publish("log", "System Mode Connecting to Spark Cloud");
            Spark.connect();
            Spark.publish("log", "System Mode Connected to Spark Cloud");
        }
        Spark.process(); // Not necessary in semi-automatic mode, but not bad either, and useful if we ever go to manual mode
        delay(750);
        RGB.color(128, 128, 128);
        delay(250);
        return; // In system mode, we won't do any of the other stuff, just wait around for cloud function calls or reprogramming
    }
    
    // In offline mode we want to store the data packet into EEPROM
    if ( (mode & OFFLINE_MODE) == OFFLINE_MODE) {
        RGB.color(0, 0, 255);
        // Ensure the wifi is off
        // WiFi.off();
        digitalWrite(LED, HIGH);
        Spark.publish("log", "Offline Mode Sampling beginning");
        
        // Read our temp, humidity and time values
        dht.begin(); // Fire up the DHT 22
        delay(2000); // Wait 2 seconds, as recommended by the DHT datasheet

        double temperature = dht.getTempCelcius(); //Get temperature from the DHT 22
        double humidity = dht.getHumidity(); // Get humidity from the DHT 22
        int current_time = Time.now(); // Get the time since the unix epoch in seconds
    
        // Round the values from doubles to long integers and pack them into our datastructure
        // TODO May want to multiply by 10x first to keep 3 numbers of precision? we should have plenty to spare
        dataPacket dp;
        dp.temperature = round(temperature);
        dp.humidity = round(humidity);
        dp.timestamp = current_time;
        
        // Get a pointer to the datapacket bytes
        char *dpp =  (char*)&dp;
    
        // Write the struct out into eeprom
        for (int i = 0; i < DATAPACKET_SIZE; i++) {
            EEPROM.write(current_eeprom_addr+i, dpp[i]);
        }
        // Increment then store the address pointer
        current_eeprom_addr += DATAPACKET_SIZE;
        EEPROM.write(EEPROM_DATA_WRITE_POINTER, current_eeprom_addr);
        
        // If we can't fit another packet after this one, the buffer is full and we should transmit
        if (current_eeprom_addr + DATAPACKET_SIZE >= MAX_EEPROM_DATA_ADDRESS) {
            set_mode("transmit");
        } else {
            // sleep for 5~ minutes, or until the button interrupt is hit
            // TODO This may require a firmware update on the core? Plus the behaviour is different on photons (RAM isn't cleared!, no need for eeprom dancing)
            // Spark.sleep(TX_BUTTON, FALLING, 10);
            Spark.process(); // Process any events available before heading to sleep
            Spark.publish("log", "Offline Mode heading into deep sleep");
            delay(500);
            // Spark.sleep(SLEEP_MODE_DEEP, 10);
            Spark.sleep(10);
        }
    } else if ( (mode & TX_MODE) == TX_MODE) {
        Spark.publish("log", "Transmit Mode Beginning");
        digitalWrite(LED, HIGH);
        RGB.color(0, 255, 255);
        // In transmit mode, connect to the spark cloud then publish our sensor data, then switch back to offline mode
        if (Spark.connected() == false) {
            Spark.connect();
            Spark.publish("log", "Transmit Mode connected to spark cloud");
            delay(100);
            return; // Continue looping while we wait for the Spark to connect
        } else {
            Spark.publish("log", "Transmit Mode publishing some data packets");
            char* buffer[DATAPACKET_SIZE];
            for (int i = INIT_EEPROM_DATA_ADDRESS; i+DATAPACKET_SIZE <= MAX_EEPROM_DATA_ADDRESS; i += DATAPACKET_SIZE) {
                // Read the bytes of our datastructure into our byte buffer
                for (int j = 0; j < DATAPACKET_SIZE; j++) {
                    buffer[j] = (char*)EEPROM.read(i+j);
                }
                dataPacket *dpp = (dataPacket*)buffer;
                
                // Message Format: "42133423:26.3:25.4"
                // Timestamp:temperature:humidity
                Spark.publish("dataPacket", String(dpp->timestamp) + ":" + String(dpp->temperature) + ":" + String(dpp->humidity));
                digitalWrite(LED, LOW);
                delay(1000);        // There's a 1/s throttle on spark events (With a little room for bursts, but not bursts this size)
                digitalWrite(LED, HIGH);
                Spark.process();    // This *IS* required here since the 30~ events could cause a timeout from the spark cloud if we don't
                                    // call process in our userspace code
            }
            
            // Reset our mode and data pointer to switch back to offline mode and start collecting data again
            Spark.publish("log", "Transmit Mode completed, wiping eeprom and heading back to offline mode");
            EEPROM.write(EEPROM_DATA_WRITE_POINTER, INIT_EEPROM_DATA_ADDRESS);
            set_mode("offline");
        }
    } else {
        Spark.publish("log", "Unknown Mode: " + String(mode));
        delay(1000);
    }
}

