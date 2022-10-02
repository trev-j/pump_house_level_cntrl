# pump_house_level_cntrl
Arduino Uno project for tank level gap control

This project uses the native Arduino Uno I/O pins to read the water level in a tank via input from an ultrasonic sensor. 
Based on the configured low limit of the tank water level, the configured output pin will be set HIGH to actuate a relay
controlling a 120 VAC feed pump. A built in delay watchdog function will monitor for tank level rise within x minutes of
the pump output being set. In the event tha the level does not rise by the configured deadband in this time the pump output
will be set LOW to prevent potential damage to the pump if run dry, or flooding caused by any leaks in the feed piping. 

Additionally, discrete outputs are set within the code to control stack lights for visual indicaiton of the system status
from outside the pumphouse. Different light colors and sequences are set depending on the status or active error codes.

Additional code added for interfacing with Arduino Yun Linux environment to allow display and control of system parameters
via web interface.
