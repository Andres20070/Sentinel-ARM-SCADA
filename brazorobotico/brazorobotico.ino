#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h> 
#include <PubSubClient.h> 
#include <Adafruit_INA219.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_TCS34725.h>

#define XSHUT_PIN 2 
Adafruit_INA219 ina219(0x45);
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_2_4MS, TCS34725_GAIN_1X);

// --- SENSOR DATA BUFFER ---
float gripperCurrent_mA = 0.0;
uint16_t dropZoneDistance_mm = 8190;
uint16_t colorR = 0, colorG = 0, colorB = 0, colorC = 0;

unsigned long ultimoTiempoSensores = 0;
const unsigned long INTERVALO_SENSORES = 100;
unsigned long ultimoTiempoFisica = 0;
const unsigned long INTERVALO_FISICA = 20; // 50Hz estricto para los servos

// --- SYSTEM CONSTANTS ---
#define FRECUENCIA_PWM 50
#define MAX_PASOS_MEMORIA 30
#define EMA_ALPHA 0.08 
#define KINEMATIC_DEADBAND 2.0 

const char* mqtt_broker_ip = "IpDelServidorWeb";
const int mqtt_port = 1883;
WiFiClient espClient; 
PubSubClient mqttClient(espClient);

// --- GLOBAL OBJECTS ---
Adafruit_PWMServoDriver pca = Adafruit_PWMServoDriver();
AsyncWebServer server(80);
Preferences nvs;

// --- BIONIC DATA STRUCTURE ---
struct Eje {
    char nombre[15];
    int canal;          
    float actual;       
    float objetivo;
    int minLimit;       
    int maxLimit;       
};

Eje robot[6] = {
    {"Base",     4, 1500.0, 1500.0, 500, 2500},
    {"Hombro",   0, 1500.0, 1500.0, 500, 2500},
    {"Codo",     8, 1500.0, 1500.0, 500, 2500},
    {"Muñeca V", 11, 1500.0, 1500.0, 500, 2500},
    {"Muñeca Giro", 7, 1500.0, 1500.0, 500, 2500},
    {"Pinza",   3, 1500.0, 1500.0, 250, 2000}
};

struct Pose {
    float angulos[6];     
    char etiqueta[20];
};

// --- STATE VARIABLES ---
bool enMovimiento = false;      
bool modoAutomatico = false;    
Pose memoria[MAX_PASOS_MEMORIA];
int totalPasos = 0;
int pasoActual = 0;
unsigned long tiempoEsperaAuto = 0;
unsigned long lastMqttAttempt = 0;

void adquirirDatosSensores() {
    if (millis() - ultimoTiempoSensores >= INTERVALO_SENSORES) {

        gripperCurrent_mA = ina219.getCurrent_mA();
        
        VL53L0X_RangingMeasurementData_t measure;
        lox.rangingTest(&measure, false); 
        if (measure.RangeStatus != 4) {  
            dropZoneDistance_mm = measure.RangeMilliMeter;
        } else {
            dropZoneDistance_mm = 8190;
        }

        tcs.getRawData(&colorR, &colorG, &colorB, &colorC);
        ultimoTiempoSensores = millis();
    }
}

// --- HTML (WEB INTERFACE) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Sentinel Bionic</title>
  <style>
    body { background-color: #121212; color: #00ff88; font-family: monospace; padding: 20px; text-align: center; }
    .control-box { border: 1px solid #333; background: #1e1e1e; margin: 10px auto; padding: 15px; max-width: 400px; border-radius: 8px; }
    .label-line { display: flex; justify-content: space-between; font-size: 1.2rem; margin-bottom: 5px; }
    input[type=range] { width: 100%; cursor: pointer; }
    button { background: #333; color: #fff; border: 1px solid #00ff88; padding: 10px 20px; margin: 5px; cursor: pointer; font-family: monospace; }
    button:hover { background: #00ff88; color: #000; }
    input[type=text] { width: 80%; padding: 10px; margin: 10px 0; background: #333; color: white; border: 1px solid #00ff88; text-align: center; }
    #status-text { margin-top: 20px; color: #888; }
  </style>
</head>
<body>
  <h2>SENTINEL <span style="color:#fff">BIONIC</span></h2>
  
  <div id="axes-container">Cargando interfaz...</div>

  <div style="margin-top: 30px; border-top: 1px solid #333; padding-top: 20px;">
    <input type="text" id="poseName" placeholder="Nombre de la Pose (ej: Agarre)">
    <br>
    <button onclick="guardarConNombre()">MEMORIZAR POSE</button>
    <button onclick="cmd('/runAuto')">EJECUTAR SECUENCIA</button>
    <button onclick="cmd('/stopAuto')">DETENER</button>
    <button onclick="cmd('/clearMem')">BORRAR MEMORIA</button>
  </div>
  
  <div id="status-text">Sistema Listo.</div>

<script>
  const axes = ["Base", "Hombro", "Codo", "Muñeca V", "Muñeca G", "Pinza"];
  function guardarConNombre() {
    let nombre = document.getElementById('poseName').value;
    if(!nombre) nombre = "Paso_" + (Date.now()); 
    cmd('/savePose?name=' + encodeURIComponent(nombre));
  }

  function init() {
    let html = '';
    axes.forEach((ax, i) => {
      html += `
        <div class="control-box">
            <div class="label-line">
              <span>${ax}</span>
              <span id="val${i}" style="color: #ffc107">1500</span>
            </div>
            <input type="range" id="rng${i}" min="500" max="2500" value="1500" 
                   oninput="document.getElementById('val${i}').innerText=this.value" 
                   onchange="sendMove(${i}, this.value)">
        </div>`;
    });
    document.getElementById('axes-container').innerHTML = html;
    setInterval(syncStatus, 1000);
  }
  
  function sendMove(id, val) { 
    fetch(`/set?id=${id}&val=${val}`).catch(e => console.error(e));
  }
  
  function cmd(endpoint) { 
    fetch(endpoint)
      .then(r => r.text())
      .then(t => document.getElementById('status-text').innerText = t);
  }
  
  function syncStatus() {
    fetch('/status')
      .then(r => r.json())
      .then(data => {
        if(data.auto || data.moving) {
            data.pos.forEach((p, i) => {
                let slider = document.getElementById('rng'+i);
                if(slider) {
                    slider.value = Math.round(p);
                    document.getElementById('val'+i).innerText = Math.round(p);
                }
            });
            document.getElementById('status-text').innerText = data.currentAction ? "Ejecutando: " + data.currentAction : "Interpolando...";
        }
    }).catch(e => {});
  }
  window.onload = init;
</script>
</body>
</html>
)rawliteral";

// --- NETWORK & STORAGE SYSTEM ---
void maintainMqttConnection() {
    if (!mqttClient.connected()) {
        // Intento de reconexión NO BLOQUEANTE cada 5 segundos
        if (millis() - lastMqttAttempt > 5000) {
            lastMqttAttempt = millis();
            Serial.print("[System] Attempting MQTT connection... ");
            if (mqttClient.connect("Sentinel_Arm_01")) {
                Serial.println("CONNECTED.");
            } else {
                Serial.println("FAILED. Running in Local Mode.");
            }
        }
    } else {
        mqttClient.loop();
    }
}

void cargarCalibracion() {
    nvs.begin("calib", true); 
    for(int i=0; i<6; i++) {
        char kMin[8], kMax[8];
        sprintf(kMin, "min%d", i);
        sprintf(kMax, "max%d", i);
        robot[i].minLimit = nvs.getInt(kMin, robot[i].minLimit);  
        robot[i].maxLimit = nvs.getInt(kMax, robot[i].maxLimit); 
    }
    nvs.end();
}

void guardarLimite(int id, String tipo) {
    nvs.begin("calib", false); 
    char key[8];
    sprintf(key, "%s%d", tipo.c_str(), id);
    
    int valorActual = (int)robot[id].actual;
    nvs.putInt(key, valorActual);
    if(tipo == "min") robot[id].minLimit = valorActual;
    else robot[id].maxLimit = valorActual;
    nvs.end();
}

// --- BIONIC KINEMATICS ENGINE ---
void planificarTrayectoria() {
    enMovimiento = true;
}

void actualizarFisica() {
    if (!enMovimiento) return;
    
    if (millis() - ultimoTiempoFisica < INTERVALO_FISICA) return;
    ultimoTiempoFisica = millis();

    bool algunEjeMoviendose = false;
    for(int i = 0; i < 6; i++) {
        float error = robot[i].objetivo - robot[i].actual;
        if (abs(error) > KINEMATIC_DEADBAND) {
            
            robot[i].actual = (EMA_ALPHA * robot[i].objetivo) + ((1.0 - EMA_ALPHA) * robot[i].actual);

            if(robot[i].actual < robot[i].minLimit) robot[i].actual = robot[i].minLimit;
            if(robot[i].actual > robot[i].maxLimit) robot[i].actual = robot[i].maxLimit;

            pca.writeMicroseconds(robot[i].canal, (int)robot[i].actual);
            algunEjeMoviendose = true;
        } else {
            if (robot[i].actual != robot[i].objetivo) {
                 robot[i].actual = robot[i].objetivo;
                 pca.writeMicroseconds(robot[i].canal, (int)robot[i].actual);
            }
        }
    }
    if (!algunEjeMoviendose) enMovimiento = false;
}

// --- CLOUD REPORTING ---
void reportarCajaNegra(String poseName) {
    if(WiFi.status() == WL_CONNECTED){
        if (mqttClient.connected()) {
            String jsonPayload;
            jsonPayload.reserve(150); 
            
            jsonPayload = "{\"pose\":\"" + poseName + "\",\"angulos\":[";
            for(int i=0; i<6; i++) {
                jsonPayload += String((int)robot[i].actual);
                if(i<5) jsonPayload += ",";
            }
        
            jsonPayload += "], \"sensores\": {";
            jsonPayload += "\"gripper_mA\":" + String(gripperCurrent_mA) + ",";
            jsonPayload += "\"zone_distance_mm\":" + String(dropZoneDistance_mm) + ",";
            jsonPayload += "\"rgb\":[" + String(colorR) + "," + String(colorG) + "," + String(colorB) + "]";
            jsonPayload += "}}";
            
            mqttClient.publish("sentinel/bionic/telemetry", jsonPayload.c_str());
            Serial.print("[Telemetry] Payload routed to Edge Gateway: ");
            Serial.println(poseName);
        }
    }
}

// ==========================================================
// SETUP 
// ==========================================================
void setup() {
    Serial.begin(115200);
    
    Wire.begin();
    pca.begin();
    pca.setPWMFreq(FRECUENCIA_PWM);
    
    // --- SENSOR BOOT SEQUENCE ---
    Serial.println("[Hardware] Inicializando sensores...");
    
    // 1. Apagar el sensor láser (Reset por Hardware)
    pinMode(XSHUT_PIN, OUTPUT);
    digitalWrite(XSHUT_PIN, LOW);
    delay(50);
    digitalWrite(XSHUT_PIN, HIGH);
    delay(50);

    // 2. Cambiar la dirección del láser a 0x30 [cite: 249, 250, 251]
    if (!lox.begin(0x30)) {
        Serial.println("[Warning] VL53L0X (Láser) no detectado.");
    }

    // 3. Inicializar Color (0x29) [cite: 252]
    if (!tcs.begin()) {
        Serial.println("[Warning] TCS34725 (Color) no detectado.");
    }

    // 4. Inicializar Corriente (0x45) [cite: 253]
    if (!ina219.begin()) {
        Serial.println("[Warning] INA219 (Pinza) no detectado.");
    }
    
    cargarCalibracion();

    Serial.println("Inicializando Bionica...");
    for(int i=0; i<6; i++) {
        pca.writeMicroseconds(robot[i].canal, (int)robot[i].actual);
    }
    delay(500);

    // --- WIFI ---
    WiFi.begin("NOMBRE-DE-LA-RED", "TUCLAVE"); 
    while (WiFi.status() != WL_CONNECTED) { 
        delay(500);
        Serial.print("."); 
    }
    Serial.println("\nIP: " + WiFi.localIP().toString());

    // --- API REST ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });
    
    server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("id") && request->hasParam("val")) {
            int id = request->getParam("id")->value().toInt();
            int val = request->getParam("val")->value().toInt();
            if(id >= 0 && id < 6) {
                robot[id].objetivo = (float)val; 
                planificarTrayectoria();
            }
            request->send(200, "text/plain", "OK");
        }
    });

    server.on("/savePose", HTTP_GET, [](AsyncWebServerRequest *request){
        if(totalPasos < MAX_PASOS_MEMORIA) {
            for(int i=0; i<6; i++) {
                memoria[totalPasos].angulos[i] = robot[i].actual;
            }
            if (request->hasParam("name")) {
                String nombre = request->getParam("name")->value();
                nombre.toCharArray(memoria[totalPasos].etiqueta, 20);
            } else {
                strcpy(memoria[totalPasos].etiqueta, "Sin Nombre");
            }
            totalPasos++;
            request->send(200, "text/plain", "Guardada: " + String(memoria[totalPasos-1].etiqueta));
        } else {
            request->send(200, "text/plain", "Memoria Llena");
        }
    });

    server.on("/runAuto", HTTP_GET, [](AsyncWebServerRequest *request){
        if(totalPasos > 0) { 
            modoAutomatico = true; 
            pasoActual = 0; 
            for(int i=0; i<6; i++) robot[i].objetivo = memoria[pasoActual].angulos[i];
            planificarTrayectoria();
        }
        request->send(200, "text/plain", "Ejecutando");
    });

    server.on("/stopAuto", HTTP_GET, [](AsyncWebServerRequest *request){
        modoAutomatico = false;
        request->send(200, "text/plain", "Detenido");
    });

    server.on("/clearMem", HTTP_GET, [](AsyncWebServerRequest *request){
        totalPasos = 0; modoAutomatico = false;
        request->send(200, "text/plain", "Memoria Limpia");
    });

    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
        StaticJsonDocument<512> doc; 
        doc["auto"] = modoAutomatico;
        doc["moving"] = enMovimiento;
        if(modoAutomatico && totalPasos > 0) {
            doc["currentAction"] = String(memoria[pasoActual].etiqueta);
        } else {
            doc["currentAction"] = "";
        }
        JsonArray pos = doc.createNestedArray("pos");
        for(int i=0; i<6; i++) pos.add(robot[i].actual);
        
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    server.begin();
    mqttClient.setServer(mqtt_broker_ip, mqtt_port);
}
unsigned long ultimoMqtt = 0; 

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        maintainMqttConnection(); 
    }

    actualizarFisica();
    adquirirDatosSensores();

    // 🚀 EL LATIDO SCADA: Enviar datos a Blazor cada 1 segundo, PASE LO QUE PASE 🚀
    if (millis() - ultimoMqtt > 1000) {
        String estadoActual = modoAutomatico ? String(memoria[pasoActual].etiqueta) : "Manual";
        reportarCajaNegra(estadoActual);
        ultimoMqtt = millis();
    }

    // --- LOGICA AUTOMATICA ---
    if (modoAutomatico) {
        if (!enMovimiento) {
            
            // --- ESTADO 0: ESPERANDO OBJETO ---
            if (pasoActual == 0) {
                if (dropZoneDistance_mm > 10 && dropZoneDistance_mm < 100) {
                    Serial.println("[Auto] Objeto detectado. Iniciando secuencia...");
                    pasoActual = 1; 
                    for(int i=0; i<6; i++) {
                        robot[i].objetivo = memoria[pasoActual].angulos[i];
                    }
                    planificarTrayectoria();
                    tiempoEsperaAuto = millis();
                }
            } 
            // --- ESTADO > 0: EJECUTANDO SECUENCIA ---
            else {
                if (millis() - tiempoEsperaAuto > 1500) { 
                    // Ya NO llamamos a reportarCajaNegra aquí, porque el latido lo hace solo
                    
                    pasoActual++;
                    if (pasoActual >= totalPasos) {
                        pasoActual = 0; 
                    }

                    for(int i=0; i<6; i++) {
                        robot[i].objetivo = memoria[pasoActual].angulos[i];
                    }
                    planificarTrayectoria();
                    tiempoEsperaAuto = millis();
                }
            }
        } else {
            tiempoEsperaAuto = millis(); 
            if (pasoActual > 0 && gripperCurrent_mA > 450.0) { 
                robot[5].objetivo = robot[5].actual; 
            }
        }
    }
}