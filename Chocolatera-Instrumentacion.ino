//Librerias
#include "HX711.h" //Modulo HX711 - Celda de carga

// Pines
#define DOUT A1     // DT Balanza
#define CLK A0      // CLOCK Balanza
#define Input1 9    // Control 1 Minibomba - para controlar 'Adelante'
#define Input2 10   // Control 2 Minibomba - para controlar 'Atras'
#define STEP 4      // Pin pasos Motor Paso a Paso (la dirección no se requiere controlar)
#define EncoderA 3  // Canal A Encoder, Motor PaP - Dirección
#define EncoderB 2  // Canal B Encoder, Motor PaP - Ticks


# define nModes 3  // vol, mix, temp, ...

// VARIABLES MODIFICABLES
float Peso_conocido = 220;// en gramos | usado para calibrar la balanza
float Escala = -1308.5; // Escala actual a la que funciona la balanza (hallada al calibrar)
float PesoRealVaso = 9; // TODO: Peso real del vaso (sin líquido) en gramos
float ToleranciaCalibracion = 2; // Tolerancia de la calibración (en gramos)

float PesoOffset = 2; // El peso anterior desde el cual se apaga la motobomba, teniendo en cuenta el agua sobrante que puede excederse de la manguera.
float Var = 1024; // Valor por defecto de PWM (a escala de 1024)
int numMedidas = 1; // Numero de medidas que se toman para promediar (balanza)

// Variables agregadas
int First = 1; // Para que la primera vez que se encienda la bomba no se tome el tiempo de caudal
float pesoDeseado; // Peso deseado a dosificar (se especifica por consola)
float rpmDeseado; // RPM deseado a mezclar (se especifica por consola)
int timeCaudal = 0; // Tiempo transcurrido para calcular el caudal (minibomba)

// Variables balanza
HX711 balanza(DOUT, CLK);

// Variables para mini bomba de agua
int Densidad = 1;// en g/mL
float OnOff = 1.0; // Switch On/Off para la bomba (1 = On | 0 = Off)
float Adelante; // Variable PWM 1
float Atras; // Variable PWM 2 - NO USADA, PORQUE LA MINIBOMBDA SOLO PUEDE ENTREGAR AGUA
float Detener = map(0, 0, 1023, 0, 255); // 0 
float pesoRecipiente = 0;
float pesoBase; // Peso base del recipiente
float pesoLiquido;

// Variables para el motor paso a paso y su encoder
volatile int encoderCount = 0;
double delayRpm;
unsigned long lastTime = 0;
unsigned long lastTime2 = 0; 

// Variables de modo del sistema
String mode[nModes];


void setup() {
  Serial.begin(9600);

  resetModes();
  Adelante = map(Var, 0, 1023, 0, 255); // Adelante = Var*255/1023 // regla de 3

  // Inicializar los pines
  pinMode( Input1, OUTPUT );
  pinMode( Input2, OUTPUT );
  pinMode( STEP, OUTPUT );
  pinMode( EncoderB, INPUT_PULLUP );

  attachInterrupt(digitalPinToInterrupt(EncoderB), countPulses, CHANGE);

  // Balanza
  CalibracionBalanza();
  Serial.println(" ");
  Serial.println("¡¡¡LISTO PARA PESAR!!!");
  Serial.println(" ");

  chMode();
}

void loop() {
  unsigned long now = millis();
  // Mide el peso del líquido

  if (isMode("vol")) {
    pesoLiquido = MedidaBalanza(numMedidas);
    pesoLiquido = pesoLiquido >= 0 ? pesoLiquido : -pesoLiquido;
    //Serial.print("Peso balanza: ");
    //Serial.println(pesoLiquido);

    //Serial.print("Peso deseado: ");
    //Serial.println(pesoDeseado);

    // Si la minibomba está encendida, seguir dosificando
    if (OnOff > 0) {
      // Si se digita un nuevo valor, cambiar el volumen deseado y reiniciar el proceso
      if (Serial.available() > 1) {
        chMode();
        return;
      }
      
      First = 0; // TODO: Mover esta línea adentro del if ???
      if (First == 1) { 
        timeCaudal = millis();
        return;
      }
      // Si el peso deseado es mayor al peso líquido menos X gramos, seguir dosificando
      if ((pesoDeseado) >= (pesoLiquido + PesoOffset)) {
        //Serial.print("Minibombda encendida! : PWM = ");
        //Serial.print(100*Adelante/255);
        //Serial.println("%");
        analogWrite(Input1, Adelante*OnOff);
      }
      // Si el peso deseado es menor al peso líquido menos X gramos, parar
      else if ((pesoDeseado) <= (pesoLiquido + PesoOffset)) {
        Serial.print("Minibomba apagando... : CAUDAL = ");
        OnOff = 0.0;
        analogWrite(Input1, Detener);
        Serial.print(1000*(pesoLiquido/Densidad)/(millis() - timeCaudal));
          // (ms/s) * (g / (g / mL)) / (ms) = (1/s) * (mL) = mL/s
        Serial.println(" mL/s");
        
        Serial.print("Tiempo: ");
        Serial.print(1000/(millis() - timeCaudal));
        Serial.println(" s");
      }
      else {
        Serial.println("????"); // Nunca debería llegar aquí
      }
    }
    // Si la minibomba está apagada, esperar a que se digite un nuevo volumen
    else {
        pesoLiquido = MedidaBalanza(5);
        pesoLiquido = pesoLiquido >= 0 ? pesoLiquido : -pesoLiquido;
        Serial.print("Peso balanza: ");
        Serial.println(pesoLiquido);
    
        Serial.print("Peso deseado: ");
        Serial.println(pesoDeseado);
        if ((pesoDeseado) >= (pesoLiquido + PesoOffset)) {
          OnOff = 0.9; // Se dosifica al 90% de PWM
          return;
        }
        
        removeMode("vol");
        chMode();
    }
  } 
  
  if (isMode("mix")) {
    if (Serial.available() > 1) {
      chMode();
      return;
    }

    if (now - lastTime >= 1000) {
      double rpm = ((double) encoderCount/800)*60;
      Serial.print("RPM: ");
      Serial.println(rpm);
      encoderCount = 0;

      lastTime = now;
    }

    if (now - lastTime2 >= 10000) {
      pesoLiquido = MedidaBalanza(numMedidas);
      pesoLiquido = pesoLiquido >= 0 ? pesoLiquido : -pesoLiquido;
      Serial.print("Peso balanza: ");
      Serial.println(pesoLiquido);

      lastTime2 = now;
    }

    digitalWrite(STEP, HIGH);
    delayMicroseconds(delayRpm);
    digitalWrite(STEP, LOW);
  } 
}

// FUNCIONES

// Función para cambiar la etapa del sistema
void chMode() {
  /**
   * Obtiene el modo del sistema y luego su parámetro
   */
  // 1. Apagar todos los procesos
  analogWrite(Input1, Detener);

  // Recibir 2 cosas EN ORDEN: Primero un comando 'vol' o 'mix' y luego un parámetro (float) y pasar a la etapa correspondiente
  String mode2 = "";
  while (mode2 != "vol" && mode2 != "mix" && mode2 != "temp") {
    Serial.println("Digite por consola el modo de operacion\n\t> vol: Dosificar un volumen de liquido\n\t> mix: Mezclar el liquido a ciertos RPM\n\t> temp: Medir la temperatura\n\t> tare: Calibrar la balanza\n\t> stop: Detener todos los procesos");
    while (!Serial.available()) {
      pesoLiquido = MedidaBalanza(numMedidas);
      Serial.print("Peso actual: ");
      Serial.print(pesoLiquido);
      Serial.println(" g");
    }
    mode2 = Serial.readStringUntil('\n');
    mode2.trim();
    // One-time modes: stop & tare
    if (mode2 == "stop") {
      resetModes();
    } else if (mode2 == "tare") {
      CalibracionBalanza();
    } else {
      // si ya está en el modo, no agregarlo
      if (!isMode(mode2)) {
        addMode(mode2);
      }
    }
    Serial.println(" ");
    printModes();
    Serial.println(" ");
  }

  if (mode2 == "vol") {
    OnOff = 1;
    Serial.println("Digite por consola el valor en mL a completar una vez este el peso...");

    while (!Serial.available()) {
      pesoLiquido = MedidaBalanza(numMedidas);
      Serial.print("Peso actual: ");
      Serial.print(pesoLiquido);
      Serial.println(" g");
    }

    float volumenDeseado = Serial.parseFloat();
    pesoDeseado = volumenDeseado * Densidad;
    
    Serial.print("Se van a completar ");
    Serial.print(volumenDeseado);
    Serial.print(" mL (");
    Serial.print(pesoDeseado);
    Serial.print(" g)");
    delay(500);
    
    Serial.println(" ");
    Serial.println("¡¡¡LISTO PARA DOSIFICAR!!!");
    Serial.println(" ");
    Serial.println(" ");
  } else if (mode2 == "mix") {
    Serial.println("Digite por consola los RPM a los que desea mezclar...");

    while (!Serial.available()) {
      pesoLiquido = MedidaBalanza(numMedidas);
      Serial.print("Peso actual: ");
      Serial.print(pesoLiquido);
      Serial.println(" g");
    }
    
    rpmDeseado = Serial.parseFloat(); // NOTA: El mínimo RPM válido es 70 RPM
    
    Serial.print("Se van a mezclar a ");
    Serial.print(rpmDeseado);
    Serial.print(" RPM");
    delay(500);

    //delayRpm = 2640 - 10*sqrt(125*rpmDeseado - 10669); // Función transferencia RPM a delay us
    //delayRpm = 2188.36 - 0.363636*sqrt(68750*2*rpmDeseado - 9.4162e6); 
    delayRpm = pow(8.69799685132514e-6*rpmDeseado,-1.0330578512396695);
    Serial.print(" ( delay de ");
    Serial.print(delayRpm);
    Serial.println(" us )");

    Serial.println(" ");
    Serial.println("¡¡¡LISTO PARA MEZCLAR!!!");
    Serial.println(" ");
    Serial.println(" ");
  }
}

// Función de conteo de ticks del encoder
void countPulses() {
  encoderCount++;
}

//Función de Anti-debounce: Evitar el rebote del pulsador
void antiDebounce(byte boton) {
  delay(100);
  while (digitalRead(boton))
    ;  //Anti-debounce
  delay(100);
}

//Función de Calibración de Balanza: Permite calibrar la medida de la balanza según un peso de calibración conocido
void CalibracionBalanza(void)
{
  while (true) {
    Serial.println("~ CALIBRACIÓN DE LA BALANZA ~");
    Serial.println(" ");
    delay(100);
    Serial.println("No ponga ningun objeto sobre la balanza");
    Serial.println(" ");
    balanza.set_scale(); //Ajusta la escala a su valor por defecto que es 1
    balanza.tare(20);  //El peso actual es considerado "Tara".
    delay(50);
    Serial.print("...Destarando...");
    delay(250);

    /* //Calibración de la balanza (comentado, ya que ya se calibró)
    Serial.println(" ");
    Serial.println(" ");
    Serial.print("Coloque un peso de ");
    Serial.print(Peso_conocido);
    Serial.print("g, luego oprima el botón.");
    while (!digitalRead(BtnInput)) {} // esperar a que se oprima el botón
    antiDebounce(BtnInput);
    Serial.println(" Espere...");
    PromMedicion = balanza.get_value(100); //20 mediciones  //Obtiene el promedio de las mediciones analogas según valor ingresado  
    Escala = PromMedicion / Peso_conocido; // Relación entre el promedio de las mediciones analogas con el peso conocido en gramos
    Serial.print("Escala: ");
    Serial.println(Escala);*/

    Serial.println(" ");
    Serial.println("Ponga el vaso:");
    for (int i = 3; i >= 0; i--) {
      Serial.print(" ... ");
      Serial.print(i);
      delay(1000);
    }

    //while (!digitalRead(BtnInput)) {} // esperar a que se oprima el botón
    //antiDebounce(BtnInput);
    Serial.println("\nEspere...");

    float PesoVaso = balanza.get_units(20);
    Serial.print("El vaso tiene un peso de ");
    Serial.print(PesoVaso/Escala);
    Serial.println(" g");
    if ((PesoVaso/Escala - ToleranciaCalibracion) > PesoRealVaso || (PesoVaso/Escala + ToleranciaCalibracion) < PesoRealVaso) {
      Serial.println("La calibración no es correcta, calibrando de nuevo...");
      delay(1000);
      continue;
    } else {
      break;
    }
  }

  balanza.tare(100);
  
  balanza.set_scale(Escala); // Ajusta la escala correspondiente
}

//Función de Medición de Balanza: Permite obtener la medida actual en peso (g) de la balanza
float MedidaBalanza(int n) {
  float peso;
  do { 
    peso = balanza.get_units(n); // Entrega el peso actualmente medido en gramos
  } while (peso > 1000); // if peso > 1000, se mide de nuevo
  return peso;
}

void resetModes(void) {
  for (int i = 0; i < nModes; i++) {
    mode[i] = "";
  }
}

void printModes(void) {
  Serial.print("Modos seleccionados: ");
  for (int i = 0; i < nModes; i++) {
    if (mode[i] != "") {
      Serial.print(mode[i]);
      Serial.print(" ");
    }
  }
  Serial.println();
}

void addMode(String newStr) {
  for (int i = 0; i < nModes; i++) {
    if (mode[i] == "") {
      mode[i] = newStr;
      return;
    }
  }
  Serial.println("ERROR: No hay espacio para más modos");
}

void removeMode(String str) {
  for (int i = 0; i < nModes; i++) {
    if (mode[i] == str) {
      mode[i] = "";
      return;
    }
  }
  Serial.println("ERROR: No se encontró el modo");
}

bool isMode(String m) {
  for (int i = 0; i < nModes; i++) {
    if (m == mode[i]) {
      return true;
    }
  }
  return false;
}
