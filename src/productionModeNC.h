#include <konkerMQTT.h>
#include <RestClient.h>
#include <Ticker.h>
#include <Crypto.h>


extern "C" {
  #include "user_interface.h"
}

int _STATUS_LED=2;

#define DEBUG 1

char savedSSID[32]="";
char savedPSK[64]="";
String wifiFile="/wifi.json";
String healthFile="/health.json";

int _netFailureAdress=0;
int _mqttFailureAdress=1;

//WiFiServer httpServer(80);// create object
ESP8266WebServer webServer(80);

void resetALL(){
		WiFi.begin("", "");
		WiFi.disconnect(true);
		strcpy(savedSSID, "");
		strcpy(savedPSK, "");
		formatFileSystem();
		Serial.println("Full reset done! FileSystem formated!");
		Serial.println("You must remove this device from Konker plataform if registred, and redo factory configuration.");

		delay(5000);
		ESP.reset();
		delay(1000);
}




//envia mensagem para a plataforma
void healthUpdate(char *health_channel){

	StaticJsonBuffer<100> jsonBuffer;
	JsonObject& jsonMSG = jsonBuffer.createObject();

	delay(10);

  char content[2];
  readFile(healthFile,content,0,2);
  int nf = content[_netFailureAdress] - '0';
  int mf = content[_mqttFailureAdress] - '0';

	//{"p":0}
  jsonMSG["build"] = (String)PIO_SRC_REV;
  jsonMSG["nfail"] = nf;
  jsonMSG["mfail"] = mf;

  jsonMSG.printTo(bufferJ, sizeof(bufferJ));
	char mensagemjson[1024];
  strcpy(mensagemjson,bufferJ);
	Serial.println("Publishing on channel:" + (String)health_channel);
	Serial.println("The message:");
	Serial.println(mensagemjson);
	PUB(health_channel, mensagemjson);

  if(nf==0 && mf==0){
    return;
  }
  //clear error flags
  saveFile(healthFile,(char*)"00");

}



/*
WL_NO_SHIELD = 255,
WL_IDLE_STATUS = 0,
WL_NO_SSID_AVAIL = 1
WL_SCAN_COMPLETED = 2
WL_CONNECTED = 3
WL_CONNECT_FAILED = 4
WL_CONNECTION_LOST = 5
WL_DISCONNECTED = 6*/
//garantee a disconection before trying to connect
int connectWifi(char *ssid, char *pass) {
	WiFi.mode(WIFI_OFF);
	delay(100);
	WiFi.mode(WIFI_STA);
	delay(100);
	//check if we have ssid and pass and force those, if not, try with last saved values
	Serial.println("WiFi.begin("+(String)ssid+", "+(String)pass+")");
	WiFi.begin(ssid, pass);


	int connRes = WiFi.waitForConnectResult();
	Serial.println("connRes=" + (String)connRes);
	return connRes;
}

int connectWifi(char *ssid, char *pass, int retryies) {
	int connRes=connectWifi(ssid,pass);
	while (connRes!=3 && retryies>1){
		delay(1000);
		connRes=connectWifi(ssid,pass);
		retryies=retryies-1;
	}
	return connRes;
}




bool saveWifiInFile(String wifiFile, char *ssid, char *psk){

  String json="{\"s\":\"" + (String)ssid + "\",\"p\":\"" + (String)psk + "\"}";
	Serial.print("saving json = ");
	Serial.println(json);
  char charJson[112];
  json.toCharArray(charJson, 112);
  return updateJsonFile(wifiFile,charJson);
}


bool getWifiFromFile(String wifiFile, char *ssid, char *psk){
   if(getJsonItemFromFile(wifiFile,(char*)"s",ssid) &&
   getJsonItemFromFile(wifiFile,(char*)"p",psk)){
		 if (ssid[0] == '\0'){
			 return 0;
		 }else{
			 return 1;
		 }
	 }else{
		 return 0;
	 }
}


bool tryConnectClientWifi(){
	char fileSavedSSID[32]={'\0'};
	char fileSavedPSK[64]={'\0'};
	if(!getWifiFromFile(wifiFile,fileSavedSSID,fileSavedPSK) && savedSSID[0]=='\0'){
    return 0;
  }
	Serial.println("fileSavedSSID=" + (String)fileSavedSSID);
	Serial.println("fileSavePSK=" + (String)fileSavedPSK);
  //check if we have ssid and pass and force those, if not, try with last saved values
	int connRes;
	if(strcmp(fileSavedSSID,"")!=0){
		if (fileSavedSSID[0]!='\0'){
			Serial.println("Tring to connect to saved WiFi  (will try 5 times):" +  (String)fileSavedSSID);
			connRes=connectWifi(fileSavedSSID,fileSavedPSK,5);
		}else{
			Serial.println("Tring to connect to WiFi (will try 3 times):" +  (String)savedSSID);
			connRes=connectWifi(savedSSID,savedPSK,3);
		}

	}else{
		if(savedSSID[0]!='\0'){
			Serial.println("Tring to connect to WiFi (will try 3 times):" +  (String)savedSSID);
			connRes=connectWifi(savedSSID,savedPSK,3);
		}else{
			Serial.println("No WiFi saved, ignoring...");
			return 0;
		}
	}

	if(connRes==3){
		Serial.println("Sucess!!");
    digitalWrite(_STATUS_LED, HIGH);
		return 1;
	}else{
		Serial.println("Failed!");
		return 0;
	}
}


//Return 1 if specific signal strength is mesured, 0 if else
bool checkSignal(int powerLimit){
	int32_t rssi =WiFi.RSSI();
	Serial.print("Signal strength: ");
	Serial.print(rssi);
	Serial.println("dBm");
	if(rssi<-powerLimit){
		Serial.println("Config device not near enought!");
		Serial.println("Disconnecting!");
		WiFi.mode(WIFI_OFF);
		delay(100);
		Serial.println("checkSignal=0");
		return 0;
	}else{
		delay(100);
		Serial.println("checkSignal=1");
		return 1;
	}
}


//only exits this function if connected or reached timout. 1 if connection was made, 0 if not
bool checkForFactoryWifi(char *ssidConfig, char *ssidPassConfig, int powerLimit, int connectTimeout){
	int connectionOK=0;
	Serial.println("Searching for " + (String)ssidConfig + ":" + (String)ssidPassConfig);

	//wifi power is weekened (remember to always set wifipower back to maximum 20.5dBm before atempting to connect to a customer WiFi)
	WiFi.setOutputPower(2);


	Serial.println(".");

	boolean keepConnecting = true;
	uint8_t status;
	unsigned long start = millis();
	while (keepConnecting) {
		if(connectWifi(ssidConfig, ssidPassConfig) == 3 && checkSignal(powerLimit)==1) {
			keepConnecting = false;
			Serial.println("Connected to "  + (String)ssidConfig);

			Serial.print("IP Address:");
			Serial.println(WiFi.localIP());
			return 1;
		}
		Serial.print(".");
		if (millis() > start + connectTimeout) {
			Serial.println("");
			Serial.println("Factory connection window timed out");
			keepConnecting = false;
			return 0;
		}
		delay(100);
	}
	WiFi.setOutputPower(20.5);// return wifi power to maximum
	return 0;
}

//Expected format->   {"srv":"mqtt.demo.konkerlabs.net","prt":"1883","usr":"jnu55qtsbbii","pwd":"3S7rnsR9gDqK", ,"prx":"data"}
bool getPlataformCredentials(char *configFilePath){
	//////////////////////////
	///step1 open file
	char fileContens[1024];
	if(!readFile(configFilePath,fileContens)){
		return 0;//if fail to open file
	}

	//////////////////////////
	///step2 config file opened
	Serial.println("Parsing: " + (String)fileContens);
	DynamicJsonBuffer jsonBuffer;
	JsonObject& configJson = jsonBuffer.parseObject(fileContens);
	if (configJson.success()) {
		Serial.println("Json file parsed!");
	}else{
		Serial.println("Failed to read Json file");
		return 0;
	}


	//////////////////////////
	///step3 copy values
	Serial.println("Config file exists, reading...");

	char bufferConfigJson[1024]="";
	configJson.printTo(bufferConfigJson, 1024);
	Serial.println("Content: "+ String(bufferConfigJson));
	if (configJson.containsKey("srv") && configJson.containsKey("prt") &&
	configJson.containsKey("usr") && configJson.containsKey("pwd") && configJson.containsKey("prx")){


		//global variables: server, port, device_login, device_pass
		//variables delcared in main.h from LibKonkerESP8266
		//char server[64];
		//int port;
		//char device_login[32];
		//char device_pass[32];
		//char prefix[32];
		strcpy(server, configJson["srv"]);
		port=atoi(configJson["prt"]);
		strcpy(device_login, configJson["usr"]);
		strcpy(device_pass, configJson["pwd"]);
		strcpy(prefix, configJson["prx"]);
	}else{
		Serial.println("Unexpected json format");
		return 0;
	}

	//////////////////////////
	///step4 check for empty values
	if(strcmp(server, "") == 0){
		Serial.println("Unexpected json format, srv is empty");
		return 0;
	}
	if(port== 0){
		Serial.println("Unexpected json format, port is empty");
		return 0;
	}
	if(strcmp(device_login, "") == 0){
		Serial.println("Unexpected json format, usr is empty");
		return 0;
	}
	if(strcmp(device_pass, "") == 0){
		Serial.println("Unexpected json format, pwd is empty");
		return 0;
	}
	if(strcmp(prefix, "") == 0){
		Serial.println("Unexpected json format, prx is empty");
		return 0;
	}

	//if reached this part then return 1 meaning it is OK
	return 1;
}

bool apConnected=0;
void WiFiEvent(WiFiEvent_t event) {
	switch(event){
	case WIFI_EVENT_SOFTAPMODE_STACONNECTED:
		Serial.println("Conectado");
		apConnected=1;
	break;
	case WIFI_EVENT_SOFTAPMODE_STADISCONNECTED:
		Serial.println("Desconectou");
		apConnected=0;
	break;
	}
}

String macToString(const unsigned char* mac) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

WiFiEventHandler stationConnectedHandler;
WiFiEventHandler stationDisconnectedHandler;
void onStationConnected(const WiFiEventSoftAPModeStationConnected& evt) {
  Serial.print("Station connected: ");
  Serial.println(macToString(evt.mac));
}

void onStationDisconnected(const WiFiEventSoftAPModeStationDisconnected& evt) {
  Serial.print("Station disconnected: ");
  Serial.println(macToString(evt.mac));
}


void setupWiFi(char *apName){


	WiFi.mode(WIFI_AP);

  // Append the last two bytes of the MAC (HEX'd) to string to make unique
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  WiFi.onEvent(WiFiEvent,WIFI_EVENT_ANY);
	const IPAddress gateway_IP(192, 168, 4, 1); // gateway IP
	const IPAddress subnet_IP( 255, 255, 255, 0); // standard subnet
	WiFi.config(gateway_IP, gateway_IP, subnet_IP); // we use a static IP address
  WiFi.softAP(apName);




}

//Usefull variables to Decrypt Password
//--------------------------------
uint8_t converted_iv[16];
uint8_t converted_key[16];
uint8_t converted_hex[64];
int plength=0;
//--------------------------------

//Usefull functions to Decrypt the Password
//--------------------------------
uint8_t *convert_hex(char str[])
{
  char substr[5]="0x00";
  int j = 0;
  int psize = (strlen(str));
  plength = int(psize/2);
  for (byte i = 0; i < psize; i=i+2) {
    substr[2] = str[i];
    substr[3] = str[i+1];
    converted_hex[j] = strtol(substr, NULL, plength);
    j++;
  }
  return converted_hex;
}
//--------------------------------
uint8_t *convert_key(char str[])
{
  int j=0;
  for (int i = 0; i < 16; i++ ) {
    if (str[j]=='\0') j=0;
    converted_key[i]=int(str[j]);
    j++;
  }
  return converted_key;
}
//--------------------------------
uint8_t *convert_iv(char str[])
{
  for (int i = 0; i < 16; i++ ) {
    converted_iv[i]=int(str[i]);
  }
  return converted_iv;
}



bool gotCredentials=0;
void getWifiCredentialsCRI(){
	Serial.println("Handle getWifiCredentials");
	String page = "<http><body><b>getWifiCredentials</b></body></http>";


	String argSSID = webServer.arg("s");
	String argPSK = webServer.arg("p");
	Serial.println("argSSID=" + argSSID);
	Serial.println("argPSK=" + argPSK);
	if(argSSID!="" && argPSK!=""){
		argSSID.toCharArray(savedSSID, 32);
		//argPSK.toCharArray(savedPSK, 64);
		gotCredentials=1;
    //Decrypt Password
    char pass[argPSK.length()+1];
    argPSK.toCharArray(pass, argPSK.length()+1);
    char mySSID[argSSID.length()+1];
    argSSID.toCharArray(mySSID, argPSK.length()+1);
    char iv1[17] = "AnE9cKLPxGwyPPVU";
    char iv2[17] = "sK33DE5TaC9nRUSt";
    uint8_t var3[64];
    uint8_t var4[64];
		uint8_t *convKeyLogin=convert_key(device_login);
		uint8_t *convKeySSID=convert_key(mySSID);

		Serial.print("convert_key(device_login): " + String(device_login) + " ");
		for (int i = 0; i < 16; i++) {
				Serial.print(convKeyLogin[i]);
		}
		Serial.println("");

		Serial.print("convert_key(mySSID): "+ String(mySSID)+ " ");
		for (int i = 0; i < 16; i++) {
				Serial.print(convKeySSID[i]);
		}
		Serial.println("");

    AES deck1(convert_key(device_login), convert_iv(iv2), AES::AES_MODE_128, AES::CIPHER_DECRYPT);
    AES deck2(convert_key(mySSID), convert_iv(iv1), AES::AES_MODE_128, AES::CIPHER_DECRYPT);
    deck1.process(convert_hex(pass), var3, plength);
    deck2.process(var3, var4, plength);
    for (int i = 0; i < plength; i++) {
        savedPSK[i]=var4[i];
    }
    for (int i = 1; i <= plength; i++) {
        if (savedPSK[plength-i]==' ' || savedPSK[plength-i] == '\0' ) savedPSK[plength-i]='\0';
        else {
					Serial.printf("char @ position %d = %c", i, savedPSK[plength-i]);
					Serial.printf("psk='%s'", savedPSK);
					break;
				}
    }

		// reset wifi credentials from file
		//Removing the Wifi Configuration
		SPIFFS.remove(wifiFile);


	}
	webServer.send(200, "text/html", page);



}


void getWifiCredentialsNC(){
	Serial.println("Handle getWifiCredentials");
	String page = "<http><body><b>getWifiCredentials</b></body></http>";


	String argSSID = webServer.arg("s");
	String argPSK = webServer.arg("p");
	Serial.println("argSSID=" + argSSID);
	Serial.println("argPSK=" + argPSK);
	if(argSSID!="" && argPSK!=""){
		argSSID.toCharArray(savedSSID, 32);
		argPSK.toCharArray(savedPSK, 64);
		gotCredentials=1;


		// reset wifi credentials from file
		//Removing the Wifi Configuration
		SPIFFS.remove(wifiFile);

	}
	webServer.send(200, "text/html", page);
}



bool startAPForWifiCredentials(char *apName, int timoutMilis){
	Serial.println("Starting AP " + (String)apName);
	setupWiFi(apName);
	delay(200);


	//Wait for connection with timout
	int counter=0;
	while (!apConnected && (counter*500)<timoutMilis) {
		delay(500);
		counter=counter+1;
	}
	if((counter*500)>=timoutMilis){
		return 0;
	}



	webServer.on("/wifisave", getWifiCredentialsNC);
	webServer.begin();

	Serial.println("Client connected");


	Serial.println("local ip");
	Serial.println(WiFi.localIP());

	while(!gotCredentials){
		delay(500);
		webServer.handleClient();
	}
	gotCredentials=0;

	webServer.stop();

	WiFi.disconnect(true);
	delay(1000);
	(void)wifi_station_dhcpc_start();

	return 1;
}



bool startAPForWifiCredentialsNC(char *apName, int timoutMilis){
	Serial.println("Starting AP " + (String)apName);
	setupWiFi(apName);
	delay(200);


	//Wait for connection with timout
	int counter=0;
	while (!apConnected && (counter*500)<timoutMilis) {
		delay(500);
		counter=counter+1;
	}
	if((counter*500)>=timoutMilis){
		return 0;
	}



	webServer.on("/wifisave", getWifiCredentialsNC);
	webServer.begin();

	Serial.println("Client connected");


	Serial.println("local ip");
	Serial.println(WiFi.localIP());

	while(!gotCredentials){
		delay(500);
		webServer.handleClient();
	}
	gotCredentials=0;

	webServer.stop();

	WiFi.disconnect(true);
	delay(1000);
	(void)wifi_station_dhcpc_start();

	return 1;
}




///////////////////////////////////

bool get_platform_credentials(){
  //Creating the variables we will use: "response" to keep the Server response and "address" to keep the address used to access the server
  String response = "";


  String address = "/" + String(getChipId());
	Serial.println("address=" + address);
  char addressC[20];
  address.toCharArray(addressC,20);

  String gateway_IP = WiFi.gatewayIP().toString();
  char gateway_IP_C[16];
  gateway_IP.toCharArray(gateway_IP_C,16);

  //The IP of our client is the Gateway IP
  RestClient Localclient = RestClient(gateway_IP_C,8081);

  //Lets do a Rest GET on the server IP and the address generated from the ESP ID
	Serial.println("addressC=" + String(addressC));
  int statusCode = Localclient.get(addressC, &response);
	Serial.
	println("statusCode=" + String(statusCode));
	Serial.println("response=" + String(response));
  if (statusCode == 200){
    File configFile = SPIFFS.open("/crd.json", "w");
    if (!configFile) {
      if (DEBUG) Serial.println("Could not open the file with write permition!");
      return 0;
    }
    configFile.print(response);
    configFile.close();

    //Removing the Wifi Configuration
    SPIFFS.remove(wifiFile);
    //save hinitial ealth state flags
    saveFile(healthFile,(char*)"00");

    //wifiManager.resetSettings();

    if (DEBUG){
      Serial.print("Status code from server :");
      Serial.println(statusCode);
      Serial.print("Response body from server: ");
      Serial.println(response);
    }
    return 1;
  }
  else {
    return 0;
  }
}


bool connectToWiFiAndMQTTServer(){
  if(WiFi.status()!=WL_CONNECTED){
    if(!tryConnectClientWifi()){
      Serial.println("Failed to connect to WiFi");
      writeFile(healthFile,(char*)"1", _netFailureAdress);
      return 0;
    }
  }


	Serial.println("Connecting to MQTT..");
	if(!connectMQTT()){
		Serial.println("Failed!");
    writeFile(healthFile,(char*)"1", _mqttFailureAdress);
		return 0;
	}

	return 1;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




void konkerSetup(char productPefix[6]){
	pinMode(D5, OUTPUT);
	digitalWrite(D5, HIGH);


	setName(productPefix);
	pinMode(_STATUS_LED, OUTPUT);
	digitalWrite(_STATUS_LED, LOW);
	//cria file system se não existir
	spiffsMount();

	//uncomment this line below for tests
	//resetALL();
	if (digitalRead(D5) == LOW){//reset and format FS all if D5 is low
		Serial.println("D5 pin in LOW state. Formating FS.. (WAIT FOR REBOOT)");
		resetALL();
	}

  //tenta se conectar ao wifi configurado
  //caso já exista o arquivo de wifi ele vai tentar se conectar
  if(tryConnectClientWifi()){
    getPlataformCredentials((char*)"/crd.json");
    return;
  }

	int arquivoWifiPreConfigurado=0;

	if(strcmp(WiFi.SSID().c_str(),(char*)"KonkerDevNetwork")!=0){
		if(strcmp(WiFi.SSID().c_str(),"")!=0){
			Serial.println("Saving wifi memory");
			strcpy(savedSSID, WiFi.SSID().c_str());
			strcpy(savedPSK, WiFi.psk().c_str());
			Serial.printf("saving wifi '%s' password '%s'\n", savedSSID, savedPSK);
			saveWifiInFile(wifiFile,savedSSID,savedPSK);
		}
	}else{
		Serial.println("Wifi memory has KonkerDevNetwork, ignoring..");
	}


  arquivoWifiPreConfigurado=getPlataformCredentials((char*)"/crd.json");//se for outro nome é só mudar aqui

	while (!arquivoWifiPreConfigurado){
		//only exits this function if connected or reached timout, only connect if specific signal strength is mesured (47dBm)
		checkForFactoryWifi((char*)"KonkerDevNetwork",(char*)"konkerkonker123",47,10000);
		//se conectar no FactoryWifi
		//baixar arquivo pré wifi

		get_platform_credentials();
		//retorno da função do Luis 1 -> recebeu e guardou o arquivo wifipré. 0->deu algum problema e não tem wifipré
		//se tiver arquivo pré wifi
		//configura pré wifi
		//Formato esperado: {"srv":"mqtt.demo.konkerlabs.net","prt":"1883","usr":"jnu56qt1bb1i","pwd":"3S7usR9g5K","prx":"data"}
		arquivoWifiPreConfigurado=getPlataformCredentials((char*)"/crd.json");//se for outro nome é só mudar aqui

	}

  //desliga led indicando que passou pela configuração de fábrica
  digitalWrite(_STATUS_LED, HIGH);



	//esta parte só chega se já passou pelo modo fábrica acima
	//lembrando
	//global variables: server, port, device_login, device_pass
	//variables delcared in main.h from LibKonkerESP8266
	//char server[64];
	//int port;
	//char device_login[32];
	//char device_pass[32];

	//Tem arquivo wifi? Se tem configura o wifi
	//se não tem entra em modo AP


	//aqui
	if(tryConnectClientWifi()){
    return;
  }

	//MODO AP (nome do device, sem senha)

	//se conectar, cria o HTTP server
	//recebe a configuração WiFi via POST enviado pelo app do celular

	//tenta conctar no wifi passado pelo app
	//se falhar ou passar do timout,reboota
	if(startAPForWifiCredentials(getChipId(),120000)){
		Serial.println("Credentials received, trying to connect to production WiFi");
		if(!tryConnectClientWifi()){
			Serial.println("Failed! Rebooting...");
			delay(3000);
			ESP.reset();
		}


		Serial.println("WiFi configuration done!");
		Serial.println("Saving wifi memory");
		strcpy(savedSSID, WiFi.SSID().c_str());
		strcpy(savedPSK, WiFi.psk().c_str());
		saveWifiInFile(wifiFile,savedSSID,savedPSK);
		delay(1000);
		Serial.println("Rebooting...");
		ESP.reset();
	}else{
		Serial.println("Timout! Rebooting...");
		delay(3000);
		ESP.reset();
	}


}






void konkerSetupNC(char productPefix[6]){
	pinMode(D5, OUTPUT);
	digitalWrite(D5, HIGH);


	setName(productPefix);
	pinMode(_STATUS_LED, OUTPUT);
	digitalWrite(_STATUS_LED, LOW);
	//cria file system se não existir
	spiffsMount();

	//uncomment this line below for tests
	//resetALL();
	if (digitalRead(D5) == LOW){//reset and format FS all if D5 is low
		Serial.println("D5 pin in LOW state. Formating FS.. (WAIT FOR REBOOT)");
		resetALL();
	}

  //tenta se conectar ao wifi configurado
  //caso já exista o arquivo de wifi ele vai tentar se conectar
  if(tryConnectClientWifi()){
    getPlataformCredentials((char*)"/crd.json");
    return;
  }

	int arquivoWifiPreConfigurado=0;

	if(strcmp(WiFi.SSID().c_str(),(char*)"KonkerDevNetwork")!=0){
		if(strcmp(WiFi.SSID().c_str(),"")!=0){
			Serial.println("Saving wifi memory");
			strcpy(savedSSID, WiFi.SSID().c_str());
			strcpy(savedPSK, WiFi.psk().c_str());
			Serial.printf("saving wifi '%s' password '%s'\n", savedSSID, savedPSK);
			saveWifiInFile(wifiFile,savedSSID,savedPSK);
		}
	}else{
		Serial.println("Wifi memory has KonkerDevNetwork, ignoring..");
	}


  arquivoWifiPreConfigurado=getPlataformCredentials((char*)"/crd.json");//se for outro nome é só mudar aqui

	while (!arquivoWifiPreConfigurado){
		//only exits this function if connected or reached timout, only connect if specific signal strength is mesured (47dBm)
		checkForFactoryWifi((char*)"KonkerDevNetwork",(char*)"konkerkonker123",47,10000);
		//se conectar no FactoryWifi
		//baixar arquivo pré wifi

		get_platform_credentials();
		//retorno da função do Luis 1 -> recebeu e guardou o arquivo wifipré. 0->deu algum problema e não tem wifipré
		//se tiver arquivo pré wifi
		//configura pré wifi
		//Formato esperado: {"srv":"mqtt.demo.konkerlabs.net","prt":"1883","usr":"jnu56qt1bb1i","pwd":"3S7usR9g5K","prx":"data"}
		arquivoWifiPreConfigurado=getPlataformCredentials((char*)"/crd.json");//se for outro nome é só mudar aqui

	}

  //desliga led indicando que passou pela configuração de fábrica
  digitalWrite(_STATUS_LED, HIGH);



	//esta parte só chega se já passou pelo modo fábrica acima
	//lembrando
	//global variables: server, port, device_login, device_pass
	//variables delcared in main.h from LibKonkerESP8266
	//char server[64];
	//int port;
	//char device_login[32];
	//char device_pass[32];

	//Tem arquivo wifi? Se tem configura o wifi
	//se não tem entra em modo AP


	//aqui
	if(tryConnectClientWifi()){
    return;
  }

	//MODO AP (nome do device, sem senha)

	//se conectar, cria o HTTP server
	//recebe a configuração WiFi via POST enviado pelo app do celular

	//tenta conctar no wifi passado pelo app
	//se falhar ou passar do timout,reboota
	if(startAPForWifiCredentialsNC(getChipId(),120000)){
		Serial.println("Credentials received, trying to connect to production WiFi");
		if(!tryConnectClientWifi()){
			Serial.println("Failed! Rebooting...");
			delay(3000);
			ESP.reset();
		}


		Serial.println("WiFi configuration done!");
		Serial.println("Saving wifi memory");
		strcpy(savedSSID, WiFi.SSID().c_str());
		strcpy(savedPSK, WiFi.psk().c_str());
		saveWifiInFile(wifiFile,savedSSID,savedPSK);
		delay(1000);
		Serial.println("Rebooting...");
		ESP.reset();
	}else{
		Serial.println("Timout! Rebooting...");
		delay(3000);
		ESP.reset();
	}


}
