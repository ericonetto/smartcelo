#include <konkerMQTT.h>

#include "productionModeNC.h"

//pinod5 ou gpio14 reseta FS se estiver em terra



#define LED 16
int pinoCaixa=14;


bool estadoCaixa=false; // LOW=vazia - HIGH =cheia
bool ultimoEstadoCaixa=false;//guarda o último valor do estado da caixa
bool lerEstadoCaixa=true;

char channelCaixa[7] = "caixa";
char channelTemperatura[7] = "data";
char health_channel[]="health";
char status_channel[] = "status";
char in_channel[] = "in";
char config_channel[] = "config";



int intPeriodoEnvio=12000;


int CURRENT_FIRMWARE_VERSION=1;


char *type;
char typeC[32];

int lasttimeCheck=0;

ADC_MODE(ADC_VCC);







///^^^^^^^^^^^^^^^^^^APLICAÇÃO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!111

void checkConnections(){
  if(!connectToWiFiAndMQTTServer()){
    Serial.println("Failed to connect");
    //TODO save in FS the failures
    delay(3000);
    ESP.reset();
  }

  Serial.println("Device connected to WiFi: " + (String)WiFi.SSID());
  Serial.print("IP Address:");
  Serial.println(WiFi.localIP());
}

void checaStatusCaixa(){

        String caixa;
        estadoCaixa = digitalRead(pinoCaixa);
      	if (estadoCaixa == HIGH){

      		//Serial.println("Caixa vazia.");
          caixa="vazia";
      	}else{
      		//Serial.println("Caixa cheia!");
          caixa="cheia";
      	}

        if(estadoCaixa!=ultimoEstadoCaixa || lerEstadoCaixa){
          checkConnections();
          StaticJsonBuffer<220> jsonBuffer;
          JsonObject& jsonMSG = jsonBuffer.createObject();
          ultimoEstadoCaixa=estadoCaixa;
          jsonMSG["deviceId"] = (String)getChipId();
          jsonMSG["Caixa"] = caixa;
          jsonMSG["state"] = ultimoEstadoCaixa ? 1 : 0;

          jsonMSG.printTo(bufferJ, sizeof(bufferJ));
          char mensagemjson[1024];
          strcpy(mensagemjson,bufferJ);
          Serial.println("Publicando no canal:" + (String)channelCaixa);
          Serial.println("A mensagem:");
          Serial.println(mensagemjson);
          if(!PUB(channelCaixa, mensagemjson)){
            writeFile(healthFile,(char*)"1", _mqttFailureAdress);

            delay(3000);
            ESP.restart();
          }else{
            healthUpdate(health_channel);
          }

          lerEstadoCaixa=false;
        }

}




void trataConfig(char msg[]){
    char periodoEnvio[5];

    parse_JSON_item(msg, (char*)"periodo", periodoEnvio);


    if(periodoEnvio[0]!='\0'){
      intPeriodoEnvio=atoi(periodoEnvio);
      Serial.println("strPeriodoEnvio=" + (String)periodoEnvio);
    }

}







//envia mensagem para a plataforma
void statusUpdate(){
	Serial.println("Turning on Wifi");
	if(!connectToWiFiAndMQTTServer()){
    Serial.println("Failed to connect");
    //TODO save in FS the failures
    delay(3000);
		ESP.reset();
  }

  Serial.println("Device connected to WiFi: " + (String)WiFi.SSID());
  Serial.print("IP Address:");
  Serial.println(WiFi.localIP());

	Serial.println("Connected to Konker!");



	Serial.println("Turning off Wifi");
	client.disconnect();
	WiFi.mode(WIFI_OFF);
	delay(100);
}





void setup(){
	Serial.begin(115200);
	Serial.println("Setup");

  Serial.println("BUILD: " + (String)PIO_SRC_REV);

	//uncomment for tests
  //resetALL();
  char content[1];


  konkerSetup((char*)"S0401");


  //statusUpdate();



  pinMode(pinoCaixa, INPUT);

	Serial.println("Setup finished");
	//Serial.println("Turning off Wifi");
	//client.disconnect();
	//WiFi.mode(WIFI_OFF);
	delay(1000);



  lasttimeCheck = millis();

}







// Loop com o programa principal
void loop(){

  client.loop();

  checaStatusCaixa();
  delay(10);


  if ((millis()-lasttimeCheck) > intPeriodoEnvio){





		lasttimeCheck = millis();
	}
}
