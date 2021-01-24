#include "mbed.h"

/*
Driver class for the Sharp GP2Y1010AU0F dust sensor with the 4-PIN board adapter.
Measure the dust denisty and calculates the dust density, with two different formulars.
The average of 5 measurements is stored in the "averageSharp" and "averageCN" variable.
*/
class GP2Y1010AU0F
{
public:
    /* 
    lightLed: Pinname to visualise measurement duration via led light on board
    aout: Pinname to read data from sensor, Voltage is set to 5V in constructor
    iled: Pinname to control the Infrared led to measure the dust density via the sensor
    */
    GP2Y1010AU0F(PinName lightLed, PinName aout, PinName iled): _iled(iled), _aout(aout,5.0),  _lightLed(lightLed)
    {
    };
    int samplingTime = 280;//280 microseconds
    int deltaTime = 20;//40 us to give total pulse width of 0.32ms, we reduce this to 20 to give the read about 10-15 us time
    int sleepTime = 9680;//LED off for 9680 us 
    float dustDensityCN = 0,dustDensitySharp = 0, voMeasured=0, voCalc=0; //Measured and calaculated parameters
    int measureCount = 0; // Counts up for every 5 measurements
    float averageSharp = 0, averageCN = 0; //Average of 5 reads for Sharp calculation and Chris Nafis

    void measure()
    {
        for(int i = 0; i < 5; i++) {
            _switchLightLed();
            _turnILEDOn(); // power on the ILED.
            wait_us(samplingTime);
            voMeasured = _sampleAOUT(); // Converts and read the analog input value - Needs time to read
            wait_us(deltaTime); // 280 + duration of sampling + 20 (40 to 320, but read also takes time, so this is reduced to 20)
            _turnILEDOff(); // turn the ILED off.
            wait_us(sleepTime);
            
            //recover voltage
            voCalc = voMeasured * 5 * 11; // *5 for voltage and *11 because of the specification in the usermanual
            //linear eqaution taken from http://www.howmuchsnow.com/arduino/airquality/
            // Chris Nafis (c) 2012
            dustDensityCN = 0.17 * voCalc - 0.1; // Most commonly used formular
            //Thomas Kirchner, "original" Sharp equation taken from https://os.mbed.com/users/kirchnet/code/DustSensorDisplay5110/file/99fdd85b4929/main.cpp/
            dustDensitySharp = 0.5/2.8 * (float(voCalc) - 0.7); // This is an alternate formular, but Chris Nafi's formular is more widely used
            /* if the measured values are smaller than 0, we set them to 0*/
            if (dustDensityCN < 0){
                dustDensityCN = 0;
            }
            if(dustDensitySharp < 0){
                dustDensitySharp = 0;
            }
            averageSharp += dustDensitySharp;
            averageCN += dustDensityCN;
        }
        /* Calculate Average from the last 5 measurements */

        averageSharp = (averageSharp /5);
        averageCN = (averageCN /5);

        measureCount = measureCount + 1; // Track the measurements done, could potentialy to an overflow after long uses
    }

    /* Print all information gathered from the last measurement */
    void printLastMeasurement()
    {
        printf("****************************************************\n");
        printf(" - %d. Measurement\n", measureCount);
        printf(" - Measurment value: %1.4f\n", voMeasured);
        printf(" - Voltage calculated: %1.4f\n", voCalc);
        printf(" - Sharp's Dust Density [mg/m3]: %f\n", dustDensitySharp);
        printf(" - C. Nafis' Dust Density [pp.01cf](x10^4): %f\n", dustDensityCN);
        printf("****************************************************\n\n");
    }

    /* Print the average dust density from the last 5 measurements (one call of measure()) */
    void printAverageDensity()
    {
        printf("****************************************************\n");
        printf(" - Sharp's Dust Density [mg/m3]: %f\n", averageSharp);
        printf(" - C. Nafis' Dust Density [pp.01cf](x10^4): %f\n", averageCN);
        printf("****************************************************\n\n");
    }



private:
    AnalogIn _aout; //Data read from the dust sensor
    DigitalOut _iled; // Infrared led control light sent to the sensor
    DigitalOut _lightLed; // Optical led on the Microcontroller

    /* Send 3.3V to sensor */
    void _turnILEDOn()
    {
        _iled = 1;
    };

    /* Send ~0V to sensor */
    void _turnILEDOff()
    {
        _iled = 0;
    };

    /* Switch the light on/off */
    void _switchLightLed()
    {
        _lightLed = !_lightLed;
    };

    /* Read sample data from the sensor */
    float _sampleAOUT()
    {
        return _aout.read();
    };
};