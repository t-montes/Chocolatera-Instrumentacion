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

// VARIABLES MODIFICABLES
float Peso_conocido = 220;// en gramos | usado para calibrar la balanza
float Escala = -1308.5; // Escala actual a la que funciona la balanza (hallada al calibrar)

float PesoOffset = 6; // El peso anterior desde el cual se apaga la motobomba, teniendo en cuenta el agua sobrante que puede excederse de la manguera.
float Var = 1024; // Valor por defecto de PWM (a escala de 1024)
int numMedidas = 1; // Numero de medidas que se toman para promediar (balanza)

// Variables agregadas
int First = 1; // Para que la primera vez que se encienda la bomba no se tome el tiempo de caudal
float volumenDeseado; // Volumen deseado a dosificar (se especifica por consola)
float rpmDeseado; // RPM deseado a mezclar (se especifica por consola)
int timeCaudal = 0; // Tiempo transcurrido para calcular el caudal (minibomba)

// Variables balanza
HX711 balanza(DOUT, CLK);

// Variables para mini bomba de agua
int Densidad = 1;// en g/mL
int OnOff = 1; // Switch On/Off para la bomba (1 = On | 0 = Off)
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
String mode = "";


void setup() {
  Serial.begin(9600);

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

  if (mode == "vol") {
    pesoLiquido = MedidaBalanza();
    pesoLiquido = pesoLiquido >= 0 ? pesoLiquido : -pesoLiquido;
    Serial.print("Peso balanza: ");
    Serial.println(pesoLiquido);

    Serial.print("Peso deseado: ");
    Serial.println(volumenDeseado * Densidad);

    // Si la minibomba está encendida, seguir dosificando
    if (OnOff == 1) {
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
      if ((volumenDeseado * Densidad) >= (pesoLiquido + PesoOffset)) {
        Serial.print("Minibombda encendida! : PWM = ");
        Adelante = map(Var, 0, 1023, 0, 255); // Adelante = Var*255/1023 // regla de 3
        Serial.print(100*Adelante/255);
        Serial.println("%");
        analogWrite(Input1, Adelante); 
      }
      // Si el peso deseado es menor al peso líquido menos X gramos, parar
      else if ((volumenDeseado * Densidad) <= (pesoLiquido + PesoOffset)) {
        Serial.print("Minibomba apagando... : CAUDAL = ");
        OnOff = 0;
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
        /*while (!Serial.available()) {
          pesoLiquido = MedidaBalanza();
          Serial.print("Peso actual: ");
          Serial.print(pesoLiquido);
          Serial.println(" g");

          Serial.print("Apagada... : CAUDAL = ");
          Serial.print(1000*(pesoLiquido/Densidad)/(millis() - timeCaudal)); 
            // (ms/s) * (g / (g / mL)) / (ms) = (1/s) * (mL) = mL/s
          Serial.println(" mL/s");
        }*/

        chMode();
    }
    Serial.println();
  } else if (mode == "mix") {
    if (Serial.available() > 1) {
      chMode();
      return;
    }

    if (now - lastTime >= 1000) {
      double rpm = ((double) encoderCount/400)*60;
      Serial.print("RPM: ");
      Serial.println(rpm);
      encoderCount = 0;

      lastTime = now;
    }

    if (now - lastTime2 >= 5000) {
      pesoLiquido = MedidaBalanza();
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

  // Recibir 2 cosas EN ORDEN: Primero un comando 'vol' o 'mix' y luego un parámetro (float) y pasar a la etapa correspondiente
  mode = "";
  while (mode != "vol" && mode != "mix") {
    Serial.println("Digite por consola el modo de operacion\n\t> vol: Dosificar un volumen de liquido\n\t> mix: Mezclar el liquido a ciertos RPM");
    while (!Serial.available()) {
      pesoLiquido = MedidaBalanza();
      Serial.print("Peso actual: ");
      Serial.print(pesoLiquido);
      Serial.println(" g");
    }
    mode = Serial.readStringUntil('\n');
    mode.trim();
    Serial.println(" ");
  }

  if (mode == "vol") {
    OnOff = 1;
    Serial.println("Digite por consola el valor en mL a completar una vez este el peso...");

    while (!Serial.available()) {
      pesoLiquido = MedidaBalanza();
      Serial.print("Peso actual: ");
      Serial.print(pesoLiquido);
      Serial.println(" g");
    }

    volumenDeseado = Serial.parseFloat();
    
    Serial.print("Se van a completar ");
    Serial.print(volumenDeseado);
    Serial.println(" mL");
    delay(500);
    
    Serial.println(" ");
    Serial.println("¡¡¡LISTO PARA DOSIFICAR!!!");
    Serial.println(" ");
    Serial.println(" ");
  } else if (mode == "mix") {
    Serial.println("Digite por consola los RPM a los que desea mezclar...");

    while (!Serial.available()) {
      pesoLiquido = MedidaBalanza();
      Serial.print("Peso actual: ");
      Serial.print(pesoLiquido);
      Serial.println(" g");
    }
    
    rpmDeseado = Serial.parseFloat(); // NOTA: El mínimo RPM válido es 85.352 RPM
    
    Serial.print("Se van a mezclar a ");
    Serial.print(rpmDeseado);
    Serial.print(" RPM");
    delay(500);

    //delayRpm = 2640 - 10*sqrt(125*rpmDeseado - 10669); // Función transferencia RPM a delay us
    delayRpm = 2640 - 10*sqrt(125*rpmDeseado - 10669); // TODO: Función transferencia RPM a delay us
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
    ;  //Anti-Rebote
  delay(100);
}

//Función de Calibración de Balanza: Permite calibrar la medida de la balanza según un peso de calibración conocido
void CalibracionBalanza(void)
{
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
  for (int i = 3; i >= 0; i--)
  {
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

  balanza.tare(100);
  
  balanza.set_scale(Escala); // Ajusta la escala correspondiente
}

//Función de Medición de Balanza: Permite obtener la medida actual en peso (g) de la balanza
float MedidaBalanza(void) {
  float peso; // variable para el peso actualmente medido en gramos
  float pl1 = 0;
  float pl2 = 0;
  float pl3 = 0;
  float promPL = 0;

  for (int i = 1; i >= 0; i--) {
    peso = balanza.get_units(numMedidas); // Entrega el peso actualmente medido en gramos
    // if (peso < 0) peso = peso * -1;
    
    pl1 = peso;
    pl2 = pl1;
    pl3 = pl2;

    promPL = (pl1 + pl2 + pl3) / 3;
  }

  return promPL;
}

