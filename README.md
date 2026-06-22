# Patient_Vital_Monitoring_and_Emergency_Alert_System
A simulated embedded patient monitoring system that tracks heart rate, body temperature, and blood pressure. The system processes sensor data using sliding window averaging, analyzes vitals through a state-machine based decision algorithm, and generates Normal, Warning, or Emergency alerts. Built for ARM Cortex-M/Keil simulation environment.

 What it Does

This project continuously monitors a patient’s vital signs —
Heart Rate, Body Temperature, and Blood Pressure —
and automatically detects any abnormal or critical condition.

If any vital goes out of range, it gives a Warning,
and if the condition becomes dangerous, it shows an Emergency alert.

How It Works (Simple Steps)
1. Sensor Simulation / Input
The program generates or takes readings for heart rate, temperature, and blood pressure.
It can work in automatic simulation mode or with manual test inputs.
2. Data Smoothing
The readings are not used directly.
They are averaged over a few samples to remove sudden spikes or noise — this makes the readings more stable.
3. Decision Logic (State Machine)
The averaged values are checked against preset safe and critical ranges.
If slightly abnormal → Warning state
If far beyond limits → Emergency state
If values return to normal → Normal state
The system waits for a few consecutive abnormal readings before changing the state — this avoids false alarms.
4. Display and Alerts
The current readings and state are printed on the console.
Whenever the state changes, an appropriate message is shown, such as
“ALARM: WARNING – vitals outside normal” or
“ALARM: EMERGENCY – critical vitals.”
5. Test Capture System
For testing, the program can record a section of the output automatically to check system behavior during a specific time.

