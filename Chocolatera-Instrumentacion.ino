/*
   Instrumentación Electrónica _ 2022-02
   Laboratorio N°1
   Profesor: Johann F. Osma
   Asistente: Santiago Tovar Perilla
   Autor: Juliana Noguera Contreras
*/

//Librerias
#include "HX711.h" //Modulo HX711 - Celda de carga

//Variables para la balanza
#define DOUT  A1
#define CLK  A0

// VARIABLES MODIFICABLES
float Peso_conocido = 220;// en gramos | MODIFIQUE PREVIAMENTE CON EL VALOR DE PESO CONOCIDO!!!
float Escala = -1308.5; // Escala actual a la que funciona la balanza

float PesoOffset = 6; // El peso anterior desde el cual se apaga la motobomba, teniendo en cuenta el agua sobrante que puede excederse de la manguera.
float Var = 1024; // Valor por defecto de PWM (a escala de 100%)
int numMedidas = 1;

//Variables agregadas
// int delayWait = 5000;
// int BtnInput = 7;
int First = 1;
float pesoBase; // no usado, por ahora
float volumenDeseado;
int timeCaudal = 0;

//Variables para la mini bomba de agua
int Input1 = 9;    // Control pin 1 for motor 1 - para controlar 'Adelante'
int Input2 = 10;     // Control pin 2 for motor 1 - para controlar 'Atras'

//Variables para calibracion de balanza

HX711 balanza(DOUT, CLK);

//Variables para mini bomba de agua
int Densidad = 1;// en g/mL | MODIFIQUE PREVIAMENTE CON EL VALOR DE DENSIDAD CORRESPONDIENTE!!!
// float volumenDeseado = 250;// en mL | MODIFIQUE PREVIAMENTE CON EL VALOR DE VOLUMEN DESEADO!!!

int OnOff = 1; // Switch On/Off para la bomba (1 = On | 0 = Off)
float Adelante; // Variable PWM 1
float Atras; // Variable PWM 2 - NO USADA, PORQUE LA MINIBOMBDA SOLO PUEDE ENTREGAR AGUA
float Detener = map(0, 0, 1023, 0, 255); // 0 
float pesoRecipiente = 0;

void setup() {
  Serial.begin(9600);

  // Inicializamos los pines
  pinMode( Input1, OUTPUT);
  pinMode( Input2, OUTPUT);
  //pinMode( BtnInput, INPUT );

  // Balanza
  CalibracionBalanza();
  Serial.println(" ");
  Serial.println("¡¡¡LISTO PARA PESAR!!!");
  Serial.println(" ");
  Serial.println("Digite por consola el valor en mL a completar una vez esté el peso...");

  float pesoLiquido = 0;
  while (!Serial.available()) {
    pesoLiquido = MedidaBalanza();
    Serial.print("Peso actual: ");
    Serial.print(pesoLiquido);
    Serial.println(" g");
  }

  pesoBase = pesoLiquido;
  volumenDeseado = Serial.parseFloat();

  Serial.print("Se van a completar ");
  Serial.print(volumenDeseado);
  Serial.println(" mL");
  delay(500);

  Serial.println(" ");
  Serial.println("¡¡¡LISTO PARA DOSIFICAR!!!");
  Serial.println(" ");
  Serial.println(" ");
}


void loop() {
  // Mide el peso del líquido
  Serial.println("Midiendo...");
  float pesoLiquido = MedidaBalanza();
  pesoLiquido = pesoLiquido >= 0 ? pesoLiquido : -pesoLiquido;

  Serial.print("Peso deseado: ");
  Serial.println(volumenDeseado * Densidad);
  Serial.print("Peso obtenido: ");
  Serial.println(pesoLiquido);
  Serial.print("Minibomba: ");

  if (OnOff == 1) {
    First = 0;
      if (First == 1) { 
        timeCaudal = millis(); 
        return;
      }
    // Si el peso deseado es mayor al peso líquido menos X gramos, seguir dosificando
    if ((volumenDeseado * Densidad) >= (pesoLiquido + PesoOffset)) {

      Serial.print("Encendida! : PWM = ");
      Adelante = map(Var, 0, 1023, 0, 255); // Adelante = Var*255/1023 // regla de 3
      Serial.print(100*Adelante/255);
      Serial.println("%");
      analogWrite(Input1, Adelante);
    }
    // Si el peso deseado es menor al peso líquido menos X gramos, parar
    else if ((volumenDeseado * Densidad) <= (pesoLiquido + PesoOffset)) {
      Serial.print("Apagando... : CAUDAL = ");
      OnOff = 0;
      analogWrite(Input1, Detener);
      Serial.print(1000*(pesoLiquido/Densidad)/(millis() - timeCaudal)); 
        // (ms/s) * (g / (g / mL)) / (ms) = (1/s) * (mL) = mL/s
      Serial.println(" mL/s");
      
      Serial.print("Tiempo: ");
      Serial.print(1000/(millis() - timeCaudal));
      Serial.println(" s");
    }
    // TODO:
    else {
      Serial.println("????");
    }
  }
  else {
      while (!Serial.available()) {
        pesoLiquido = MedidaBalanza();
        
        Serial.print("Peso actual: ");
        Serial.print(pesoLiquido);
        Serial.println(" g");

        Serial.print("Apagada... : CAUDAL = ");
        Serial.print(1000*(pesoLiquido/Densidad)/(millis() - timeCaudal)); 
          // (ms/s) * (g / (g / mL)) / (ms) = (1/s) * (mL) = mL/s
        Serial.println(" mL/s");
      }
      pesoBase = pesoLiquido;
      volumenDeseado = Serial.parseFloat();
    
      Serial.print("Se van a completar ");
      Serial.print(volumenDeseado);
      Serial.println(" mL");
      delay(500);
      OnOff = 1;
    
      Serial.println(" ");
      Serial.println("¡¡¡LISTO PARA DOSIFICAR!!!");
      Serial.println(" ");
      Serial.println(" ");
  }

  /*if (digitalRead(BtnInput)) {
    antiDebounce(BtnInput);
    OnOff = 1;
    First = 1;
    Serial.println("Tarando el vaso");
    for (int i = 3; i >= 0; i--)
    {
      Serial.print(" ... ");
      Serial.print(i);
      balanza.tare(25);  //El peso actual es considerado "Tara".
    }
    Serial.println(" ");
    delay(200);
  }*/

  Serial.println(" ");
  //Serial.begin(9600);
}

// FUNCIONES

//Función de Anti-debounce (Evitar el rebote del pulsador)
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

  /*Serial.println(" ");
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

  /*for (int i = 3; i >= 0; i--)
  {
    Serial.print(" ... ");
    Serial.print(i);
    balanza.tare(25);  //El peso actual es considerado "Tara".
  }*/
  balanza.tare(100);
  
  balanza.set_scale(Escala); // Ajusta la escala correspondiente
}

//Función de Medición de Balanza: Permite obtener la medida actual en peso (g) de la balanza
float MedidaBalanza(void)
{
  float peso; // variable para el peso actualmente medido en gramos
  float pl1 = 0;
  float pl2 = 0;
  float pl3 = 0;
  float promPL = 0;

  for (int i = 1; i >= 0; i--)
  {
    peso = balanza.get_units(numMedidas); // Entrega el peso actualmente medido en gramos
    // if (peso < 0) peso = peso * -1;
    
    pl1 = peso;
    pl2 = pl1;
    pl3 = pl2;

    promPL = (pl1 + pl2 + pl3) / 3;
  }

  //Serial.print("Peso: ");
  //Serial.print(promPL, 2);
  //Serial.println(" g");
  //delay(25);

  return promPL;

}

//Función de DosificarB: Permite entregar un volumen deseado(mL) según la medida actual de peso (g) de la balanza
/*
Funcionamiento:
- 
*/
void DosificarB(float pesoRecipiente)
{  
  float pesoLiquido = MedidaBalanza() - pesoRecipiente;// Entrega el peso del liquido actualmente medido en gramos

  Serial.print("\tPeso recipiente: ");
  Serial.println(pesoRecipiente);
  Serial.print("\tPeso Liquido: ");
  Serial.println(pesoLiquido);

  while ((volumenDeseado / Densidad) + 0.5 >= pesoLiquido)
  {
    Adelante = map(Var, 0, 1023, 0, 255); // Adelante = Var*255/1023 // regla de 3
    analogWrite(Input1, Adelante);
        
    pesoLiquido = MedidaBalanza() - pesoRecipiente;
  }


  if ((volumenDeseado / Densidad) + 0.1 <= pesoLiquido)
  {
    OnOff = 0;
    analogWrite(Input1, Detener);
  }
}
