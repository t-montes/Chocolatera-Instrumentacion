/* --------------------------------- LIBRERIAS --------------------------------- */
#include "HX711.h" //Modulo HX711 - Celda de carga

/* ----------------------------------- PINES ----------------------------------- */
#define BA_DOUT A1    // Balanza - DT
#define BA_CLK A0     // Balanza - CLOCK
#define MB_FRONT 9    // Minibomba - Control 1, para controlar 'Adelante'
#define MB_BACK 10    // Minibomba - Control 2 , para controlar 'Atras'
#define MP_STEP 4     // Motor PaP - Pin STEP (la DIR no se requiere controlar)
#define MP_ENCA 3     // Motor PaP Encoder - Canal A Dirección
#define MP_ENCB 2     // Motor PaP Encoder - Canal B Ticks

/* ---------------------------- VARIABLES MODULADAS ---------------------------- */
// Balanza
float pesoConocido = 200; // SOLO para hallar la escala de la balanza
float escala = -1308.5;  // Escala de la balanza (después de calibrar)
float pesoVaso = 36;      // Peso real del vaso
float toleranciaVaso = 2;
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
float tempActual; // Temperatura actual del líquido

int modeTemp = 0; // Modo de medir temperatura (0: APAGADO | 1: ENCENDIDO)

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

  pinMode(MB_FRONT, OUTPUT);
  pinMode(MB_BACK, OUTPUT);
  pinMode(MP_STEP, OUTPUT);
  pinMode(MP_ENCB, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(MP_ENCB), countPulses, CHANGE);

  calibracionBalanza();
  Serial.println("\n¡¡¡LISTO PARA PESAR!!!\n");
}

void loop() {
  now = millis();

  if (Serial.available() > 1) {
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
        onOff = modeMix ? 0.3 : 0.75; // Se enciende la minibomba al 75% de su PWM
      } else {
        Serial.println("La dosificación ha finalizado");
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
    printTemp();
    // TODO: Sensor de Temperatura
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
    Serial.println("¡¡¡CALIBRANDO BALANZA!!!");
    Serial.println("\nNo ponga ningun objeto sobre la balanza");
    delay(500);
    balanza.tare(20); // Tara sin el recipiente

    // Acá se calcula la escala de la balanza

    Serial.println("\nColoque el recipiente sobre la balanza");
    for (int i = 0; i < 3; i++) {
      delay(1000);
      Serial.println(3 - i);
    }

    pesoRecipiente = balanza.get_units(20);
    Serial.print("\nPeso del recipiente: ");
    Serial.print(pesoRecipiente/escala);
    Serial.println(" g");


    if ((pesoRecipiente/escala - toleranciaVaso) > pesoVaso || (pesoRecipiente/escala + toleranciaVaso) < pesoVaso) {
      Serial.println("Calibracion erronea\tVolviendo a medir");
      delay(1000);
    } else {
      balanza.tare(100);
      balanza.set_scale(escala);
      break;
    }
  }
}

// Función de medición de la balanza (actualiza la variable pesoActual directamente)
void medidaBalanza(int n) {
  // TODO: recomentar: do { 
  //  pesoActual = balanza.get_units(n); // Entrega el peso actualmente medido en gramos
  //} while (pesoActual > 1000); // if peso > 1000, se mide de nuevo

  if ((now - lastTimeBalanza) >= intervalPrintBalanza) {
    Serial.print("Peso actual: ");
    Serial.print(pesoActual);
    Serial.println(" g");
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
      Serial.println("Minibomba ENCENDIDA");
      Serial.print("PWM: ");
      Serial.print((pwmFuncionamiento * onOff)/2.55);
      Serial.println("%");
      lastTimeMinibomba = now;
    }
  } else {
    //caudal = 1000*(pesoActual/densidad)/(now - timeCaudal);
      // (ms/s) * (g / (g / mL)) / (ms) = (1/s) * (mL) = mL/s
    Serial.println("Minibomba APAGADA");

    //Serial.print("Caudal: ");
    //Serial.print(caudal);
    //Serial.println(" mL/s");
  }
}

// Función de impresión de los RPM del motor
void printRpmMotor(void) {
  if ((now - lastTimeMotor) >= intervalPrintMotor) {
    rpm = ((double) encoderCount/960)*60;
    Serial.print("RPM: ");
    Serial.println(rpm);
    encoderCount = 0;
    lastTimeMotor = now;
  }
}

// Función de impresión de la temperatura
void printTemp(void) {
  if ((now - lastTimeTemp) >= intervalPrintTemp) {
    // TODO: Sensor de Temperatura
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
  cmd = Serial.readString();
  cmd.trim();
  cmdSep = cmd.indexOf(" ");
  cmdType = cmd.substring(0, cmdSep);
  cmdValue = cmd.substring(cmdSep + 1);

  switch (cmdType[0]) {
    case 'v':
      volumenDeseado = cmdValue.toInt();
      pesoDeseado = volumenDeseado * densidad;
      modeVol = 1;

      Serial.println("\n¡¡¡LISTO PARA DOSIFICAR!!!\n");
      Serial.print("Se van a completar ");
      Serial.print(volumenDeseado);
      Serial.print(" mL (");
      Serial.print(pesoDeseado);
      Serial.println(" g)");
      delay(500);
      break;

    case 'm':
      rpmDeseado = cmdValue.toInt();
      delayRpm = pow(2*8.69799685132514e-6*rpmDeseado, -1.0330578512396695);
      modeMix = 1;

      Serial.println("\n¡¡¡LISTO PARA MEZCLAR!!!\n");
      Serial.print("Se van a mezclar a ");
      Serial.print(rpmDeseado);
      Serial.print(" RPM ( delay de ");
      Serial.print(delayRpm);
      Serial.println(" us)");
      delay(500);
      break;

    case 't':
      // TODO: Sensor de Temperatura

      modeTemp = 1;
      break;

    case 'b':
      calibracionBalanza();
      modeVol = 0;
      modeMix = 0;
      modeTemp = 0;

      Serial.println("\n¡¡¡LISTO PARA PESAR!!!\n");
      break;

    case 's':
      modeVol = 0;
      modeMix = 0;
      modeTemp = 0;

      Serial.println("\n¡¡¡SE HA DETENIDO TODO!!!\n");
      break;
    
    case 'o':
      switch (cmdValue[0]) {
        case 'v':
          modeVol = 0;
          Serial.println("\nSE HA DETENIDO LA DOSIFICACIÓN\n");
          break;
        case 'm':
          modeMix = 0;
          Serial.println("\nSE HA DETENIDO EL MEZCLADO\n");
          break;
        case 't':
          modeTemp = 0;
          Serial.println("\nSE HA DETENIDO LA MEDICIÓN DE TEMPERATURA\n");
          break;
        default:
          Serial.println("Comando no reconocido");
          break;
      }
      break;

    default:
      Serial.println("Comando no reconocido");
      break;
  }

  firstTime = modeVol ? 1 : 0;
  onOff = modeVol ? (modeMix ? 0.3 : 1) : 0;

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
    Serial.println("Modo de dosificación por volumen");
  }
  if (modeMix) {
    Serial.println("Modo de mezclado");
  }
  if (modeTemp) {
    Serial.println("Modo de medición de temperatura");
  }
  if (modeNone) {
    Serial.println("No hay modos activos");
  }
}
