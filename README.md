# SpeedPulser
The SpeedPulser is designed to take digital pulses from a speed sensor within the gearbox and translate this into a PWM signal which drives an OEM speedometer.  It’s perfect for situations where a physical speedometer cable cannot be installed or those wishing for a cleaner installation.  It is budget-friendly and therefore requires some final user setup and calibration.

The SpeedPulser also accepts speed signals from Can2Cluster – which can get speed from the ECU, DSG or GPS inputs.

The device takes the signals and converts them into signals to suit analog clusters – like the MK1 & MK2 Golf.  It is fully opensource and the source code can be downloaded and adjusted to suit other brands if required.  Please get in touch if you need help!

It is based on a Lolin C3 ESP32 and the BLDC motor is a TY3816B.

![SpeedPulserCoupler](/Images/BoardOverview.png)

### Supported Models

> MK1/MK2 Golf (120/140/160mph)

> FIAT (160mph)

> Mercedes W123 (confirmed by Forbes / W124 (confirmed via. Discord)

> Smiths 5/8" 

> Volvo M12x1.5

Users are actively encouraged to submit new models & calibrations or feedback if models need adjusting. 

### More Detailed Instructions
The last file on GitHub is the 'Installation Guide' and provides more detail.

### Purchase
If you want to purchase an assembled SpeedPulser, you can do so here: [SpeedPulser - Forbes Automotive](https://forbes-automotive.com/products/speedpulser)

#### Installation
Only three wires are required to operate the SpeedPulser:

| Pin/ | Signal | Notes |
|-----|--------|-------|
| 1 | Vbatt | 12V | Closest to edge of PCB |
| 2 | Ground/MALT | — | Middle of connector |
| 3 | Speed Signal | Square wave type input signal |

For reference, the motor pinout is:
| Pin/ | Signal | Notes |
|-----|--------|-------|
| 1 | Motor Power | Black - 5-9v adjustable |
| 2 | Motor Feedback | Not used |
| 3 | Motor Direction | Green - ground to reverse |
| 4 | Motor Ground | White - motor ground |
| 5 | Motor PWM | 10kHz signal from ESP32 |

#### Pull-up or Pull-down
Different hall sensors from different manufacturers may require a pull-up or pull-down resistor.  There is a 2-way header on the board to select this.  If you find the SpeedPulser is not detecting incoming pulses, move this jumper.  Some hall sensors may have their own internal resistor, so the jumper can be removed entirely.

#### Coupler Installation
The motors will come with the coupler pre-installed and the final drive pin and securing pins separate. The end user should remove the motor and align the drive pin into the coupler and push it home. It will be a  tight fit, but a few light taps will drive it home. A spare coupler is also supplied in the kit. There should be an appreciation that the couplers are 3D printed and therefore may have some alignment variations, this is the chance to lightly heat (~100c max) to encourage/make run true. If the coupling has excessive vibration is may strip out, but a drop of superglue will repair this and run indefinitely. If this is required, ensure that the coupler runs true! Future revisions may look to replace these with a metal alternative, but this would reflect in a higher price. The point of this kit is to remain budget friendly. 

![SpeedPulserCoupler](/Images/speedPulserCoupler.png)

#### Trimming the Drive Shaft
Drive shafts are supplied in excess of required length and should be trimmed to suit each individual cluster.  Typically this is ~level with the motor housing but confirmation that it is not bottomed out is key!

#### Fine Tuning Coupler
Once the motor is built up, it can be slid over the OEM cluster. Fitment of the drive pin should be confirmed and that the motor spins freely. Some fine tuning here will keep noise to a minimum and reduce the risk of premature failure of the coupler. The drive-pin length should also be confirmed and ensure that it does engage with the cluster but does not bottom out and stop the housing seating fully. Take your time here!

#### Software Benefits
The SpeedPulser is now WiFi capable which means that all of the necessary calibration settings are available for changing in WiFi.  It will store these settings in EEPROM.  
> Fully calibrated motor: Duty Percentage vs. Resulting Speed
> Needle Sweep available
> WiFi Enabled

#### WiFi Support
WiFi is now enabled for easier on-the-fly setup.  Needle sweep & calibration can be done without needing to mess with complicated firmware.
Connect to 'SpeedPulser' on WiFi and use IP Address 192.168.1.1 in your browser to adjust settings!  WiFi is available for the first 30s after powering on the unit and will remain active when there is a connection.  This is to manage the heat generated.  Version 2 PCBs have a larger voltage step-down unit which can manage more heat.

### Initial Calibration
Initial calibration will be carried out using the WiFi feature.

> Connect to SpeedPulser on WiFi
> Go to 192.168.1.1
> Select 'Advanced Controls'
> Select the calibration you wish to use
> Choose 'Test Speed'
> Select 'Go to Speed' and choose the highest value on your cluster
> Adjust the blue potentiometer to the maximum valve
> Use 'Go to Speed' to confirm random speeds and ensure that it matches

![SpeedPulserCoupler](/Images/WiFiConn3.jpg)
![SpeedPulserCoupler](/Images/WiFiConn2.jpg)

### Fine Tuning Calibration
If the speedometer is consistently out by a factor - the 'Offset' option can be used to adjust the speedometer.  The 'Positive Offset' switch will ADD the chosen offset.  If the switch remains off, it will SUBTRACT the chosen offset.

## Calibration of Other Speedometers

> Connect to SpeedPulser on WiFi
> Go to 192.168.1.1
> Select 'Calibration'
> Choose 'Previous' - this will roll over the duty cycle to 385 - this is the maximum speed avaiable for your calibration
> Adjust the blue potentiometer to the maximum speed you wish to calibrate to (likely the top speed of the speedometer)
> Choose 'Next' - this will reset the duty cycle back to 0 
> Choose 'Next' until the motor begins to turn.  This is the lowest speed the motor can achieve
> Begin filling the Excel sheet with each speed at each duty cycle
> Copy these values into a Word document and carry out a 'Find/Replace'.  Find "^p" and replace with ",".  This is a quick way to change all the new lines with commas
> Copy the array from Word into the suitable '_motorCal' array and upload the code
> Submit to GitHub for others to use!

![SpeedPulserCoupler](/Images/WiFiConn4.jpg)

## Understanding the Excel
There is a plot of motor speed vs. PWM in an Excel.  Should it be required, the module can be put into 'testing mode' and each PWM variance worked through in 5 second intervals.  It takes around 10 minutes to carry out a full sweep of the available speeds, but will give good accuracy.  

## Disclaimer
It should be noted that this will drive a speedometer, but it should always be assumed that it is inaccurate and therefore no responsibilty on speed related incidents are at fault of Forbes Automotive.  
