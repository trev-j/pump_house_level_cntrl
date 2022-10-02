#include <Wire.h> //library for basic I/O
#include <Process.h>
#include <BridgeServer.h>
#include <BridgeClient.h>

// Listen to the default port 5555, the Yún webserver
// will forward there all the HTTP requests you send
BridgeServer server;

// Output definitions
const int RelayOut_Pin = 12; //Pump on / off relay output pin on digital 6
const int Heater_Pin = 10; //Arduino enclosure heater PWM output pin on digital 9
const int White_LED = 4; //Arduino heartbeat indicator (Blue LED in field)
const int Green_LED = 5; //Pumping indicator
const int Orange_LED = 6; //Wait period indicator
const int Red_LED = 7; //Error indicator

//Analog A2 reserved for level input
//Analog A0 reserved for enclosure temp input

// For Level Logic
int waterLevel = 0;
int waterLevel2 = 0;
int lowLevel = 30;
int highLevel = 80;
int hyst = 2;
int dly = 1000; //General delay between reads
int errorDly = 1000; //Delay for blinking Red LED in error loop
int lvlFlag = 0; //Flag to go into pump mode
int errorType = 0; //1 == fail during wait, 2 == fail during pump

// For Timer
unsigned long startTime = 0;
int pumpLagTime = 7; // Time in minutes expected between when pump is started and level will start rising
int pumpMaxOnTime = 20; // Maximum time pump would be expected to be on to prevent potenial overflow in the event of a bad level sensor

// For Temp Monitor
int TempFlag = 0;
int sampleTemps[10];
int x; //Temp array index
int sum = 0; //Temp array sum
int tempAvg = 5; //Temp array average value
int tempCount = 0; //Temp array count (1000 counts = 1 sample recorded to array)
int z = 0; //For tempMonitor function sampleTemps index value
String reset_control = "0";
String pump_control = "0";

// For SQL queries to SQLITE3 database
String query_prefix = "SELECT value FROM softanalog WHERE name = ";
String pump_command_query = query_prefix + "'pump_control'";
String reset_command_query = query_prefix + "'reset_control'";

//=====================================================================================================================================================================================================================================================

void setup(void) {
  
  //Start serial comms
  Serial.begin(9600);
  Bridge.begin();  // Initialize Bridge
  
  //Set heater, relay, and LED digital pins to output mode
  pinMode(RelayOut_Pin, OUTPUT);
  pinMode(Heater_Pin, OUTPUT);
  pinMode(White_LED, OUTPUT);
  pinMode(Green_LED, OUTPUT);
  pinMode(Orange_LED, OUTPUT);
  pinMode(Red_LED, OUTPUT);
  
  //Initialize temp sample array with values at limit
  for (x=0; x<10; x++) {
     sampleTemps[x]= 5; 
  }

  // Listen for incoming connection only from localhost
  // (no one from the external network could connect)
  server.listenOnLocalhost();
  server.begin();
}
//=====================================================================================================================================================================================================================================================
void loop(void) {
  writeToDB("softanalog", "low_level", lowLevel);
  writeToDB("softanalog", "high_level", highLevel); 
   
  digitalWrite(RelayOut_Pin, LOW); //Stop pump
  digitalWrite(White_LED, HIGH);  //Turn on main white LED

  Serial.println("======Idle======");
  writeToDB("softanalog", "sequence_state", 0);
  tempMonitor();
  waterLevel = getLevel();
  Serial.println("==================");  
  
  // Sum temperatures stored in array  
  for (x=0; x<10; x++) {
    sum += sampleTemps[x];
    Serial.println(sampleTemps[x]); 
  }
  
  // Calculate temperature average
  tempAvg = sum/10;
  Serial.println(tempAvg);
  
  // Adjust low level based on average temperature
  // In winter keep low level higher to keep more thermal mass in tank to reduce risk of freezing
  if (tempAvg >= 5 && lowLevel == 30) {
    lowLevel -= 10;
  } else {
   if (tempAvg < 5 && lowLevel == 20) {
     lowLevel += 10;
   }
  }
  
  Serial.println(lowLevel); 

  delay(dly);                        // Delay in between reads for stability
  
  if ((waterLevel <= lowLevel) && lvlFlag ==0) { // Low level detected, enter active logic
    
    lvlFlag = 1;
    int startingLevel = getLevel();
    tempMonitor();
    
    digitalWrite(RelayOut_Pin, HIGH);  //Start pump
    writeToDB("digital", "D12", 1); // Write pump state to DB
    digitalWrite(Orange_LED, HIGH);  //Turn on Orange LED
    
    starttime = millis();
    int i = 0;
    
    while (water_level < startingLevel + hyst) {  // Delay 7 minutes for water flow to start and level to rise
      
      Serial.println("======Check======"); 
      writeToDB("softanalog", "sequence_state", 1);
      i = timer(pumpLagTime, startTime);
      
      if (i ==1) {
        
        errorType = 1; //Set error type to 1 if timeout before level increase
        break;
      }
      
      waterLevel = getLevel();
      delay(dly);  
    }
    
    digitalWrite(Orange_LED, LOW);  //Turn off Orange LED
    waterLevel2 = getLevel(); //Record level after 5 min pump delay
    
    if (lvlFlag == 1 && errorType == 0) {
      startTime = millis();
      i = 0;
      while (waterLevel2 <= highLevel) { //Future: Includes total timeout time of 18 minutes to stop pump regardless of read level.
        
        i = timer(pumpMaxOnTime, startTime);
        
        if (i == 1) {
          errorType = 2; //Set error type to 2 if timeout before tank is filled.
          break;
        }
        
        digitalWrite(Green_LED, HIGH);  //Turn on Green LED
        
        Serial.println("======Pumping======");
        
        writeToDB("softanalog", "sequence_state", 2);
        tempMonitor();
        waterLevel2 = getLevel();
        
        Serial.println("==================");

        delay(dly);                // delay in between reads for stability

      }  
      
      tempMonitor();
      
      digitalWrite(RelayOut_Pin, LOW); //Stop pump
      writeToDB("digital", "D12", 0); // Write pump state to DB
      digitalWrite(Green_LED, LOW);  //Turn off Green LED
      
      //Serial.println("======Done======"); // Leave commented out: For testing via potentiometer

      delay(dly);                   // delay in between reads for stability
    }
    
    else {
      
      while((waterLevel2 < lowLevel + hyst) && lvlFlag == 1 && errorType > 0 && reset_control == "0") {
        
        digitalWrite(RelayOut_Pin, LOW); //Stop pump
        writeToDB("digital", "D12", 0); // Write pump state to DB
        digitalWrite(Red_LED, HIGH);  //Turn on Red LED
        
        Serial.println("======Error======"); // Leave commented out: For testing via potentiometer
        
        writeToDB("softanalog", "sequence_state", -1);
        tempMonitor();
        waterLevel2 = getLevel();
        
        Serial.println("==================");
        
        switch(errorType) {
          case 1:
            errorDly = 1000;
          case 2:
            errorDly = 250;
        }

        delay(1000);                // delay in between reads for stability
        digitalWrite(Red_LED, LOW); //Turn off Red LED
        delay(1000);                //delay for blinking effect 
        
        reset_control = runSqlQuery(reset_command_query);
      }
      runSqlQuery("UPDATE softanalog SET value = '0' WHERE name = 'reset_control'");
      writeToDB("softanalog", "sequence_state", 0);
      digitalWrite(Red_LED, LOW);  //Turn off Red LED when exiting fail state loop
      lvlFlag = 0;
    }
   lvlFlag = 0; 
  } 
}
//=====================================================================================================================================================================================================================================================
void tempMonitor() {
  
  int tempRead = analogRead(A0);               //Read value on analog pin 0
  float tempVolt = float(tempRead) / 204.8;    //Convert read to voltage
  float tempCelsius = (100 * tempVolt) - 50;   //Convert voltage to temp in Celsius
  
  Serial.print("Temp C: ");
  Serial.println(tempCelsius);
  writeToDB("analog", "A0", tempCelsius); 
  
  if (tempCelsius <= 2 && TempFlag == 0) {
    
    analogWrite(Heater_Pin, 100);
    TempFlag = 1;
  }
  
  if (tempCelsius >= 5 && TempFlag == 1) {
    
    digitalWrite(Heater_Pin, LOW);
    TempFlag = 0;
  }
  
  if (tempCount >= 1000) {
    
    sampleTemps[z] = tempCelsius;
    tempCount = 0;
    z++;
    
    if (z > 9) {
      z = 0;
    }
  }
  
  tempCount ++;

  BridgeClient client = server.accept();

  // There is a new client?
  if (client) {
    
    // Process request
    process(client);

    // Close connection and free resources.
    client.stop();
  }
}

void process(BridgeClient client) {
  
  // read the command
  String command = client.readStringUntil('/');
  Serial.print(command);
  
  // is "digital" command?
  if (command == "digital") {
    //digitalCommand(client);
  }

  // is "analog" command?
  if (command == "analog") {
    //analogCommand(client);
  }

  // is "mode" command?
  if (command == "mode") {
    //modeCommand(client);
  }
}

float getLevel() {
  
  int levelRead = analogRead(A2);               //Read value on analog pin 2
  float levelVolt = float(levelRead) / 204.8;    //Convert read to voltage
  float levelCM = levelVolt/0.0049;   //Convert voltage to level in centimeters
  float levelPct = 100 - (100*((levelCM - 38.1) / (177.8 - 38.1))); // Inverts 0-100% over 38.1cm to 177.8cm range (TBD) formula for pct = 100* (value - min / max - min)
  
  //int levelPct = (analogRead(A2)/12.03529); // Leave commented out: For testing via potentiometer
  Serial.print("Level %: ");
  Serial.println(levelPct); // Leave commented out: For testing via potentiometer
  
  writeToDB("analog", "A2", levelPct); 
  
  return levelPct;
}

void writeToDB(String type, String signal, float value) {
  
  //digitalWrite(13, HIGH);
  Process p;              
  p.begin("/mnt/sda1/www/Arduino/sensor.php");
  //p.runShellCommand("php-cli /mnt/sda1/www/Arduino/sensor.php 2>&1");
  p.addParameter(type);
  p.addParameter(signal);      
  p.addParameter(String(value));
  p.run();
}

String runSqlQuery(String query) {
  
  int    iNumber    = 0;
  String sName      = "";
  float value = 0;
  String response = "";

  // Start a shell process to run a command in the OS.
  Process p;
  String sCommand = "/usr/bin/sqlite3 -csv /DB/test1.db " + query;
  p.runShellCommand(sCommand);
  Serial.println   (sCommand);

  // do nothing until the process finishes, so you get the whole output:
  while (p.running());

  // Read command output. runShellCommand()
  while (p.available()>0) {
    
    char c = p.read();

    response = String(response + c);
    Serial.print(c);   
  }
  
  Serial.flush();
  Serial.println("Done");
  
  return response;
}

int timer(int time, float start) { //Converts user input of minutes into milliseconds for arduino delay function
  
  int flag = 0;
  float now = millis();
  float currenttime = (now - start)/60000;
  
  if(currenttime >= time) {
    flag = 1;
  }
  
  Serial.print("Timer Minutes: ");
  Serial.println(currenttime);
  
  return flag;
}



