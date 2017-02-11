# rpiburn
Raspberry power supply testing, while monitoring system for anomalies.

rpiburn is a tool for heavy load Raspberry Pi hardware testing. It will make the board to consume as much current as possible, but aborts if the processor becomes overheated, a brownout (under voltage) condition occur or something else bad happens. Note that **board** consumption is of interest! This is not necessarily the same as maxing out the processor (although that's used too).

### Measured values
| Pi model | Current (mA)                    |
| -------- | ------------------------------- |
| 1B+      | 320                             |
| 2B       | 650                             |
| 3B       | 1300 with Neon and 830 without  |

### Disclaimer
:warning: Use at your own risk! This program overheats a RPi 3 in just 10 seconds. :smiley:

### Copyright
rpiburn is part of [Nard SDK](http://www.arbetsmyra.dyndns.org/nard/ "Nard SDK")   
&copy; 2014-2017 Ronny Nilsson, 2013 Siarhei Siamashka

