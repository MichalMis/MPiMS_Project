//Token aplikacji Blynk: 
#define BLYNK_TEMPLATE_ID "TMPL4m5W_oJBH"
#define BLYNK_TEMPLATE_NAME "Health monitoring device"
#define BLYNK_AUTH_TOKEN "GxrQxZlLl-CqYPEE2JNRA5PIUOPJ6zXk"
//////////////////////////////////////////////////////////////////////////
/*
Biblioteki:
*/
#include <Wire.h>               //Biblioteka Wire - używana do komunikacji I2C
#include <LCD_I2C.h>            //Bibliotek dla funkcji wyświetlacza LCD
#include <MillisTimer.h>        //Biblioteka dlaa funkcji millis (czas)
#include <MAX30105.h>           //Biblioteka do obsługi czujnika pulsu  
#include <heartRate.h>          //Biblioteka do obliczania pulsu
#include <WiFi.h>               //Biblioteka do obsługi komunikacji WiFi
#include <BlynkSimpleEsp32.h>   //Biblioteka do obsługi aplikacji Blynk
#include <Adafruit_MLX90614.h>  //Biblioteka do obsługi pirometru
//////////////////////////////////////////////////////////////////////////
/*
Użycie drugiej magistrali I2C:
*/
#define SDA_2 33      //zdefiniowanie pinu 33 jako SDA (I2C2)
#define SCL_2 32      //zdefiniowanie pinu 32 jako SCL (I2C2)

//////////////////////////////////////////////////////////////////////////

/*
Deklaracje adresów dla poszczególnych peryferiów jako zmienne globalne (Lub twzorzenie klas dla danego czujnika):
*/
LCD_I2C lcd(0x27, 16, 2);   // Deklaracja adresu PCF8574. Do znalezienia w plikach biblioteki LCD  
int ADXL345 = 0x53;         // Deklaracja adresu ADXL345 (Akcelerometr) 
MAX30105 Sensor_max;        // Deklaracja obiektu klasy MAX30105 do obsługi czujnika MAX30102
Adafruit_MLX90614 Sensor_mlx = Adafruit_MLX90614();     // Deklaracja obiektu klasy Adafruit_MLX90614 do obsługi czujnika MLX90614

/*
Deklaracja zmiennych do obsługi WiFi i połączenia z aplikacją Blynk
*/
char auth[] = BLYNK_AUTH_TOKEN; //Uwierzytelnianie dla aplikacji Blynk 
char ssid[] = "POCO X3 NFC";            //Nazwa WiFi
char pass[] = "12345678";            //Hasło do WiFi 
BlynkTimer timer;               //Deklaracja pierwszego timera wykorzystywanego przez aplikcje Blynk (Definiuje, co ile odświeżają się dane z "wirtualnych pinów")
BlynkTimer timer2;              //Deklaracja drugiego timera wykorzystywanego przez aplikcje Blynk
BlynkTimer timer3;              //Deklaracja trzeciego timera do obsługi wskaźnika temperatury przez Blynk

/*
Deklaracje zmiennych globalnych dla funkcji millis i obliczania czasu:
*/
unsigned long previousTime = 0;         //Deklaracja poprzedniego czasu zmierzonego podczas wykonywania pomiaru 
unsigned long previousUpdateTime = 0;   //Deklaracja czasu ostatniego odświeżenia (dojścia do czasu update time)
unsigned long updateTime = 1000;        //Deklaracja czasu przy którym program odświeża daną funkcjonalność

/*
Deklaracje zmiennych globalnych do obsługi akcelerometru:
*/
float X_out[100];    //Bufor 100 danych zapisanych z osi X
float Y_out[100];    //Bufor 100 danych zapisanych z osi Y
float Z_out[100];    //Bufor 100 danych zapisanych z osi z
float xyz_sum[100];  //Bufor 100 danych zapisanych sum z osi x,y i z
float xyz_avg[100];  //Bufor 100 danych zapisujących średnią z osi xyz
float X_avg = 0.0;   //Średnia wartośc z osi X
float Y_avg = 0.0;   //Średnia wartość z osi Y
float Z_avg = 0.0;   //Średnia wartość z osi Z
int flag = 0;                       //Flaga używana w funkcji zliczania kroków
int steps = 0;                      //Ilość zliczonych kroków 
int sampling = 0;
float threshold = 2;
unsigned long step_time = 0;        //Zmienne czasowe eliminujące drgania zestyku 
unsigned long last_step_time = 0; 
float avg_sig  = 0; 
float alpha = 0.05;
float minave,maxave; 

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
#define FINGER_ON 7000    //Próg odpowiadający przyłożeniu palca do czujnika 
#define MINIMUM_SPO2 90.0 //Próg minimalnej saturacji krwii

/*
Deklaracja zmiennych używanych do wywołania przerwań za pomocą przycisków
*/
struct Button {                      //Definicja struktury Button 
	const uint8_t PIN;
	uint32_t flag ;
	bool pressed;
};
Button button1 = {18, 0, false};      //Deklaracja obiektu struktury Button do obsługi przycisku 
unsigned long button_time = 0;        //Zmienne czasowe eliminujące drgania zestyku 
unsigned long last_button_time = 0; 

Button button2 = {19, 0, false};  
/*
Deklaracja zmiennych używanych do pomiaru tempertaury 
*/
double temp = 0;

//////////////////////////////////////////////////////////////////////////
/*
Funkcja obsługująca przerwania za pomocą przycisku w programie: 
*/
void IRAM_ATTR isr() {
  button_time = millis();
  if (button_time - last_button_time > 250)
  {
    if(button1.flag == 0)
    {	
      button1.flag = 1;
      button1.pressed = true;
    }
    else if (button1.flag == 1){
      button1.flag = 2;
      button1.pressed = true;
    }
    else{
      button1.flag = 0;
      button1.pressed = true;
    }
    last_button_time = button_time;
  }
}

/*
Funkcja obsługująca przerwania za pomocą przycisku w programie: 
*/
void IRAM_ATTR isr2() {
  button_time = millis();
  if (button_time - last_button_time > 250)
  {
    steps = 0;
    last_button_time = button_time;
  }
}
/*
Funkcja obsługująca aplikacje Blynk (funkcje odowiedzialne za puls i saturacje krwi): 
*/
void Service_Blynk(){
  Blynk.virtualWrite(V2, "Pulsoximeter");
  if(avgHR_Freq> 20){
    Blynk.virtualWrite(V0, avgHR_Freq);
    Blynk.virtualWrite(V4, Filtered_SpO2);
  }
  else{
    Blynk.virtualWrite(V0, 0);
    Blynk.virtualWrite(V4,0);
  }
}

/*
Funkcja obsługująca aplikacje Blynk (funkcje odowiedzialne za zliczanie kroków): 
*/
void Service_Blynk_Steps(){
    Blynk.virtualWrite(V1, steps);
    Blynk.virtualWrite(V2, "Step Counter"); 
}

/*
Funkcja obsługująca aplikacje Blynk (funkcje odowiedzialne za wyświetlanie temperatury): 
*/
void Service_Temperature(){
    Blynk.virtualWrite(V3, temp);
    Blynk.virtualWrite(V2, "Thermometer "); 
}

/*
Funkcja zliczająca kroki: 
*/
void StepCounter(){
  avg_sig = 0; 
  for(int i=0; i<100; i++){
    Wire.beginTransmission(ADXL345);  // Rozpoczęcie komunikacjii z czujnikiem 
    Wire.write(0x32);                 // Użycie rejstrów zaczynając od rejestru 0x32 (ACCEL_XOUT_H)
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ADXL345, (size_t)6, (bool)true);  // Zczytywanie z 6 kolejnych rejestrów. Kazda oś zaajmuje dwa rejestry (w sumie 6)
    X_out[i] = (int16_t)(Wire.read() | (Wire.read() << 8));     // Wrtość Osi X jako 16-bitowy int ze znakiem 
    X_out[i] = X_out[i] / 256.0;                                // Konwersja na floating-point i przeskalowanie przez 256 (dokumentacja)
    Y_out[i] = (int16_t)(Wire.read() | (Wire.read() << 8));     // Wartości Osi Y
    Y_out[i] = Y_out[i] / 256.0;
    Z_out[i] = (int16_t)(Wire.read() | (Wire.read() << 8));     // Wartości Osi Z
    Z_out[i] = Z_out[i] / 256.0;
    xyz_sum[i] = sqrt(((X_out[i] - X_avg)*(X_out[i] - X_avg)) + ((Y_out[i] - Y_avg)*(Y_out[i] - Y_avg)) + ((Z_out[i] - Z_avg)*(Z_out[i] - Z_avg)));                    
    if (i == 0) {                 //Zaimplementowanie filtru dolno przepustowego
      xyz_avg[i] = xyz_sum[i];    // Wartość początkowa filtru
    } 
    else {
      xyz_avg[i] = alpha * xyz_sum[i] + (1 - alpha) * xyz_avg[i - 1]; // Formuła filtru EMA 
    }
    avg_sig = avg_sig + xyz_avg[i];
  }   
  avg_sig = avg_sig / 100;

  if(sampling == 0)
  {
    maxave = avg_sig;
    minave = avg_sig;
  }
  else if(maxave < avg_sig)
  {
    maxave = avg_sig;
  }
  else if(minave > avg_sig)
  {
    minave = avg_sig;
  }

  sampling++;
  
  if(sampling >= 5)
  {
    threshold = (maxave + minave) / 2 + 0.2 ;
    sampling = 0;
  }
  if (avg_sig > threshold && flag == 0)
  {
    flag = 1;
  }
  else if (avg_sig > threshold && flag == 1)
  {
    //Do nothing 
  }
  else if (avg_sig < threshold && flag == 1)
  {
    step_time = millis();
    if (step_time - last_step_time > 250){
      steps = steps + 1;
      flag = 0; 
      last_step_time = step_time;
    }
  }
  Serial.print("XYZ_avg:");
  Serial.println(avg_sig);
  Serial.print(",");
  Serial.print("Threshold:");
  Serial.println(threshold);
  Serial.print(",");
  Serial.print("Steps: ");
  Serial.println(steps);
  previousTime = millis();
  if(previousTime-previousUpdateTime > updateTime){
    previousUpdateTime = previousTime;
    lcd.clear();
    lcd.print("Steps: ");
    lcd.print(steps);
  }    
}

/*
Funkcja obliczająca wartość pulsu: 
*/
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

/*
Funkcja obliczająca wartość saturacji krwi: 
*/
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

/*
Funkcja monitorująca temperature: 
*/
void Thermometer(){
  double samples[1000];
  temp = 0; 
  for (int i = 0; i < 1000; i++){
    samples[i] = Sensor_mlx.readObjectTempC();
    temp += samples[i];
  }
  temp /= 1000;
  Serial.print("Temperature = "); Serial.print(temp); Serial.println("*C");
  Serial.println();
  lcd.clear();
  lcd.print("Temp: ");
  lcd.print(temp);
}

void setup() {
  Blynk.begin(auth, ssid, pass);    //Połączenie z WiFi i aplikacją Blynk
  Serial.begin(115200);             // Inicjalizacja portu szeregowego do wyświetlania daych w Serial monitor
  Wire.begin();                     // Inicjalizacja 1-go portu I2C używanego przez wyświetlacz i czujnik 
  Wire1.begin(SDA_2, SCL_2);
  
  // Ustawienie odpowiednich pocztkowych funkcji wyświetlacza LCD.      
  lcd.begin(false);                 //Używane aby nie uruchamiać funkcjonalności wire.begin po raz drugi 
  lcd.backlight();                  //Włączenie podświetlenia ekranu 
  lcd.clear();                      //Wyczyszczenie ekranu 
  // Ustawienie czujnika ADXL345 w tryb pomiaru.
  Wire.beginTransmission(ADXL345);  // Rozpoczecie komunikacji z czujnikiem ADXL345 
  Wire.write(0x2D);                 // Uruchomienie dostępu do rejestru POWER_CTL - 0x2D (Dokumentacja ADXL345)

  // Uruchomienie funkcji pomiru dla ADXL345 (Dokumentacja )
  Wire.write(8);                    // (8dec -> 0000 1000 binary) Bit D3 Stan wysoki aby aktywować możliwość pomiaru (Dokumentacja) 
  Wire.endTransmission();           // Zakończenia transmisji (dokumentacja)
  delay(10);                        //Opóźnieine 10ms
  
  // Konfiguracja dla czujnika MAX30102
  Serial.println("Initializing...");                //Wyświetlanie informacji o inicjalizacji Max30105
  if (!Sensor_max.begin(Wire1, I2C_SPEED_FAST))     //Użycie portu 2-go I2C i prędkości (deafult) 400kHz
  {                                                 //Sprawdzenie komunikacji z czujnikiem i wyswietlenie odpowiedniej informacji
    Serial.println("Nie wykryto czujnika MAX30102. Sprawdź połączenie z mikrokontrolerem");
    while(1);
  }

  if (!Sensor_mlx.begin()) {                        //Sprawdzenie komunikacji z czujnikiem i wyswietlenie odpowiedniej informacji
		Serial.println("Nie wykryto czujnika mlx. Sprwadź połączenie");
		while(1);
	};

  Sensor_max.setup();                               //Skonfigurowanie czujnika z ustawieniami początkowymi (defaultowymi)
  Sensor_max.setPulseAmplitudeRed(0x0A);            //Ustawienie Czerwonej diody LED w celu rozpoznania działania czujnika (dokumentacja MAX30102) 
  Sensor_max.setPulseAmplitudeGreen(0x0A);          //Wyłączenie diody zielonej (dokumentacja MAX30102)

   
  Sensor_mlx.writeEmissivity(.985);

  pinMode(button1.PIN, INPUT_PULLUP);                   //Inicjalizacja przycisku z rezystorem pullup
	attachInterrupt(button1.PIN, isr, FALLING);           //Inicjalizacja przerwania na przyciksu button1

  pinMode(button2.PIN, INPUT_PULLUP);                   //Inicjalizacja przycisku z rezystorem pullup
	attachInterrupt(button2.PIN, isr2, FALLING);           //Inicjalizacja przerwania na przyciksu button1
  
  timer.setInterval(1000L,Service_Blynk);               //Timer1 aplikacji Blynk
  timer2.setInterval(100L,Service_Blynk_Steps);         //Timer2 aplikacji Blynk
  timer3.setInterval(100L,Service_Temperature);         //Timer3 aplikacji Blynk
}

void loop() {
  Blynk.run();
  if(button1.flag == 0){
    timer2.run();
    StepCounter();
  }
  else if (button1.flag == 1){
    timer.run();
    long IR_Value = Sensor_max.getIR();
    if (IR_Value > FINGER_ON ) {
      HeartBeatRate(IR_Value);
      SpO2_Calc(IR_Value);
    }
    else{
      previousTime = millis();      
      if(previousTime-previousUpdateTime > updateTime){
        previousUpdateTime = previousTime;
        lcd.clear();
        lcd.print("Put the finger");HeartBeatRate(IR_Value);
        SpO2_Calc(IR_Value);
        lcd.setCursor(0,1);
        lcd.print("on the sensor");
      }
      avgHR_Freq = 0;
      RED_RMS = 0.0; IR_RMS = 0.0; SpO2 = 0;
    }
  }
  else {
    timer3.run();
    Thermometer();
  }
}

