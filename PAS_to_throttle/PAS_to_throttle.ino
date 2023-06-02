/******************************************************************
Created with PROGRAMINO IDE for Arduino - 02.06.2023 | 10:21:23
Project     :
Libraries   :
Author      :  Chris74

Description :  PAS to throttle v1

Github (code, infos and more) : 

Forum Cyclurba :


******************************************************************/


// -------- GPIO Arduino Uno/Nano --------------

const int PAS_PIN = 2;   // interrupt (0)  : input from PAS
const int LED_PIN = 13;  // 13 = LED_BUILTIN (controle)

const int PWM_PIN = 10;  // PWM output pin
// PWM signal 490,20 Hz on D3, D9, D10, (or D11 but not on all Nano)
// PWM signal 976,56 Hz on D5 or D6



// -------- SETTING CONSTANTS (if var float use decimal !) ------------------------

#define USE_PRPORTIONAL     1   // use proportional assistance ? 1=yes, 0=no (if no, use only On-Off assistance with full PWM)
#define INVERSE_ASSISTANCE  0   // if proportional, inverse assistance ? 1=yes, 0=no (if yes, less RPM = more assistance !)

const float V_REF = 4.80;       // Arduino +5V pin reference (=PWM level high) - To test! (default 5.00)
const float V_MIN_THR = 1.10;   // throttle min voltage, default 1.1V --- no push
const float V_MAX_THR = 3.60;   // throttle max voltage, default 3.6V --- full  push

const int NB_MAGNETS =  8;      // How many magnets on PAS ?  (default 6)

const int RPM_TO_START = 10;    // How many RPM to start assistance ? (with default 10, start is normally fast enough)
const int START_PULSES  = 0;    // Number of pulses (of magnet) needed before turning On (0 = fastest)

// for maping proportional value, cf map() in void turnOn()
const int RPM_MIN = 20;         // min rpm  (default 20rpm)
const int RPM_MAX = 60;         // max rpm  (default 60rpm)



// ********** Calculation (don't modif) *********
// -----------------------------------------
const int MIN_PWM  = V_MIN_THR/V_REF * 255;  // expl:  1.1V / 5.0V * 255 = PWM 56.1 (int 56)  
const int MAX_PWM = V_MAX_THR/V_REF * 255;  // expl:  3.4V / 5.0V * 255 = PWM 173.4 (int 173)
// PWM 8bit = 5V/255 = 19mV  precision ...

const long COEFF_RPM = 60000 / NB_MAGNETS;  // 60000 / 6 magnets  = 10000  (1 minute=60000ms)

const long MS_TO_START = 60000 / RPM_TO_START / NB_MAGNETS;  // result en ms. Expl 10rpm and 8 magnets = 750ms

const long MS_SLOW = 60000 / RPM_MIN / NB_MAGNETS; // for maping proportional value
const long MS_FAST = 60000 / RPM_MAX / NB_MAGNETS; // for maping proportional value


// timer loop
const int SERIAL_INTERVAL = 2000;     // interval affichage Serial et infos, en ms, 1000 ou 2000
// 1000 = affichage rapide, 2000 calcul RPM plus stable et précis


// -------- GLOBAL VARIABLES ----------------------

unsigned long previousMillis = 0;  // for timer loop (Serial info) 

unsigned int rpm      = 0;         // RPM pedalling 

bool led_state = false;            // state led controle


// -- var Interrupt
unsigned long time_isr = 0;
unsigned long lastTime = 0;

volatile unsigned int period = 0;
volatile unsigned int pulse = 0;   // revolution count : Volatile obligatoire pour interupt si échange avec loop 


// -------- MAIN PROG ----------------------

void setup() {
    Serial.begin(115200);
    
    pinMode(LED_PIN, OUTPUT);
    pinMode(PAS_PIN, INPUT_PULLUP);  // PAS Hall, sans ajout de résistance !
    
    digitalWrite(LED_PIN, LOW);      // Led  Low au boot
    
    // -- Interrupt pedaling : appel "isr_pas" sur signal CHANGE  
    attachInterrupt(digitalPinToInterrupt(PAS_PIN), isr_pas, RISING);  
    
    // debug
    Serial.println(MS_TO_START);
    Serial.println(MS_SLOW);
    Serial.println(MS_FAST);
    
    // no delay() here !
    
} //end setup



void loop()
{
    unsigned long currentMillis = millis(); // init timer 
    
    // Turn Off (check timeout, turn off if too long without signal)
    if ((currentMillis > time_isr + 400) && ((currentMillis - time_isr) > 100)) {
        turnOff();
    }
    
    // Turn On only if ...
    if (pulse > START_PULSES && period < MS_TO_START) {
        turnOn();
    }
    
    
    // timer loop  info RPM and debug Serial
    long check_t = currentMillis - previousMillis;
    
    if (check_t >= SERIAL_INTERVAL) {
        
        rpm   = (pulse * COEFF_RPM) / check_t;
        pulse = 0; // reset pulses
        
        // debug
        Serial.println(rpm);    // rpm pedalling
        Serial.println(period); // period front to front, in ms
        
        //Serial.println(currentMillis);
        //Serial.println(time_isr);
        //Serial.println(currentMillis - time_isr);
        
        previousMillis = currentMillis; // reset timer loop
        
    } // endif
    
    //Use LED for status info
    digitalWrite(LED_PIN, led_state);
    
    
    // No delay() here !
    
} // end loop



// -------- FUNCTIONS ----------------------


// -- ISR - interrupt PAS
void isr_pas() {
    
    // rester ici le plus concis possible !
    time_isr = millis();                 // timer isr
    
    period = (time_isr - lastTime);      // = ms entre 2 fronts
    
    if (period < MS_TO_START)  pulse++;  // increment pulse only if  ...
    
    lastTime = time_isr ;                // reset timer
    
} // endfunc


// -- Turn off output, reset pulse counter and set state variable to false
void turnOff() {
    
    // noInterrupts();
    detachInterrupt(digitalPinToInterrupt(PAS_PIN)); //  desactiver interrupt,  uniquement pin concernée
    
    analogWrite(PWM_PIN, MIN_PWM);  
    pulse  = 0;
    period = 0;
    
    led_state = false;
    
    //interrupts();
    attachInterrupt(digitalPinToInterrupt(PAS_PIN), isr_pas, RISING); // reactiver interruption
    
} // endfunc


// -- Turn on output and set state variable to true
void turnOn() {
    
    
    #if USE_PRPORTIONAL==0
        int pwm_out = MAX_PWM;
    #endif  
    
    #if USE_PRPORTIONAL==1 && INVERSE == 0
        int pwm_out = map(period, MS_SLOW, MS_FAST, MIN_PWM, MAX_PWM);
        if (pwm_out > MAX_PWM) pwm_out=MAX_PWM; 
    #endif 
    
    #if USE_PRPORTIONAL==1 && INVERSE == 1
        int pwm_out = map(period, MS_SLOW, MS_FAST, MAX_PWM, MIN_PWM);
        if (pwm_out > MAX_PWM) pwm_out=MAX_PWM; 
    #endif 
    
    analogWrite(PWM_PIN, pwm_out);
    //analogWrite(PWM_PIN, MAX_PWM);
    led_state = true;
    
} // endfunc







