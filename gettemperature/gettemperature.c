#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include <Temboo.h>
#include <TembooSession.h>
#include <TembooNetworkClient.h>
#include "TembooAccount.h"

#define HIGH 1
#define LOW 0
#define INPUT 1
#define OUTPUT 0

// SocketConnection is a struct containing data needed
// for communicating with the network interface
SocketConnection theSocket;

// There should only be one TembooSession per device. It represents
// the connection to Temboo
TembooSession theSession;

// Limit the number of times the Choreo is to be run. This avoids
// inadvertently using up your monthly Choreo limit
int currentRun = 0;
const int MAX_RUNS = 10;

// Defines a time in seconds for how long the Choreo
// has to complete before timing
const int CHOREO_TIMEOUT = 300;

// How long to wait between Choreo execution requests in seconds.
// Note that some services will block access if you
// hit them too frequently
const uint32_t choreoInterval = 30;
uint32_t lastChoreoRunTime = 0;
int outputPin = 13;

bool digitalPinMode(int pin, int dir){
  FILE * fd;
  char fName[128];
  // Exporting the pin to be used
  if ((fd = fopen("/sys/class/gpio/export", "w")) == NULL) {
    printf("Error: unable to export pin\n");
    return false;
  }
  fprintf(fd, "%d\n", pin);
  fclose(fd);

  // Setting direction of the pin
  sprintf(fName, "/sys/class/gpio/gpio%d/direction", pin);
  if ((fd = fopen(fName, "w")) == NULL) {
    printf("Error: can't open pin direction\n");
    return false;
  }
  if (dir == OUTPUT) {
    fprintf(fd, "out\n");
  } else {
    fprintf(fd, "in\n");
  }
  fclose(fd);

  return true;
}

int analogRead(int pin) {
  FILE * fd;
  char fName[64];
  char val[8];

  // open value file
  sprintf(fName, "/sys/devices/12d10000.adc/iio:device0/in_voltage%d_raw", pin);
  if ((fd = fopen(fName, "r")) == NULL) {
    printf("Error: can't open analog voltage value\n");
    return 0;
  }
  fgets(val, 8, fd);
  fclose(fd);

  return atoi(val);
}

int digitalRead(int pin) {
  FILE * fd;
  char fName[128];
  char val[2];

  // Open pin value file
  sprintf(fName, "/sys/class/gpio/gpio%d/value", pin);
  if ((fd = fopen(fName, "r")) == NULL) {
    printf("Error: can't open pin value\n");
    return false;
  }
  fgets(val, 2, fd);
  fclose(fd);

  return atoi(val);
}

bool digitalWrite(int pin, int val) {
  FILE * fd;
  char fName[128];

  // Open pin value file
  sprintf(fName, "/sys/class/gpio/gpio%d/value", pin);
  if ((fd = fopen(fName, "w")) == NULL) {
    printf("Error: can't open pin value\n");
    return false;
  }
  if (val == HIGH) {
    fprintf(fd, "1\n");
  } else {
    fprintf(fd, "0\n");
  }
  fclose(fd);

  return true;
}

TembooError setup() {
  // We have to initialize the TembooSession struct exactly once.
  TembooError returnCode = TEMBOO_SUCCESS;

#ifndef USE_SSL
  returnCode = initTembooSession(
            &theSession, 
            TEMBOO_ACCOUNT, 
            TEMBOO_APP_KEY_NAME, 
            TEMBOO_APP_KEY, 
            &theSocket);
#else    
  printf("Enabling TLS...\n");
  returnCode = initTembooSessionSSL(
            &theSession, 
            TEMBOO_ACCOUNT, 
            TEMBOO_APP_KEY_NAME, 
            TEMBOO_APP_KEY, 
            &theSocket,
            "/opt/iothub/artik/temboo/temboo_artik_library/lib/temboo.pem",
            NULL);
#endif

  if (!digitalPinMode(outputPin, OUTPUT)) {
    return -1;
  }

  return returnCode;
}

// Call a Temboo Choreo
void runGetTemperature(TembooSession* session) {

  printf("\nRunning GetTemperature\n");

  // Initialize Choreo data structure
  TembooChoreo choreo;
  const char choreoName[] = "/Library/Yahoo/Weather/GetTemperature";
  initChoreo(&choreo, choreoName);

  // Set profile
  const char profileName[] = "newone2";
  setChoreoProfile(&choreo, profileName);

  // Set Choreo inputs
  ChoreoInput AddressIn;
  AddressIn.name = "Address";
  AddressIn.value = "Bakersfield, CA 93304";
  addChoreoInput(&choreo, &AddressIn);

  int returnCode = runChoreo(&choreo, session, CHOREO_TIMEOUT);
  if (returnCode != 0) {
    printf("runChoreo failed.  Error: %d\n", returnCode);
  }

  // Print the response received from Temboo
  while (tembooClientAvailable(session->connectionData)) {
    char name[64];
    char value[64];
    memset(name, 0, sizeof(name));
    memset(value, 0, sizeof(value));

    choreoResultReadStringUntil(session->connectionData, name, sizeof(name), '\x1F');

    if (0 == strcmp(name, "Temperature")) {
      if (choreoResultReadStringUntil(session->connectionData, value, sizeof(value), '\x1E') == -1) {
        printf("Error: char array is not large enough to store the string\n");
      } else {
        if (atoi(value) > 55) {
          digitalWrite(outputPin, LOW);
        }
      }
    }
    else {
      choreoResultFind(session->connectionData, "\x1E");
    }
  }

  // When we're done, close the connection
  tembooClientStop(session->connectionData);
}

int main(void) {
  if (setup() != TEMBOO_SUCCESS) {
    return EXIT_FAILURE;
  }

  uint32_t now = time(NULL);
  lastChoreoRunTime = now - choreoInterval;

  while(currentRun < MAX_RUNS){
    now = time(NULL);
    if ((now - lastChoreoRunTime >= choreoInterval)){
      lastChoreoRunTime = now;
      currentRun++;
      runGetTemperature(&theSession);
    }

    usleep(1000);
  }

#ifdef USE_SSL
  // Free the SSL context and and set Temboo connections to no TLS
  endTembooSessionSSL(&theSession);
#endif

  return EXIT_SUCCESS;
}