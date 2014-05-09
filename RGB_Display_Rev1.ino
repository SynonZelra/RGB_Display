// Base Code from kdarrah, https://www.youtube.com/user/kdarrah1234

#include <SPI.h>

byte ch=0, chbit=0, spibit=0, spibyte=0;  // variables used by tlc sub routine
int SINData;                              //variable used to shift data to the TLC
// 12-bit resolution for grayscale data, 6-bit resolution for dot correction data

const int totalCols = 48;  // Total channels 
const int totalRows = 16;  // Total rows
const int byteCount = 72;  // Bytes needed for 12-bit data of one row

int displayRow = 0;        // row currently being displayed through tlc
int rowDelay = 0;          // counter for row switching


const byte greenMax = 11110000;    // max green output value
const byte blueMax = 11111111;     // max blue output value
const byte redMax = 01111111;      // max red output value

float brightness = 0.5;            // brightness ratio

// 8-bit resolution allows for each channel to be addressed by 1 byte
const int pwmRes = 256;            // 8 bit PWM resolution
byte newRowBytes[totalRows][48] = {0};       // New data to write to rows

// 3D array for sending out data to TLC
// Updating/Display Array || Rows || 12-bit channel data
// 2 array, one that is currently being displayed and the other is being updated. At the end of the cycle
// the indexes are switched. Considered pointer here instead but it was too slow
byte transferBytes[2][totalRows][byteCount] = {0};
int displayIndex = 0;
int updateIndex = 1;

boolean registers[totalRows];  // array holding the shift register output data for initiliazation 

byte DCvalue[totalCols];      //0-63, 6 bit DOT Correction Bytes

//*******************************************************************************************
//*******************************************************************************************
void setup(){//   MAIN SETUP   MAIN SETUP   MAIN SETUP   MAIN SETUP   MAIN SETUP

  pinMode(6, OUTPUT);//XLAT
  pinMode(9, OUTPUT);//OC2B GSCLK Timer2 (oscillating the pin at 8MHz). OC2B is pin 9
  pinMode(8, OUTPUT);//VPRG
  pinMode(51, OUTPUT);//MOSI DATA, SIN. Must be pin 51
  pinMode(52, OUTPUT);//SPI Clock, SCLK. Must be pin 52

  pinMode(30,OUTPUT);  // strobePin, PC7
  pinMode(31,OUTPUT);  // dataPin, PC6
  pinMode(32,OUTPUT);  // shiftClock, PC5
  pinMode(33,OUTPUT);  // outEnPin, PC4

  //Set up the SPI
  SPI.setBitOrder(MSBFIRST);//Most Significant Bit First
  SPI.setDataMode(SPI_MODE0);// Mode 0 Rising edge of data, keep clock low when idle

  // Clock data in by a multiple of the clock for input data by SPI
  SPI.setClockDivider(SPI_CLOCK_DIV4);//Run the data in at 16MHz/4 - 4MHz

  for(int j=0; j<totalRows; j++){
    for(i=0; i<byteCount; i++)//clear out Gray Scale Data
      transferBytes[0][j][i]=0; 
      transferBytes[1][j][i]=0; 
  }

  for(i=0; i<totalCols; i++){//set Dot Correction data to max (63 decimal for 6 bit)
      DCvalue[i]=63;  
  }

  Serial.begin(115200);//debugging

  //set up DOT Correction
  DotCorrection();// sub routine helps 

  noInterrupts();// set up the counters, so don't go into interrupts

    // Timer2 for grayscale clock. Timer1 to get end of grayscale cycle

  // Using Timer 2 to oscillate the grayscale pin 
  // Toggle OC2B on Compare Match
  // Clear Timer on Compare (CTC) Match mode where OCR2A is the TOP value to reach 
  // Cleared to zero when TOP is reached
  TCCR2A=B00010010;//Timer 2 set to Compare Mode Toggling pin 5 @ 8MHz, Arduino Digital 3
  // TIMER 2 IS GSCLCK
  //Timer 2 prescaler set to 1, 16/1=16 MHz, but toggles pin 5 every other cycle, 8MHz
  TCCR2B=B00000001;

  TCCR1A=B00000000;//Timer 1 doesn't toggle anything, used for counting
  //Timer 1 prescaler set to Fclk/256 
  //Why? We need to count 4096 pulses out of Timer 2 - pin 5
  //8 MHz = 1 pulse every 125ns - - 4096 pulses would need 512us
  //Timer 1 runs at 16MHz/256=62.5kHz, we need a match at every 512us
  //Basically, I can get an interrupt to get called every 512us, so...
  // I need to run Timer 2 @ 8MHz for 512us to get 4096 pulses
  // I can't count those pulses directy (too fast) , so
  // I'll count using Timer 1, which makes a count every 16us
  // The counter starts at 0, so we'll set it to 31 to get an interrupt after 512us
  TCCR1B=B00001100;//Mode=CTC with OSCR1A = TOP and 256 as the prescaler

  // Interrupt. When you get the compare match to A, you want it to go to the interrupt A(?)
  // Mask set up, will call ISR (Inerrupt Service Routine) for Compare match on A
  TIMSK1=B00000010;
  //These are the match values for the counters
  // 0 here means it will match on one cycle of the clock/prescaler

  OCR1A= 31;//to get our 512us Interrupt

  interrupts();// kick off the timers!

  tlcRow(0);  // sends zeros to tlc for setup 

  setupRegisters();

  pinMode(7, OUTPUT);//BLANK  We set this pin up here, so it remains in a high impedance
  // state throughout the setup, otherwise the LEDs go crazy!  even if you write this HIGH

}// END OF SETUP END OF SETUP END OF SETUP END OF SETUP END OF SETUP END OF SETUP END OF SETUP
//******************************************************************************************

//*******************************************************************************************
// INTERRUPTS   INTERRUPTS   INTERRUPTS   INTERRUPTS   INTERRUPTS   INTERRUPTS   INTERRUPTS  
ISR(TIMER1_OVF_vect){
}// Over Limit Flag Interrupt  you need this even if you don't use it

ISR(TIMER1_COMPB_vect){ 
}// Compare B - Not Used

ISR(TIMER1_COMPA_vect){ // Interrupt to count 4096 Pulses on GSLCK
  // Switches row output every other interrupt
  if (rowDelay == 1){
    resetRowOutput();  // prevents bleeding
    nextRow();
    rowDelay = 0;
  }
  else{
    displayOutputs();
    rowDelay++;
  }


}//INTERRUPTS END  INTERRUPTS END  INTERRUPTS END  INTERRUPTS END  INTERRUPTS END  INTERRUPTS END  
//*******************************************************************************************
void loop(){//   MAIN LOOP   MAIN LOOP   MAIN LOOP   MAIN LOOP   MAIN LOOP   MAIN LOOP
  cycleAllColours();
}//      loop close    loop close      loop close      loop close

// ----- PATTERNS ----- \\

// Sets a colour to column A in each row and shifts it over
void shiftTest(){
  static boolean setFlag = true;
  static int currentCol = 0;
  if (setFlag){
    newRowBytes[0][0] = redMax;//black
    newRowBytes[0][1] = greenMax;
    newRowBytes[0][2] = blueMax;
    
    newRowBytes[1][0] = redMax;//red
    newRowBytes[1][1] = 0;
    newRowBytes[1][2] = 0;
    
    newRowBytes[2][0] = 0;//green
    newRowBytes[2][1] = greenMax;
    newRowBytes[2][2] = 0;
    
    newRowBytes[3][0] = 0;//blue
    newRowBytes[3][1] = 0;
    newRowBytes[3][2] = blueMax;
    
    newRowBytes[4][0] = redMax;//white
    newRowBytes[4][1] = greenMax;
    newRowBytes[4][2] = blueMax;
    
    newRowBytes[5][0] = redMax;//purple
    newRowBytes[5][1] = 0;
    newRowBytes[5][2] = blueMax;
    
    newRowBytes[6][0] = 0;//sky blue
    newRowBytes[6][1] = greenMax;
    newRowBytes[6][2] = blueMax;
    
    newRowBytes[7][0] = redMax;//lime green
    newRowBytes[7][1] = greenMax;
    newRowBytes[7][2] = 0;
    
    newRowBytes[8][0] = 0;//teal
    newRowBytes[8][1] = greenMax;
    newRowBytes[8][2] = blueMax*0.1;
    
    newRowBytes[9][0] = redMax;//orange
    newRowBytes[9][1] = greenMax*0.1;
    newRowBytes[9][2] = 0;
    
    newRowBytes[10][0] = 0;//black
    newRowBytes[10][1] = 0;
    newRowBytes[10][2] = 0;
    
    newRowBytes[11][0] = redMax;//yellow?
    newRowBytes[11][1] = greenMax*0.25;
    newRowBytes[11][2] = 0;
    
    newRowBytes[12][0] = redMax;
    newRowBytes[12][1] = 0;
    newRowBytes[12][2] = 0;
    
    newRowBytes[13][0] = 0;
    newRowBytes[13][1] = greenMax;
    newRowBytes[13][2] = 0;
    
    newRowBytes[14][0] = 0;
    newRowBytes[14][1] = 0;
    newRowBytes[14][2] = blueMax;
    
    newRowBytes[15][0] = redMax;
    newRowBytes[15][1] = greenMax;
    newRowBytes[15][2] = blueMax;
    delay(2000);
    setFlag = false;
  }
  else{
    for (int i=0; i<totalRows; i++){
      shiftRight(i);
      newRowBytes[i][0*currentCol] = 0;
      newRowBytes[i][0*currentCol+1] = 0;
      newRowBytes[i][0*currentCol+2] = 0;
      currentCol++;
      delay(1);
    }
  }
  // Updates slightly faster if update through hardcode instead of for loop
  tlcRow(0);
  tlcRow(1);
  tlcRow(2);
  tlcRow(3);
  tlcRow(4);
  tlcRow(5);
  tlcRow(6);
  tlcRow(7);
  tlcRow(8);
  tlcRow(9);
  tlcRow(10);
  tlcRow(11);
  tlcRow(12);
  tlcRow(13);
  tlcRow(14);
  tlcRow(15);
  updateOutputs();
}

void cycleAllColours(){
  int redVal = 0;
  int greenVal = 0;
  int blueVal = 0;
  int changeRate = 20;

  // FADE UP 

  while (blueVal < blueMax){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    blueVal += changeRate;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  blueVal = blueMax;


  delay(500); 
  blueVal = blueMax;

  while (greenVal < greenMax){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    greenVal += changeRate;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  greenVal = greenMax;

  while (blueVal > -1){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    blueVal -= changeRate;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  blueVal = 0;

  while (redVal < redMax){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    redVal += changeRate/2;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  redVal = redMax;

  while (greenVal > -1){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    greenVal -= changeRate;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  greenVal = 0; 

  while (blueVal < blueMax){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    blueVal += changeRate;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  blueVal = blueMax;

  greenVal = greenMax;
  
  delay(5000);

  // Fade Down
  while (greenVal > -1){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    greenVal -= changeRate;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  greenVal = 0;

  while (blueVal > -1){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    blueVal -= changeRate;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  blueVal = 0;

  while (greenVal < greenMax){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    greenVal += changeRate;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  greenVal = greenMax;

  while (redVal > -1){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    redVal -= changeRate/2;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  redVal = 0;

  while (blueVal < blueMax){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    blueVal += changeRate;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  blueVal = 255;

  while (greenVal > -1){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    greenVal -= changeRate;
    tlcRow(0);
    tlcRow(1);
  }
  delay(500);
  greenVal = 0;

  while (blueVal > -1){
    for (int i=0; i<totalCols; i+=3){
      newRowBytes[0][i] = redVal;
      newRowBytes[0][i+1] = greenVal;
      newRowBytes[0][i+2] = blueVal;
      newRowBytes[1][i] = redVal;
      newRowBytes[1][i+1] = greenVal;
      newRowBytes[1][i+2] = blueVal;
    }
    blueVal -= changeRate;
    tlcRow(0);
    tlcRow(1);
  }

  // To ensure 0,0,0
  for (int i=0; i<totalCols; i+=3){
    newRowBytes[0][i] = 0;
    newRowBytes[0][i+1] = 0;
    newRowBytes[0][i+2] = 0;
    newRowBytes[1][i] = 0;
    newRowBytes[1][i+1] = 0;
    newRowBytes[1][i+2] = 0;
  }
  tlcRow(0);
  tlcRow(1);
  tlcRow(2);
  tlcRow(3);
  tlcRow(4);
  tlcRow(5);
  tlcRow(6);
  tlcRow(7);
  tlcRow(8);
  tlcRow(9);
  tlcRow(10);
  tlcRow(11);
  tlcRow(12);
  tlcRow(13);
  tlcRow(14);
  tlcRow(15);
  updateOutputs();
  delay(5000);
}

// Sets different colour to the columns and shifts them over
// Old function made for 5x2 prototype
void rainbowShift(){
  static boolean setFunction = true;  // first time through function, set it up
  int val = 0;
  int oldBlue1,oldGreen1,oldRed1,oldBlue0,oldGreen0,oldRed0;
  if (setFunction){
    newRowBytes[0][0] = 0;//blue
    newRowBytes[0][1] = 0;
    newRowBytes[0][2] = blueMax;

    newRowBytes[0][3] = 0;//green
    newRowBytes[0][4] = greenMax;
    newRowBytes[0][5] = 0;

    newRowBytes[0][6] = redMax;//orange
    newRowBytes[0][7] = 0.1*greenMax;
    newRowBytes[0][8] = 0;

    newRowBytes[0][9] = redMax;//red
    newRowBytes[0][10] = 0;
    newRowBytes[0][11] = 0;

    newRowBytes[0][12] = redMax;//purple
    newRowBytes[0][13] = 0;
    newRowBytes[0][14] = blueMax;


    newRowBytes[1][0] = 0;//blue
    newRowBytes[1][1] = 0;
    newRowBytes[1][2] = blueMax;

    newRowBytes[1][3] = 0;//green
    newRowBytes[1][4] = greenMax;
    newRowBytes[1][5] = 0;

    newRowBytes[1][6] = redMax;//orange
    newRowBytes[1][7] = 0.1*greenMax;
    newRowBytes[1][8] = 0;

    newRowBytes[1][9] = redMax;//red
    newRowBytes[1][10] = 0;
    newRowBytes[1][11] = 0;

    newRowBytes[1][12] = redMax;//purple
    newRowBytes[1][13] = 0;
    newRowBytes[1][14] = blueMax;

    setFunction = false;  // done setup
    delay(2000);
  }
  else{// Shift row 0 data left and row 1 data right
    //Cycle Colours Right
    oldRed0 = newRowBytes[0][totalCols-3];
    oldGreen0 = newRowBytes[0][totalCols-2];
    oldBlue0 = newRowBytes[0][totalCols-1];
    shiftRight(0);
    newRowBytes[0][0] = oldRed0;
    newRowBytes[0][1] = oldGreen0;
    newRowBytes[0][2] = oldBlue0 ;

    //Cycle Colours Left
    //    oldBlue0 = newRowBytes[0][0];
    //    oldGreen0 = newRowBytes[0][1];
    //    oldRed0 = newRowBytes[0][2];
    //    shiftLeft();
    //    newRowBytes[0][totalCols-3] = oldBlue0;
    //    newRowBytes[0][totalCols-2] = oldGreen0;
    //    newRowBytes[0][totalCols-1] = oldRed0;

    oldRed1 = newRowBytes[1][0];
    oldGreen1 = newRowBytes[1][1];
    oldBlue1 = newRowBytes[1][2];
    shiftLeft(1);
    newRowBytes[1][totalCols-3] = oldRed1;
    newRowBytes[1][totalCols-2] = oldGreen1;
    newRowBytes[1][totalCols-1] = oldBlue1;
  }
  tlcRow(0);
  tlcRow(1);
  delay(1000);

}

// Shift all outputs left (ch1->ch0/ch1->ch0/etc.)
void shiftLeft(int row){
  for (int i=0; i<totalCols-3; i++){//-1 because not using all 16 outputs
    newRowBytes[row][i] = newRowBytes[row][i+3];
  }
}

// Shift all outputs left (ch1->ch0/ch1->ch0/etc.)
void shiftRight(int row){
  for (int i=totalCols-1; i>=3; i--){
    newRowBytes[row][i] = newRowBytes[row][i-3];
  }
}

// Shift Register Functions \\
void setupRegisters(){// Set row0 outpput LOW and the rest HIGH
    registers[0] = LOW;
//  registers[-1] = LOW;
  for (int i=0; i<totalRows-1; i++){
    registers[i] = HIGH;
  }
  PORTC &= ~(1<<7);
  PORTC &= ~(1<<4);
  for (int i=0; i<totalRows; i++){
    PORTC &= ~(1<<5);
    PORTC |= 1<<6;//sethigh
    PORTC |= 1<<5;
    Serial.println(registers[i]);
  }
  Serial.println("----------");
  PORTC |= 1<<7;
  PORTC |= 1<<4;
}

void nextRow(){// Shift data in and set new data bit as HIGH, unless at row0
  PORTC &= ~(1<<7);
  PORTC &= ~(1<<4);
  PORTC &= ~(1<<5);
  if (displayRow == totalRows-1){
    PORTC &= ~(1<<6);// clear low
  }
  else{
    PORTC |= 1<<6;//set high
  }
  PORTC |= 1<<5;
  PORTC |= 1<<7;
  PORTC |= 1<<4; 
  displayRow++;
  if (displayRow == totalRows){
    displayRow = 0;
  }
  delayMicroseconds(1000);
}
//--------------------------------------\\

// Base function from kdarrah. Changed for different type of data addressing and multiplexing
// Converts 8-bit data to 12-bit
void tlcRow(int updateRow){// TLC to UPDATE TLC to UPDATE TLC to UPDATE TLC to UPDATE
  int value;
  for (int i=0; i<totalCols; i++){
    value = map(newRowBytes[updateRow][i],0,256,0,4096)*brightness;  // Adjusts for resolution and brightness ratio
    
    // We need to convert the 12 bit value into an 8 bit BYTE, the SPI can't write 12bits
    if(value>4095)
      value=4095;
    if(value<0)
      value=0;

    //We figure out where in all of the bytes to write to, so we don't have to waste time
    // updating everything

    //12 bits into bytes, a start of 12 bits will either at 0 or 4 in a byte
    spibit=0;
    if(bitRead(i, 0))//if the read of the value is ODD, the start is at a 4
      spibit=4;

    //This is a simplification of channel * 12 bits / 8 bits
    spibyte = int((i)*3/2);//this assignes which byte the 12 bit value starts in

    for(chbit=0; chbit<12; chbit++, spibit++){// start right at where the update will go
      if(spibit==8){//during the 12 bit cycle, the limit of byte will be reached
        spibyte++;//roll into the next byte
        spibit=0;//reset the bit count in the byte
      }
      if(bitRead(value, chbit))//check the value for 1's and 0's
        bitSet(transferBytes[updateIndex][updateRow][spibyte], spibit);//transferbyte is what is written to the TLC
      else
        bitClear(transferBytes[updateIndex][updateRow][spibyte], spibit);
    }//0-12 bit loop
  }//  END OF TLC WRITE  END OF TLC WRITE  END OF TLC WRITE  END OF TLC WRITE  END OF TLC WRITE
}







//}//  END OF TLC WRITE  END OF TLC WRITE  END OF TLC WRITE  END OF TLC WRITE  END OF TLC WRITE
//*******************************************************************************************

void DotCorrection(){//  DOT Correction  DOT Correction  DOT Correction  DOT Correction
  // digitalWrite is slow so mmanipulate port directly. VPRG is digital pin 4
  // Pin 4 is PD4. Port D, Pin 4. PortD, use bitwise OR with a 1, at position 4. Writing a 1 to pin 4
  PORTH |= 1<<5;//VPRG to DC Mode HIGH
  spibyte=0;//reset our variables
  spibit=0;
  // SPI port can only write bytes (8-bits) but each channel needs 6-bit number for DC and must be 
  // sent continuously (one continuous chain)
  for(ch=0; ch<totalCols; ch++){// 6 bit a piece x 32 Outputs
    for(chbit=0; chbit<6; chbit++){
      if(spibit==8){
        spibyte++;
        spibit=0;
      }
      // Checks DC value (initially set to 63), converts it to binary and check all 6 bits (2^6=64) 
      // and set corresponding value in transferbyte (the 
      if(bitRead(DCvalue[ch], chbit))//all 6 bits
        bitSet(transferBytes[0][0][spibyte], spibit);//setting bit 7 of transfer byte
      else
        bitClear(transferBytes[0][0][spibyte], spibit);
      spibit++;
    }//i 
  }//j
  SPI.begin();
  // Start transferring MSB
  for(j=spibyte; j>=0; j--){
    SPI.transfer(transferBytes[0][0][j]);
  }
  // XLAT is PD2, PortD bit 2
  PORTH |= 1<<3;//XLAT the data in
  PORTH &= ~(1<<3);//XLAT data is in now
  PORTH &= ~(1<<5);//VPRG is good to go into normal mode LOW
}//  end of DOT Correction  end of DOT Correction  end of DOT Correction  end of DOT Correction
//*******************************************************************************************

// Switches display and update arrays 
void updateOutputs(){
  displayIndex = (displayIndex+1)%2;
  updateIndex = (updateIndex+1)%2;
}

// Send data to TLC5940
void displayOutputs(){
  PORTH |= 1<<4;// write blank HIGH to reset the 4096 counter in the TLC
  PORTH |= 1<<3;// write XLAT HIGH to latch in data from the last data stream
  PORTH &= ~(1<<3);  //XLAT can go low now
  PORTH &= ~(1<<4);//Blank goes LOW to start the next cycle
  SPI.end();//end the SPI so we can write to the clock pin

  PORTB |= 1<<1;// SPI Clock pin to give it the extra count
  PORTB &= ~(1<<1);// The data sheet says you need this for some reason?
  // Needs pulse number 193 (with no data) for some reason

  SPI.begin();// start the SPI back up
  // 32 channels*16 bits/8 bytes = 48 bytes needed
  for(SINData=(byteCount-1); SINData>=0; SINData--)// send the data out!
    SPI.transfer(transferBytes[displayIndex][displayRow][SINData]);// The SPI port only understands bytes-8 bits wide
  // The TLC needs 12 bits for each channel, so 12bits times 32 channels gives 384 bits
  // 384/8=48 bytes, 0-47
}

// Sends Blank data to TLC5940 to prevent vleeding
void resetRowOutput(){
  PORTH |= 1<<4;// write blank HIGH to reset the 4096 counter in the TLC
  PORTH |= 1<<3;// write XLAT HIGH to latch in data from the last data stream
  PORTH &= ~(1<<3);  //XLAT can go low now
  PORTH &= ~(1<<4);//Blank goes LOW to start the next cycle
  SPI.end();//end the SPI so we can write to the clock pin

  PORTB |= 1<<1;// SPI Clock pin to give it the extra count
  PORTB &= ~(1<<1);// The data sheet says you need this for some reason?
  // Needs pulse number 193 (with no data) for some reason

  SPI.begin();// start the SPI back up
  // 32 channels*16 bits/8 bytes = 48 bytes needed
  for(SINData=(byteCount-1); SINData>=0; SINData--)// send the data out!
    SPI.transfer(0);// The SPI port only understands bytes-8 bits wide
  // The TLC needs 12 bits for each channel, so 12bits times 32 channels gives 384 bits
  // 384/8=48 bytes, 0-47
}

