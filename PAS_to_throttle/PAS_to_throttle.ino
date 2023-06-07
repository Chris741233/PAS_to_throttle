/******************************************************************
Created with PROGRAMINO IDE for Arduino - 02.06.2023 | 10:21:23
Project     :
Libraries   :
Author      :  Chris74

Description :  e-bike PAS to Throttle

*** Github (code, diagram, infos and more) :  https://github.com/Chris741233/PAS_to_throttle


- Forum Cyclurba : https://cyclurba.fr/forum/742211/arduino-l-assistance-d-un-vae.html?discussionID=31032#msg742211

- Doc PAS ebikes.ca (Signal Types for Basic PAS Sensors) https://ebikes.ca/learn/pedal-assist.html

- Divers : RC Low-pass Filter Design Tool http://sim.okawa-denshi.jp/en/CRtool.php


******************************************************************/


// -------- GPIO Arduino Uno/Nano --------------

const int PAS_PIN = 2;   // interrupt (0)  : input from PAS
const int LED_PIN = 13;  // 13 = LED_BUILTIN (controle)

const int PWM_PIN = 10;  // PWM output pin --> to e-bike controler
// PWM signal 490,20 Hz on D3, D9, D10, (or D11 but not on all Nano)
// PWM signal 976,56 Hz on D5 or D6

const int THR_PIN = 0;   // -- THROTTLE A0 (if needed, not mandatory)



// -------- SETTING CONSTANTS (if const float use decimal !) ------------------------

#define USE_PROPORTIONAL    0   // use proportional assistance ? 1=yes, 0=no (if no, use only On-Off assistance with full PWM)
#define INVERSE_ASSISTANCE  0   // if proportional, inverse assistance ? 1=yes, 0=no (if yes, slow pedaling = more assistance !)
// Si inverse, envois plus d'assistance en pédalage lent qu'en pedalage rapide !

#define USE_THUMB_THROTTLE  0   // thumb throttle instaled ? 1=yes, 0=no (throttle priority on the PAS)

const int  NB_MAGNETS =  6;     // How many magnets on PAS ?  (default 6)

const float V_REF =     4.95;   // Arduino +5V pin reference (=PWM high level) - To test! (default 5.00)
const float V_MIN_THR = 1.10;   // throttle min voltage (default 1.1V --- no push)
const float V_MAX_THR = 3.50;   // throttle max voltage (default 3.5V --- full  push)

// throttle (if instaled) --> ADC value, see debug Serial in loop !
const int   TR_ADC_MIN    = 220;    // throttle min - marge ajoutee dans map()
const int   TR_ADC_MAX    = 856;    // throttle max - marge deduite dans map()
const int TR_ADC_MARGIN   = 15;     // margin throttle before send signal PWM and as a deduction of TR_MAX

const int  RPM_TO_START = 10;   // How many RPM to start assistance ? (with default 10, start is normally fast enough)
// --> more rpm = less ms

const int START_PULSES  = 0;    // Number of pulses (magnet) needed before turning On (0 = fastest)

// if use_proportional, RPM value for maping PWM out --> cf map() in void turnOn()
const int RPM_MIN = 25;         // min rpm  (default 25rpm)
const int RPM_MAX = 65;         // max rpm  (default 65rpm)

// interval timer loop: Check RPM, Turn Off, and debug Serial
const int SCAN_INTERVAL = 250;   // default 250, if assistance cut too fast when stop pedaling, put const to ~500ms or 1000ms



// ********** Calculation (don't modif) *********
// -----------------------------------------
const long MS_TO_START = 60000/NB_MAGNETS/RPM_TO_START;  // result en ms. Expl 10rpm and 6 magnets = 1000ms

// PWM 8bit = 5V/255 = 19mV  precision ...
const int MIN_PWM  = V_MIN_THR/V_REF * 255;  // expl:  1.1V / 5.0V * 255 = PWM 56.1 (int 56)
const int MAX_PWM = V_MAX_THR/V_REF * 255;   // expl:  3.5V / 5.0V * 255 = PWM 178.5 (int 178)

const long COEFF_RPM = 60000 / NB_MAGNETS;               // 60000 / 6 magnets  = 10000  (1 minute=60000ms)

const long MS_SLOW = 60000 / RPM_MIN / NB_MAGNETS;       // for maping proportional value
const long MS_FAST = 60000 / RPM_MAX / NB_MAGNETS;       // for maping proportional value



// -------- GLOBAL VARIABLES ----------------------

unsigned int rpm  = 0;          // RPM pedalling

bool ped_forward = false;       // pedaling forward or backward

bool led_state = false;         // state led controle

bool throt_on = false;          // throttle in use, default false


// -- var Interrupt isr_pas
unsigned long isr_oldtime = 0;

volatile unsigned int period_h = 0;  // volatile obligatoire pour interupt si echange avec autres fonctions
volatile unsigned int period_l = 0;
volatile unsigned int period = 0;

volatile unsigned int pulse = 0;    // PAS pulse count


// -------- MAIN PROG ----------------------

void setup() {
    Serial.begin(115200);
    
    pinMode(LED_PIN, OUTPUT);
    pinMode(PAS_PIN, INPUT_PULLUP);  // PAS Hall, without resistor (input_pullup) !
    
    pinMode(THR_PIN, INPUT);         // thumb throttle, if instaled
    
    digitalWrite(LED_PIN, LOW);      // Led control, low in boot (high if PAS-PWM out)
    
    // -- Interrupt pedaling : appel "isr_pas" sur signal CHANGE (high or low)
    attachInterrupt(digitalPinToInterrupt(PAS_PIN), isr_pas, CHANGE);
    
    // debug const systeme
    Serial.println(MS_TO_START);
    Serial.println(MS_SLOW);
    Serial.println(MS_FAST);
    Serial.println("---------------");
    
    // no delay() here !
    
} //end setup



void loop()
{
    static uint32_t oldtime = millis(); // timer loop, static !
    
    
    // -- If instaled, read ADC thumb throttle --> throttle priority on the PAS !
    #if  USE_THUMB_THROTTLE == 1
        int val = analogRead(THR_PIN);
        
        // re-map val throttle
        int pwm_throttle = map(val, TR_ADC_MIN + TR_ADC_MARGIN, TR_ADC_MAX -TR_ADC_MARGIN, MIN_PWM, MAX_PWM);
        
        if (val > TR_ADC_MIN + TR_ADC_MARGIN) {
            throt_on = true;
            analogWrite(PWM_PIN, pwm_throttle);  // PWM out
        }
        else {
            throt_on = false;
        }
    #endif
    // -- end thumb throttle
    
    
    // -- pedaling forward or backwards ? (verif si pedalage en avant ou en arriere)
    if (period_h >= period_l) ped_forward = true;
    else ped_forward = false;
    
    
    // -- Turn On only if ...
    if (pulse > START_PULSES && period < MS_TO_START) {
        if (ped_forward) turnOn(); // Turn On only if ped. forward (pedalage en avant !)
    }
    
    // -- Timer loop: Check RPM, Turn Off if no pedalling, and debug Serial
    uint32_t check_t = millis() - oldtime;
    
    if (check_t >= SCAN_INTERVAL) {
        
        oldtime = millis();   // update timer loop
        
        rpm = (pulse * COEFF_RPM) / check_t;
        
        pulse = 0; // reset pulses
        
        // Turn off if no pedaling ...
        if (rpm == 0 || period == 0) {
            turnOff();
        }
        
        // -- debug val thumb throttle
        #if  USE_THUMB_THROTTLE == 1
            /*
            Serial.println(val);
            Serial.println(throt_on);
            Serial.println("---------------");
            */
        #endif
        
        
        // -- debug rpm and period info
        /*
        Serial.print("rpm= ");
        Serial.println(rpm);      // rpm pedalling
        Serial.print("tot period= ");
        Serial.println(period);   // period tot, front to front, in ms
        Serial.print("period high= ");
        Serial.println(period_h); // period high, in ms
        Serial.print("period low= ");
        Serial.println(period_l); // period low, in ms
        if (rpm > 0) {
            if (ped_forward) Serial.println("ped. forward, OK !");
            else Serial.println("ped. backward, no assist !");
        }
        Serial.println("---------------");
        */
        
    } // endif
    
    //Use LED for status info
    digitalWrite(LED_PIN, led_state);
    
    // No delay() here !
    
} // end loop



// -------- FUNCTIONS ----------------------


// -- ISR - interrupt PAS
void isr_pas() {
    
    // rester ici le plus concis possible
    uint32_t isr_time = millis();
    
    if (digitalRead(PAS_PIN) == HIGH) {
        period_l = (isr_time - isr_oldtime);  // ms low (inverse)
        
        pulse++;          // increment nb de pulse (pour calcul rpm)
    }
    else {
        period_h = (isr_time - isr_oldtime);  // ms high (inverse)
    }
    
    period = period_h + period_l;   // tot period
    
    isr_oldtime = isr_time ;        // update timer interupt
    
} // endfunc


// -- Turn off output, reset pulse counter and set state variable to false
void turnOff() {
    
    // noInterrupts();
    detachInterrupt(digitalPinToInterrupt(PAS_PIN)); //  desactiver interrupt,  uniquement pin concernée
    
    pulse    = 0;
    period   = 0;
    period_h = 0;
    period_l = 0;
    
    //interrupts();
    attachInterrupt(digitalPinToInterrupt(PAS_PIN), isr_pas, CHANGE);
    
    analogWrite(PWM_PIN, MIN_PWM);  // PWM minimum
    led_state = false;
    
} // endfunc


// -- Turn on output and set state variable to true
void turnOn() {
    
    int pwm_out;
    
    #if USE_PROPORTIONAL==0
        pwm_out = MAX_PWM;
    #endif
    
    #if USE_PROPORTIONAL==1 && INVERSE == 0
        pwm_out = map(period, MS_SLOW, MS_FAST, MIN_PWM, MAX_PWM);
        if (pwm_out > MAX_PWM) pwm_out=MAX_PWM;
    #endif
    
    #if USE_PROPORTIONAL==1 && INVERSE == 1
        pwm_out = map(period, MS_SLOW, MS_FAST, MAX_PWM, MIN_PWM);
        if (pwm_out > MAX_PWM) pwm_out=MAX_PWM;
    #endif
    
    // -- PAS PWM only if no use of thumb throtle
    #if  USE_THUMB_THROTTLE == 1
        if (throt_on==false) analogWrite(PWM_PIN, pwm_out);
    #else
        analogWrite(PWM_PIN, pwm_out);
    #endif
    
    led_state = true;
    
} // endfunc







