
#include <Wire.h>               //Biblioteka Wire - używana do komunikacji I2C
#include <MAX30105.h>           //Biblioteka do obsługi czujnika pulsu  
#include <heartRate.h>          //Biblioteka do obliczania pulsu
#include <MillisTimer.h>        //Biblioteka dlaa funkcji millis (czas)
#include <LCD_I2C.h>            //Bibliotek dla funkcji wyświetlacza LCD

#define FINGER_ON 7000    //Próg odpowiadający przyłożeniu palca do czujnika 
#define MINIMUM_SPO2 90.0 //Próg minimalnej saturacji krwii

/*
Deklaracje zmiennych globalnych dla funkcji millis i obliczania czasu:
*/
unsigned long previousTime = 0;         //Deklaracja poprzedniego czasu zmierzonego podczas wykonywania pomiaru 
unsigned long previousUpdateTime = 0;   //Deklaracja czasu ostatniego odświeżenia (dojścia do czasu update time)
unsigned long updateTime = 1000;        //Deklaracja czasu przy którym program odświeża daną funkcjonalność

MAX30105 Sensor_max;        // Deklaracja obiektu klasy MAX30105 do obsługi czujnika MAX30102
LCD_I2C lcd(0x27, 16, 2);   // Deklaracja adresu PCF8574. Do znalezienia w plikach biblioteki LCD
/*
Deklaracje zmiennych globalnych do obliczenia pulsu:
*/
const byte counterLimit = 4; //Zmienna definiująca ilość wartości do uśredniania, 4 jest wystraczjące. 
byte beats[counterLimit];    //Bufor z wynikami zebranych wartości pulsu
byte beatsCounter = 0; 
long previousBeat = 0;       //Czas w którym wystąpiło ostatnie bicie serca
float HR_Freq;               //Wartość pulsu 
int avgHR_Freq;              //Uśredniona wartość pulsu 

/*
Deklaracje zmiennych globalnych do obliczenia SPO2:
*/
double avgRed_SpO2 = 0;        //Średnia wartość odczytana za pomocą diody "Red"
double avgIR_SpO2 = 0;         //Średnia wartość odczytana za pomocą diody "IR"
double IR_RMS = 0;      
double RED_RMS = 0;
double SpO2 = 0;
double Filtered_SpO2 = 0.0;     //Obliczona wartość saturacji krwii 
double Alpha_SpO2 = 0.6;        //Współczynnik filtru używanego do obliczenia Saturacji krwii
double filter_rate = 0.95;      //Filtr dolno-przepustowy dla wartości diod IR/Red aby wyeliminować szumy
int k = 0;                
int Num = 30;             //Czas próbkowania dla obliczania Saturacji krwii 
uint32_t IR_SpO2, Red_SpO2;
double double_IR_SpO2, double_Red_SpO2;

void HeartBeatRate(long IR_Value) {
	if (checkForBeat(IR_Value) == true) {
		long delta = millis() - previousBeat;
		previousBeat = millis();

		HR_Freq = 60 / (delta / 1000.0);

		if (HR_Freq > 20 && HR_Freq < 240) {
			beats[beatsCounter++] = (byte)HR_Freq;
			beatsCounter %= counterLimit;
      avgHR_Freq = 0;
			for (byte x = 0; x < counterLimit; x++) {
				avgHR_Freq += beats[x];
			}
			avgHR_Freq /= counterLimit;
		}
	}
}

void SpO2_Calc(long IR_Value){
  Sensor_max.check(); //Funkcja sprawdzająca wartość czujnika, zczytująca do 3 próbek.
  if (Sensor_max.available()) {
    k++;
    IR_SpO2 = Sensor_max.getFIFOIR(); 
    Red_SpO2 = Sensor_max.getFIFORed(); 
    double_IR_SpO2 = (double)IR_SpO2;   //Rzutowanie na double
    double_Red_SpO2 = (double)Red_SpO2; //Rzutowanie na double
    avgIR_SpO2 = avgIR_SpO2 * filter_rate + (double)IR_SpO2 * (1.0 - filter_rate);     //Średnia wartość IR wylilczona za pomocą filtru Low-Pass
    avgRed_SpO2 = avgRed_SpO2 * filter_rate + (double)Red_SpO2 * (1.0 - filter_rate);  //Średnia wartość światła czerwonego wylilczona za pomocą filtru Low-Pass
    IR_RMS += (double_IR_SpO2 - avgIR_SpO2) * (double_IR_SpO2 - avgIR_SpO2);              //suma kwadratowa alternatywnego składnika poziomu IR
    RED_RMS += (double_Red_SpO2 - avgRed_SpO2) * (double_Red_SpO2 - avgRed_SpO2);         //suma kwadratowa alternatywnego składnika poziomu Czerwonego
    if ((k % Num) == 0) {
      double Ratio = (sqrt(IR_RMS) / avgIR_SpO2) / (sqrt(RED_RMS) / avgRed_SpO2);
      SpO2 = -23.3 * (Ratio - 0.4) + 100;
      Filtered_SpO2 = Alpha_SpO2 * Filtered_SpO2 + (1.0 - Alpha_SpO2) * SpO2;         //Filtr dolno-przepustowy
      if (Filtered_SpO2 <= MINIMUM_SPO2){                           //Warunek sprawdzający przyłożenie palca 
        Filtered_SpO2 = MINIMUM_SPO2;
      }     
      if (Filtered_SpO2 > 100){                                     //Warunek zapewniający o nie pojawieniu się wyniku 100  
        Filtered_SpO2 = 99.9;
      }
      if (avgHR_Freq > 20){
        Serial.println();
        Serial.print("SPO2 = "); 
        Serial.print(Filtered_SpO2);
        Serial.print(", BPM=");
        Serial.print(HR_Freq);
        Serial.print(", Avg BPM=");
        Serial.print(avgHR_Freq);
        Serial.println();
        previousTime = millis();
        if(previousTime-previousUpdateTime > updateTime){
          previousUpdateTime = previousTime;
          lcd.clear();
          lcd.print("Pulse: ");
          lcd.print(avgHR_Freq);
          lcd.setCursor(0,1);
          lcd.print("SPO2: ");
          lcd.print(Filtered_SpO2);
        }            
      }
      else{
        Serial.print("Measuring...");
        Serial.println();
        previousTime = millis();
        if(previousTime-previousUpdateTime > updateTime){
          previousUpdateTime = previousTime;
          lcd.clear();
          lcd.print("Measuring...");
        }
      }
      RED_RMS = 0.0; IR_RMS = 0.0; SpO2 = 0;
      k = 0;
    }
  Sensor_max.nextSample();       //Zostały zakończone działania na danej próbce więc program może przejśc do następnej 
  } 
}

void setup() {
  // put your setup code here, to run once:

}

void loop() {
  long IR_Value = Sensor_max.getIR();
  if (IR_Value > FINGER_ON ) {
    HeartBeatRate(IR_Value);
    SpO2_Calc(IR_Value);
  }

}
