/* --------------------------------- LIBRERIAS --------------------------------- */
#include "HX711.h" //Modulo HX711 - Celda de carga
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SoftwareSerial.h>

/* ----------------------------------- PINES ----------------------------------- */
#define BA_DOUT A1       // Balanza - DT
#define BA_CLK A0        // Balanza - CLOCK
#define MB_FRONT 8       // Minibomba - Control 1, para controlar 'Adelante'
#define MB_BACK 9        // Minibomba - Control 2 , para controlar 'Atras'
#define MP_STEP 4        // Motor PaP - Pin STEP (DIR no se requiere controlar)
#define MP_ENCA 5        // Motor PaP Encoder - Canal A Dirección
#define MP_ENCB 3        // Motor PaP Encoder - Canal B Ticks
const int TMP = 2;       // Sensor de temperatura
const int BT_RX = 10;    // Bluetooth RX
const int BT_TX = 11;    // Bluetooth TX

/* ---------------------------- VARIABLES MODULADAS ---------------------------- */
// Balanza
float pesoConocido = 200; // SOLO para hallar la escala de la balanza
float escala = -1308.5;  // Escala de la balanza (después de calibrar)
float pesoVaso = 41;      // Peso real del vaso
float toleranciaVaso = 3;
int numMedidas = 1;     // Número de medidas para promediar (entre más, más lento)

// Minibomba
float pesoOffset = 1.5; // El peso anterior desde el cual se apaga la minibomba
float pwmFuncionamiento = 255; // % (0-255) del PWM de funcionamiento de la minibomba
int densidad = 1; // Densidad del líquido a dosificar (g/mL)

/* ---------------------------- VARIABLES DEL CÓDIGO --------------------------- */
// Balanza
HX711 balanza(BA_DOUT, BA_CLK); // Balanza
float pesoRecipiente = 0; // Peso del recipiente
float pesoActual; // Peso medido por la balanza

// Minibomba
int firstTime = 1; // Para calcular el caudal de la minibomba (1 | 0)
//int timeCaudal = 0; // Tiempo transcurrido para calcular el caudal (minibomba)
//float caudal = 0; // Caudal de la minibomba (mL/s)

int modeVol = 0; // Modo de dosificación (0: APAGADO | 1: ENCENDIDO)
float volumenDeseado; // Volumen deseado a dosificar (se especifica por consola)
float pesoDeseado; // Peso deseado a dosificar (se calcula a partir del volumen)

// Motor PaP
float onOff = 1; // % (0-1) del PWM del motor PaP
volatile int encoderCount = 0; // Contador de ticks del encoder
double rpm; // RPM del motor PaP, medidos por el encoder
double delayRpm; // Delay calculado (en us) para ir a los RPM deseados

int modeMix = 0; // Modo de mezclado (0: APAGADO | 1: ENCENDIDO)
float rpmDeseado; // RPM deseado a mezclar (se especifica por consola)

// Temperatura
OneWire oneWireObjeto(TMP);
DallasTemperature sensorDS18B20(&oneWireObjeto);
float tempActual; // Temperatura actual del líquido

int modeTemp = 0; // Modo de medir temperatura (0: APAGADO | 1: ENCENDIDO)

// Modulo Bluetooth
SoftwareSerial BT(BT_RX, BT_TX);

// Comandos
String cmd; // Comando recibido por consola
int cmdSep; // Separador del tipo y valor del comando
String cmdType; // Tipo de comando recibido por consola
String cmdValue; // Valor del comando recibido por consola

// Adicionales
int modeNone = 1; // Modo de no hacer nada (0: APAGADO | 1: ENCENDIDO)
unsigned long now;

unsigned long lastTimeBalanza = 0; // Tiempo desde la última impresión
int intervalPrintBalanza = 0; // (MODIFICABLE) Intervalo para imprimir el peso de la balanza (ms)

unsigned long lastTimeMinibomba = 0; // Tiempo desde la última impresión
int intervalPrintMinibomba = 0; // (MODIFICABLE) Intervalo para imprimir el estado de la minibomba (ms)

unsigned long lastTimeMotor = 0; // Tiempo desde la última impresión
int intervalPrintMotor = 0; // (MODIFICABLE) Intervalo para imprimir los RPM del motor (ms)

unsigned long lastTimeTemp = 0; // Tiempo desde la última impresión
int intervalPrintTemp = 0; // (MODIFICABLE) Intervalo para imprimir la temperatura (ms)

void setup() {
  Serial.begin(9600);
  BT.begin(9600);
  sensorDS18B20.begin();

  pinMode(MB_FRONT, OUTPUT);
  pinMode(MB_BACK, OUTPUT);
  pinMode(MP_STEP, OUTPUT);
  pinMode(MP_ENCB, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(MP_ENCB), countPulses, CHANGE);

  while (BT.available() == 0) {}
  BT.readString();

  BT.println("¡Encendido!");
  
  calibracionBalanza(); 
  BT.println("¡¡¡LISTO PARA PESAR!!!");
  Serial.println("\n¡¡¡LISTO PARA PESAR!!!\n");
}

void loop() {
  now = millis();

  if (BT.available() > 1) {
    // Digitar los comandos de la forma: "modo valor"
    changeMode();
  }

  if (modeNone) {
    medidaBalanza(numMedidas);
    return;
  }
  
  /* -------------------------------- MINIBOMBA -------------------------------- */
  if (modeVol) {
    printStatusMinibomba();
    // ENCENDIDA
    if (onOff > 0) {
      if (firstTime) {
        //timeCaudal = now; // Reiniciar el tiempo para medir el caudal
        firstTime = 0;
        BT.println("Encendiendo minibomba!");
        analogWrite(MB_FRONT, pwmFuncionamiento * onOff);
      }

      medidaBalanza(numMedidas);

      if (!(pesoDeseado >= (pesoActual + pesoOffset))) {
        onOff = 0;
        analogWrite(MB_FRONT, 0);
      }
    } 
    // APAGADA
    else {
      medidaBalanza(5);
      medidaBalanza(15);
      if (pesoDeseado >= (pesoActual + pesoOffset)) {
        firstTime = 1;
        onOff = modeMix ? 0.65 : 0.75; // Se enciende la minibomba al 75% de su PWM
      } else {
        BT.println("La dosificación ha finalizado");
        modeVol = 0;
        modeChanged();
      }
    }
  }

  /* ---------------------------------- MOTOR ---------------------------------- */
  if (modeMix) {
    printRpmMotor();
    // Si no está encendido el modo de volumen, igual toca mostrar el peso
    medidaBalanza(numMedidas);
    
    digitalWrite(MP_STEP, HIGH);
    delayMicroseconds(delayRpm);
    digitalWrite(MP_STEP, LOW);
    delayMicroseconds(delayRpm);
  }

  /* ------------------------------- TEMPERATURA ------------------------------- */
  if (modeTemp) {
    medidaBalanza(numMedidas);
    printTemp();
  }

}

/* --------------------------------- FUNCIONES --------------------------------- */

// Función de conteo de ticks del encoder
void countPulses(void) {
  encoderCount++;
}

// Función de calibración de la balanza
void calibracionBalanza() {
  while (true) {
    BT.println("¡¡¡CALIBRANDO BALANZA!!!");
    Serial.println("¡¡¡CALIBRANDO BALANZA!!!");
    BT.println("\nNo ponga ningun objeto sobre la balanza");
    Serial.println("\nNo ponga ningun objeto sobre la balanza");
    delay(500);
    balanza.tare(20); // Tara sin el recipiente

    BT.println("\nColoque el recipiente sobre la balanza");
    Serial.println("\nColoque el recipiente sobre la balanza");
    for (int i = 0; i < 3; i++) {
      delay(1000);
      BT.println(3 - i);
      Serial.println(3 - i);
    }

    pesoRecipiente = balanza.get_units(20);
    BT.print("\nPeso del recipiente: ");
    Serial.print("\nPeso del recipiente: ");
    BT.print(pesoRecipiente/escala);
    Serial.print(pesoRecipiente/escala);
    BT.println(" g");
    Serial.println(" g");


    if ((pesoRecipiente/escala - toleranciaVaso) > pesoVaso || (pesoRecipiente/escala + toleranciaVaso) < pesoVaso) {
      BT.println("Calibracion erronea\tVolviendo a medir");
      Serial.println("Calibracion erronea\tVolviendo a medir");
      delay(4000);
    } else {
      balanza.tare(100);
      balanza.set_scale(escala);
      break;
    }
  }
}

// Función de medición de la balanza (actualiza la variable pesoActual directamente)
void medidaBalanza(int n) {
  if ((now - lastTimeBalanza) >= intervalPrintBalanza) {
    BT.print("Peso actual: ");
    BT.print(pesoActual);
    BT.println(" g");
    lastTimeBalanza = now;
    do { 
      pesoActual = balanza.get_units(n); // Entrega el peso actualmente medido en gramos
    } while (pesoActual > 1000); // if peso > 1000, se mide de nuevo
  }
}

// Función de impresión del estado de la minibomba
void printStatusMinibomba(void) {
  if (onOff > 0) {
    if ((now - lastTimeMinibomba) >= intervalPrintMinibomba) {
      BT.println("Minibomba ENCENDIDA");
      BT.print("PWM: ");
      BT.print((pwmFuncionamiento * onOff)/2.55);
      BT.println("%");
      lastTimeMinibomba = now;
    }
  } else {
    BT.println("Minibomba APAGADA");

    //caudal = 1000*(pesoActual/densidad)/(now - timeCaudal);
      // (ms/s) * (g / (g / mL)) / (ms) = (1/s) * (mL) = mL/s
    //BT.print("Caudal: ");
    //BT.print(caudal);
    //BT.println(" mL/s");
  }
}

// Función de impresión de los RPM del motor
void printRpmMotor(void) {
  if ((now - lastTimeMotor) >= intervalPrintMotor) {
    rpm = ((double) encoderCount/960)*60;
    BT.print("RPM: ");
    BT.println(rpm);
    encoderCount = 0;
    lastTimeMotor = now;
  }
}

// Función de impresión de la temperatura
void printTemp(void) {
  if ((now - lastTimeTemp) >= intervalPrintTemp) {
    sensorDS18B20.requestTemperatures();

    BT.print("Temperatura sensor 1: ");
    BT.print(sensorDS18B20.getTempCByIndex(0));
    BT.println(" C");

    lastTimeTemp = now;
  }
}

// Función de comandos
void changeMode(void) {
  /**
   * Modos:
   * - v X: Dosificación por volumen (X: volumen deseado)
   * - m X: Mezclado a ciertas RPM (X: RPM deseadas)
   * - t: Medición de temperatura
   * - b: Calibración de la balanza (comando de una vez)
   * - s: Detener todos los modos
   * - o Y: Off Y (apagar el modo Y)
   */
  cmd = BT.readString();
  BT.print("BT received: ");
  BT.println(cmd);
  cmd.trim();
  cmdSep = cmd.indexOf(" ");
  cmdType = cmd.substring(0, cmdSep);
  cmdValue = cmd.substring(cmdSep + 1);

  switch (cmdType[0]) {
    case 'v':
      volumenDeseado = cmdValue.toInt();
      pesoDeseado = volumenDeseado * densidad;
      modeVol = 1;

      BT.println("\n¡¡¡LISTO PARA DOSIFICAR!!!\n");
      BT.print("Se van a completar ");
      BT.print(volumenDeseado);
      BT.print(" mL (");
      BT.print(pesoDeseado);
      BT.println(" g)");
      delay(500);
      break;

    case 'm':
      rpmDeseado = cmdValue.toInt();
      delayRpm = pow(2*8.69799685132514e-6*rpmDeseado, -1.0330578512396695);
      modeMix = 1;

      BT.println("\n¡¡¡LISTO PARA MEZCLAR!!!\n");
      BT.print("Se van a mezclar a ");
      BT.print(rpmDeseado);
      BT.print(" RPM ( delay de ");
      BT.print(delayRpm);
      BT.println(" us)");
      delay(500);
      break;

    case 't':
      modeTemp = 1;

      BT.println("\n¡¡¡LISTO PARA MEDIR TEMPERATURA!!!\n");
      delay(500);      
      break;

    case 'b':
      calibracionBalanza();
      modeVol = 0;
      modeMix = 0;
      modeTemp = 0;

      BT.println("\n¡¡¡LISTO PARA PESAR!!!\n");
      break;

    case 's':
      modeVol = 0;
      modeMix = 0;
      modeTemp = 0;

      BT.println("\n¡¡¡SE HA DETENIDO TODO!!!\n");
      break;
    
    case 'o':
      switch (cmdValue[0]) {
        case 'v':
          modeVol = 0;
          BT.println("\nSE HA DETENIDO LA DOSIFICACIÓN\n");
          break;
        case 'm':
          modeMix = 0;
          BT.println("\nSE HA DETENIDO EL MEZCLADO\n");
          break;
        case 't':
          modeTemp = 0;
          BT.println("\nSE HA DETENIDO LA MEDICIÓN DE TEMPERATURA\n");
          break;
        default:
          BT.println("Comando no reconocido");
          break;
      }
      break;

    default:
      BT.println("Comando no reconocido");
      break;
  }

  firstTime = modeVol ? 1 : 0;
  onOff = modeVol ? (modeMix ? 0.65 : 1) : 0;

  modeChanged();
}

void modeChanged(void) {
  if (modeVol || modeMix || modeTemp) {
    modeNone = 0;
    intervalPrintBalanza = (modeMix && modeVol) ? 900 : (modeMix ? 6000 : 0);
    intervalPrintMinibomba = (modeMix && modeVol) ? 900 : (modeMix ? 6000 : 0);
    intervalPrintMotor = (modeMix && modeVol) ? 900 : (modeMix ? 1200 : 0);
    intervalPrintTemp = (modeMix && modeVol) ? 900 : (modeMix ? 6000 : 0);
  } else {
    modeNone = 1;
    intervalPrintBalanza = 0;
    intervalPrintMinibomba = 0;
    intervalPrintMotor = 0;
    intervalPrintTemp = 0;
  }
  
  if (modeVol) {
    BT.println("Modo de dosificación por volumen");
  }
  if (modeMix) {
    BT.println("Modo de mezclado");
  }
  if (modeTemp) {
    BT.println("Modo de medición de temperatura");
  }
  if (modeNone) {
    BT.println("No hay modos activos");
  }
}
