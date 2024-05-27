
#include <Wire.h>               //Biblioteka Wire - używana do komunikacji I2C
#include <MAX30105.h>           //Biblioteka do obsługi czujnika pulsu  
#include <heartRate.h>          //Biblioteka do obliczania pulsu

#define FINGER_ON 7000    //Próg odpowiadający przyłożeniu palca do czujnika 

MAX30105 Sensor_max;        // Deklaracja obiektu klasy MAX30105 do obsługi czujnika MAX30102
/*
Deklaracje zmiennych globalnych do obliczenia pulsu:
*/
const byte counterLimit = 4; //Zmienna definiująca ilość wartości do uśredniania, 4 jest wystraczjące. 
byte beats[counterLimit];    //Bufor z wynikami zebranych wartości pulsu
byte beatsCounter = 0; 
long previousBeat = 0;       //Czas w którym wystąpiło ostatnie bicie serca
float HR_Freq;               //Wartość pulsu 
int avgHR_Freq;              //Uśredniona wartość pulsu 

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

void setup() {
  // put your setup code here, to run once:

}

void loop() {
  long IR_Value = Sensor_max.getIR();
  if (IR_Value > FINGER_ON ) {
    HeartBeatRate(IR_Value);
  }

}
