#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "esp_eap_client.h"
#include <IPAddress.h>
#include <vector>
#include <stdarg.h>

// ==========================================================
// 1. DEFINIÇÕES E ESTRUTURAS
// ==========================================================
const int ledPin = 2;
#define RS232_TX 17
#define RS232_RX 16

// Configurações de Tempo
const long SOFTAP_GRACE_PERIOD_MS = 60000; 
const long SOFTAP_STARTUP_TIMEOUT_MS = 10000;

struct AuthClient {
    WiFiClient client;
    String ip;
};

// ==========================================================
// 2. OBJETOS GLOBAIS
// ==========================================================
HardwareSerial SerialRS232(2);
WiFiServer server(80);          
WiFiServer dataServer(9000); 
WiFiServer logServer(5000);  
Preferences prefs;

std::vector<AuthClient> dataClients;
std::vector<AuthClient> logClients;

// ==========================================================
// 3. VARIÁVEIS DE CONFIGURAÇÃO
// ==========================================================
String adminPassword = "123456";
String deviceHostname = "esp32-balanca";
long rs232Baud = 9600;
uint16_t dataPort = 9000;
uint16_t logPort = 5000;

// Variáveis de Rede
bool cancelConnect = false;
String lastSSID = "", lastPass = "", lastUser = "";
bool lastEnterprise = false;
bool lastUseStaticIP = false;
String lastStaticIP = "", lastSubnet = "", lastGateway = "", lastDns1 = "", lastDns2 = "";

// Variáveis Temporárias (Web)
bool connectionRequestFromWeb = false;
String webSsid, webPass, webUser;
bool webEnterprise, webUseStaticIP;
String webStaticIP, webSubnet, webGateway, webDns1, webDns2;

// Estado do Sistema
bool connectionJustSucceeded = false;
unsigned long connectionSuccessTime = 0;
bool isHandlingWebRequest = false;
unsigned long ledPreviousMillis = 0;
const long ledInterval = 500;
bool isRs232Active = false; 

// Controle de Peso
String lastSentWeight = ""; 
String currentWeightBuffer = "";
int dataSendFormat = 1; 

// --- Protótipos ---
void app_log(const char *format, ...);
void saveCredentials();
void startSoftAP();
void handleDataClient(); 
void broadcastWeightAndClientStatusToLog(String cleanWeight); 

// ==========================================================
// 4. SISTEMA DE LOG (Corrigido para incluir contagem e IP)
// ==========================================================

void app_log(const char *format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Serial.print(buf);
    
    // REGRA DE LOG: Só envia o log completo se NÃO houver clientes de dados conectados.
    if (dataClients.empty()) {
        for (int i = logClients.size() - 1; i >= 0; i--) {
            if (logClients[i].client.connected()) {
                logClients[i].client.write((uint8_t*)buf, strlen(buf));
            } else {
                logClients[i].client.stop();
                logClients.erase(logClients.begin() + i);
            }
        }
    }
}

// CORREÇÃO: Implementação do log detalhado com contagem e IPs.
void broadcastWeightAndClientStatusToLog(String cleanWeight) {
    if (logClients.empty()) return;

    String dataClientList = "";
    for (const auto& client : dataClients) {
        if (!dataClientList.isEmpty()) dataClientList += ", ";
        dataClientList += client.ip;
    }
    if (dataClientList.isEmpty()) dataClientList = "Nenhum";

    String logClientList = "";
    for (const auto& client : logClients) {
        if (!logClientList.isEmpty()) logClientList += ", ";
        logClientList += client.ip;
    }
    
    // Log Detalhado (Peso + Contagem e IPs de Clientes)
    String logPayload = "[PESO]|" + cleanWeight + 
                        "|Clientes Dados (" + String(dataClients.size()) + "): " + dataClientList + 
                        "|Clientes Log (" + String(logClients.size()) + "): " + logClientList + 
                        "\n";
    
    for (int i = logClients.size() - 1; i >= 0; i--) {
        if (logClients[i].client.connected()) {
            logClients[i].client.write((const uint8_t*)logPayload.c_str(), logPayload.length());
        } else {
            logClients[i].client.stop();
            logClients.erase(logClients.begin() + i);
        }
    }
}


void handleLogClients() {
    if (logServer.hasClient()) {
        WiFiClient newClient = logServer.available();
        if (newClient) {
            AuthClient lc;
            lc.client = newClient;
            lc.ip = newClient.remoteIP().toString();
            logClients.push_back(lc);
            app_log("[LOG] Novo monitor IP: %s\n", lc.ip.c_str());
            app_log("[LOG] Clientes conectados: %d dados, %d log\n", dataClients.size(), logClients.size());
        }
    }

    // Limpa clientes de log desconectados
    for (int i = logClients.size() - 1; i >= 0; i--) {
        if (!logClients[i].client.connected()) {
            logClients[i].client.stop();
            logClients.erase(logClients.begin() + i);
            app_log("[LOG] Monitor desconectado: %s\n", logClients[i].ip.c_str());
        }
    }
}

// ==========================================================
// 5. GESTÃO DE REDE (WIFI)
// ==========================================================
void WiFiEvent(arduino_event_id_t event, arduino_event_info_t info){
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START: app_log("[WIFI] Station Iniciado.\n"); break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED: app_log("[WIFI] Conectado ao AP.\n"); break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: app_log("[WIFI] Desconectado! Razão: %d\n", info.wifi_sta_disconnected.reason); break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP: app_log("[IP] Atribuído: %s\n", WiFi.localIP().toString().c_str()); break;
        default: break;
    }
}

void configureStaticIP() {
    if (lastUseStaticIP && lastStaticIP.length() > 0) {
        IPAddress staticIP, gateway, subnet, dns1, dns2;
        if (staticIP.fromString(lastStaticIP) && gateway.fromString(lastGateway) && subnet.fromString(lastSubnet)) {
            dns1.fromString(lastDns1); dns2.fromString(lastDns2);
            app_log("[IP] Configurando IP Estático: %s\n", staticIP.toString().c_str());
            if (!WiFi.config(staticIP, gateway, subnet, dns1, dns2)) app_log("[ERRO] Falha IP Estático.\n");
        } else {
            app_log("[ERRO] Configuração IP inválida. Usando DHCP.\n");
            WiFi.config(IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(0,0,0,0));
        }
    } else {
        app_log("[IP] DHCP Ativado.\n");
        WiFi.config(IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(0,0,0,0));
    }
}

void startServices() {
    server.begin(); 
    dataServer.end(); dataServer.begin(dataPort);
    logServer.end(); logServer.begin(logPort);

    ArduinoOTA.setHostname(deviceHostname.c_str());
    ArduinoOTA.setPassword(adminPassword.c_str());
    ArduinoOTA.begin();
    app_log("[SISTEMA] Serviços Online. Portas - Web:80, Dados:%d, Log:%d\n", dataPort, logPort);
}

void connectNormal(String ssid, String pass) {
    cancelConnect = false;
    app_log("[CONEXAO] WPA2: %s\n", ssid.c_str());
    WiFi.disconnect(true); WiFi.mode(WIFI_AP_STA); configureStaticIP();
    WiFi.begin(ssid.c_str(), pass.c_str());
    int t = 0;
    while (WiFi.status() != WL_CONNECTED && t < 20 && !cancelConnect) { delay(1000); app_log("."); t++; }
    if (WiFi.status() == WL_CONNECTED) {
        // CORREÇÃO: Salva as credenciais/IPs usados
        lastSSID = ssid; lastPass = pass; lastEnterprise = false;
        lastUseStaticIP = webUseStaticIP; 
        if(lastUseStaticIP){ 
             lastStaticIP = webStaticIP; lastSubnet = webSubnet;
             lastGateway = webGateway; lastDns1 = webDns1; lastDns2 = webDns2;
        } else { 
             lastStaticIP = ""; lastSubnet = ""; lastGateway = ""; lastDns1 = ""; lastDns2 = "";
        }
        saveCredentials(); 
        connectionJustSucceeded = true; connectionSuccessTime = millis();
        startServices();
    } else { app_log("\n[FALHA] Conexão.\n"); }
}

void connectEnterprise(String ssid, String user, String pass) {
    cancelConnect = false;
    app_log("[CONEXAO] Enterprise: %s\n", ssid.c_str());
    WiFi.disconnect(true); WiFi.mode(WIFI_AP_STA); configureStaticIP();
    esp_eap_client_set_identity((uint8_t*)user.c_str(), user.length());
    esp_eap_client_set_username((uint8_t*)user.c_str(), user.length());
    esp_eap_client_set_password((uint8_t*)pass.c_str(), pass.length());
    esp_wifi_sta_enterprise_enable();
    WiFi.begin(ssid.c_str());
    int t = 0;
    while (WiFi.status() != WL_CONNECTED && t < 30 && !cancelConnect) { delay(1000); app_log("."); t++; }
    if (WiFi.status() == WL_CONNECTED) {
        // CORREÇÃO: Salva as credenciais/IPs usados
        lastSSID = ssid; lastUser = user; lastPass = pass; lastEnterprise = true;
        lastUseStaticIP = webUseStaticIP; 
        if(lastUseStaticIP){ 
             lastStaticIP = webStaticIP; lastSubnet = webSubnet;
             lastGateway = webGateway; lastDns1 = webDns1; lastDns2 = webDns2;
        } else { 
             lastStaticIP = ""; lastSubnet = ""; lastGateway = ""; lastDns1 = ""; lastDns2 = "";
        }
        saveCredentials(); 
        connectionJustSucceeded = true; connectionSuccessTime = millis();
        startServices();
    } else { app_log("\n[FALHA] Enterprise.\n"); startSoftAP(); }
}

// ==========================================================
// 6. PERSISTÊNCIA E HARDWARE
// ==========================================================
void saveCredentials() {
    prefs.begin("wifiCreds", false);
    prefs.putString("ssid", lastSSID); prefs.putString("pass", lastPass);
    prefs.putString("user", lastUser); prefs.putBool("enterprise", lastEnterprise);
    prefs.putBool("useStatic", lastUseStaticIP);
    if(lastUseStaticIP){
        prefs.putString("staticIP", lastStaticIP); prefs.putString("subnet", lastSubnet);
        prefs.putString("gateway", lastGateway); prefs.putString("dns1", lastDns1); 
        prefs.putString("dns2", lastDns2); 
    }
    prefs.end();
}

void loadCredentials() {
    prefs.begin("wifiCreds", true);
    lastSSID = prefs.getString("ssid", ""); lastPass = prefs.getString("pass", "");
    lastUser = prefs.getString("user", ""); lastEnterprise = prefs.getBool("enterprise", false);
    lastUseStaticIP = prefs.getBool("useStatic", false);
    lastStaticIP = prefs.getString("staticIP", ""); lastSubnet = prefs.getString("subnet", "");
    lastGateway = prefs.getString("gateway", ""); lastDns1 = prefs.getString("dns1", "");
    lastDns2 = prefs.getString("dns2", ""); 
    prefs.end();
    
    prefs.begin("sysConfig", true);
    adminPassword = prefs.getString("admPass", "123456");
    dataPort = prefs.getUShort("dPort", 9000);
    logPort = prefs.getUShort("lPort", 5000);
    rs232Baud = prefs.getLong("baud", 9600);
    dataSendFormat = prefs.getInt("dsf", 1); 
    prefs.end();
}

void clearCredentials(){ prefs.begin("wifiCreds", false); prefs.clear(); prefs.end(); lastSSID=""; }

void attemptReconnect() {
    if (lastSSID.length() > 0) {
        if (lastEnterprise) connectEnterprise(lastSSID, lastUser, lastPass);
        else connectNormal(lastSSID, lastPass);
    }
}

void checkSerialState() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!isRs232Active) {
            SerialRS232.begin(rs232Baud, SERIAL_8N1, RS232_RX, RS232_TX);
            isRs232Active = true;
            app_log("[HARDWARE] Serial Ativada. Baud: %ld\n", rs232Baud);
        }
    } else {
        if (isRs232Active) {
            SerialRS232.end(); 
            isRs232Active = false;
            app_log("[HARDWARE] Serial Pausada (Modo Config).\n");
        }
    }
}

void startSoftAP() { 
    WiFi.mode(WIFI_AP_STA); 
    WiFi.softAP("ESP32_Config", "12345678"); 
    server.begin(); 
}

void stopSoftAP() { 
    if (WiFi.getMode() == WIFI_AP_STA) WiFi.softAPdisconnect(true); 
}

// ==========================================================
// 7. LÓGICA DE DADOS
// ==========================================================
String cleanWeightLine(const String &line) {
    String filtered = "";
    for (char c : line) { if (c >= 32 && c <= 126) filtered += c; }
    return filtered;
}

void broadcastData(String jsonPayload) {
    for (int i = dataClients.size() - 1; i >= 0; i--) {
        if (!dataClients[i].client.connected()) {
            dataClients[i].client.stop(); dataClients.erase(dataClients.begin() + i); continue;
        }
        size_t len = jsonPayload.length();
        dataClients[i].client.write((const uint8_t*)jsonPayload.c_str(), len);
        dataClients[i].client.write('\n'); 
    }
}

void handleDataClient() {
    if (dataServer.hasClient()) {
        WiFiClient c = dataServer.available();
        if (c) { 
            AuthClient ac; 
            ac.client = c; 
            ac.ip = c.remoteIP().toString(); 
            dataClients.push_back(ac);
            app_log("[DADOS] Novo cliente IP: %s\n", ac.ip.c_str());
        }
    }
    
    // Limpa clientes de dados desconectados
    for (int i = dataClients.size() - 1; i >= 0; i--) {
        if (!dataClients[i].client.connected()) {
            app_log("[DADOS] Cliente desconectado: %s\n", dataClients[i].ip.c_str());
            dataClients[i].client.stop(); 
            dataClients.erase(dataClients.begin() + i);
        }
    }

    if (isRs232Active && SerialRS232.available()) {
        while (SerialRS232.available()) {
            char c = SerialRS232.read();
            if (c == '\n' || c == '\r') {
                currentWeightBuffer.trim();
                if (currentWeightBuffer.length() > 0) {
                    String cw = cleanWeightLine(currentWeightBuffer);
                    if (cw != lastSentWeight) {
                        String payload;
                        if (dataSendFormat == 0) {
                            // FORMATO 0: JSON Completo (Pesagem e Latência)
                            payload = "{\"weight\":\"" + cw + "\",\"timestamp_ms\":" + String(millis()) + "}";
                        } else {
                            // FORMATO 1: Somente Pesagem (String Bruta Limpa)
                            payload = cw;
                        }

                        broadcastData(payload); 
                        lastSentWeight = cw;
                        
                        // LÓGICA DE LOG: Se há clientes de dados, envia o log condicional detalhado (Peso + Clientes)
                        if (!dataClients.empty()) {
                            broadcastWeightAndClientStatusToLog(cw);
                        } else {
                            // Se não há clientes de dados, usa o app_log padrão (que envia log detalhado)
                            app_log("[BALANCA] Peso: %s (Formato: %d)\n", cw.c_str(), dataSendFormat);
                        }
                    }
                }
                currentWeightBuffer = "";
            } else { currentWeightBuffer += c; }
        }
    }
}

// ==========================================================
// 8. SERVIDOR WEB - LAYOUT E FUNCIONALIDADES CORRIGIDOS
// ==========================================================
String urlDecode(String str) {
    String d = ""; char t[] = "0x00";
    for (unsigned int i = 0; i < str.length(); i++) {
        if (str[i] == '%') {
            if (i + 2 < str.length()) { t[2] = str[i+1]; t[3] = str[i+2]; d += (char)strtol(t, NULL, 16); i += 2; }
        } else if (str[i] == '+') d += ' '; else d += str[i];
    } return d;
}

String getParam(String req, String key) {
    String p = key + "="; int s = req.indexOf(p); if(s == -1) return "";
    s += p.length(); int e = req.indexOf('&', s);
    if(e == -1) e = req.indexOf(' ', s); if(e == -1) e = req.length();
    return urlDecode(req.substring(s, e));
}

void handleWebClient(WiFiClient client) {
    String req = client.readStringUntil('\r');
    client.flush();
    
    // --- APIs ---
    if (req.indexOf("GET /scan") != -1) {
        if(isRs232Active) { SerialRS232.end(); isRs232Active = false; } 
        int n = WiFi.scanNetworks();
        String json = "[";
        for (int i=0; i<n; ++i) {
            json += "{\"ssid\":\""+WiFi.SSID(i)+"\",\"rssi\":"+String(WiFi.RSSI(i))+"}";
            if(i < n-1) json += ",";
        }
        json += "]";
        client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + json);
        return;
    }

    // CORREÇÃO: Endpoint /save para lidar com POST de formulário
    if (req.indexOf("POST /save") != -1) {
        String body = "";
        while(client.available()) { body += (char)client.read(); }
        
        String pAdm = getParam(body, "adm");
        
        // Verifica se a senha de administração está correta
        if (pAdm == adminPassword) {
            String newPass = getParam(body, "newp");
            
            if(newPass.length() >= 4) adminPassword = newPass;
            
            dataPort = getParam(body, "dp").toInt();
            logPort = getParam(body, "lp").toInt();
            rs232Baud = getParam(body, "br").toInt();
            dataSendFormat = getParam(body, "dsf").toInt();
            
            prefs.begin("sysConfig", false);
            prefs.putString("admPass", adminPassword);
            prefs.putUShort("dPort", dataPort);
            prefs.putUShort("lPort", logPort);
            prefs.putLong("baud", rs232Baud);
            prefs.putInt("dsf", dataSendFormat);
            prefs.end();

            app_log("[CONFIG SAVE] Configurações salvas. Reiniciando...\n");
            
            client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                            "<html><body style='font-family: Arial, sans-serif; text-align: center; padding: 50px;'>"
                            "<h2>✅ Configurações Salvas!</h2>"
                            "<p>As configurações foram salvas com sucesso. Reiniciando o sistema...</p>"
                            "<script>setTimeout(() => window.location.href='/', 3000);</script>"
                            "</body></html>");
            delay(1000); 
            ESP.restart(); 
        } else { 
            client.println("HTTP/1.1 403 Forbidden\r\nContent-Type: text/html\r\n\r\n"
                            "<html><body style='font-family: Arial, sans-serif; text-align: center; padding: 50px;'>"
                            "<h2 style='color: red;'>Senha Incorreta</h2>"
                            "<p>Tente novamente.</p>"
                            "<script>setTimeout(() => window.location.href='/', 3000);</script>"
                            "</body></html>");
        }
        return;
    }

    if (req.indexOf("GET /connect") != -1) {
        webSsid = getParam(req, "ssid"); webPass = getParam(req, "pass");
        webUser = getParam(req, "user"); webEnterprise = (req.indexOf("ent=on") != -1);
        webUseStaticIP = (req.indexOf("st=on") != -1);
        if(webUseStaticIP){
            webStaticIP = getParam(req, "ip"); webSubnet = getParam(req, "msk");
            webGateway = getParam(req, "gw"); webDns1 = getParam(req, "d1");
            webDns2 = getParam(req, "d2"); 
        }
        connectionRequestFromWeb = true;
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                       "<html><body style='font-family: Arial, sans-serif; text-align: center; padding: 50px;'>"
                       "<h2>Aplicando Configurações...</h2>"
                       "<p>Conectando à rede " + webSsid + ". Aguarde 10 segundos para verificar o status.</p>"
                       "<script>setTimeout(() => window.location.href='/', 10000);</script>"
                       "</body></html>");
        return;
    }

    if (req.indexOf("GET /disconnect") != -1) {
        String pAdm = getParam(req, "adm");
        if (pAdm == adminPassword) {
            client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                            "<html><body style='font-family: Arial, sans-serif; text-align: center; padding: 50px;'>"
                            "<h2>Desconectando...</h2>"
                            "<p>Limpando configurações e reiniciando.</p>"
                            "</body></html>");
            delay(1000); 
            WiFi.disconnect(true); 
            clearCredentials(); 
            ESP.restart();
        } else { 
            client.println("HTTP/1.1 403 Forbidden\r\nContent-Type: text/html\r\n\r\n"
                            "<html><body style='font-family: Arial, sans-serif; text-align: center; padding: 50px;'>"
                            "<h2 style='color: red;'>Senha Incorreta</h2>"
                            "<p>Tente novamente.</p>"
                            "<script>setTimeout(() => window.location.href='/', 3000);</script>"
                            "</body></html>");
        }
        return;
    }

    // --- HTML PRINCIPAL ---
    client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n");
    client.println("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>");
    client.println("<title>Configurador Wi-Fi ESP32</title>");
    client.println("<style>");
    // CSS CORRIGIDO para inputs maiores e consistentes
    client.println("body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;background-color:#f0f2f5;margin:0;padding:20px;display:flex;justify-content:center;align-items:center;min-height:100vh;box-sizing:border-box;}");
    client.println(".container{background-color:white;padding:30px 35px;border-radius:12px;box-shadow:0 6px 20px rgba(0,0,0,0.1);width:100%;max-width:600px;}");
    client.println("h1{color:#1c1e21;text-align:center;font-size:28px;margin-bottom:8px;font-weight:600;}");
    client.println("h2{color:#606770;text-align:center;font-size:18px;margin-bottom:25px;font-weight:normal;}");
    client.println("h3{color:#1c1e21;font-size:20px;margin-top:25px;margin-bottom:15px;border-bottom:1px solid #eee;padding-bottom:8px;}");
    client.println(".status{border:2px solid;padding:18px;margin-bottom:25px;border-radius:8px;font-size:16px;}");
    client.println(".status.connected{background-color:#e7f3ff;border-color:#0866ff;color:#0866ff;}");
    client.println(".status.disconnected{background-color:#fff0f0;border-color:#a22;color:#a22;}");
    client.println(".status b{display:block;margin-bottom:10px;color:#1c1e21;font-size:18px;font-weight:600;}");
    client.println(".status p{margin:6px 0;font-size:15px;}");
    client.println("form div{margin-bottom:18px;}");
    client.println("label{font-weight:600;color:#606770;font-size:15px;margin-bottom:6px;display:block;}");
    
    // INPUTS CORRIGIDOS
    client.println("input[type='text'],input[type='password'],input[type='number'],select{width:100%;padding:12px 14px;border:1px solid #dddfe2;border-radius:6px;box-sizing:border-box;font-size:16px;height:48px;line-height:24px;}");
    
    client.println(".checkbox-group{display:flex;align-items:center;margin-top:18px;}");
    client.println(".checkbox-group input{margin-right:12px;width:18px;height:18px;}");
    client.println(".checkbox-group label{margin-bottom:0;font-weight:normal;font-size:15px;display:inline;}");
    client.println("input[type='submit'],.btn{width:100%;background-color:#0866ff;color:white;border:none;padding:14px;font-size:17px;border-radius:6px;cursor:pointer;font-weight:600;margin-top:12px;transition:background-color 0.2s;}");
    client.println("input[type='submit']:hover,.btn:hover{background-color:#0654d1;}");
    client.println(".btn.disconnect{background-color:#d93636;} .btn.disconnect:hover{background-color:#b32b2b;}");
    client.println(".btn.scan{background-color:#28a745;margin-bottom:18px;} .btn.scan:hover{background-color:#218838;}");
    client.println(".btn.config{background-color:#6c757d;margin-top:8px;} .btn.config:hover{background-color:#5a6268;}");
    client.println(".hide{display:none;}");
    client.println(".modal{display:none;position:fixed;z-index:1000;left:0;top:0;width:100%;height:100%;overflow:auto;background-color:rgba(0,0,0,0.5);padding-top:60px;}");
    client.println(".modal-content{background-color:#fefefe;margin:10% auto;padding:25px;border:1px solid #888;width:90%;max-width:400px;border-radius:10px;box-shadow:0 4px 12px rgba(0,0,0,0.2);}");
    client.println(".close{color:#aaa;float:right;font-size:28px;font-weight:bold;cursor:pointer;line-height:1;}");
    client.println(".close:hover{color:black;}");
    client.println("#list div{padding:12px;border-bottom:1px solid #eee;cursor:pointer;font-size:15px;}");
    client.println("#list div:hover{background:#f8f9fa;}");
    client.println(".input-group div{margin-bottom:15px;}");
    client.println("hr{border:none;border-top:1px solid #dddfe2;margin:25px 0;}");
    client.println(".info-text{color:#606770;font-size:14px;text-align:center;margin-top:15px;font-style:italic;}");
    client.println("</style>");

    client.println("<script>");
    // Funções de controle de UI
    client.println("function toggle(id){var x=document.getElementById(id); x.classList.toggle('hide');}");
    client.println("function showModal(mode){ document.getElementById('authModal').style.display='block'; document.getElementById('authMode').value=mode; document.getElementById('modalPass').value=''; }");
    client.println("function closeModal(){ document.getElementById('authModal').style.display='none'; }");
    
    // Função para auto-preencher o SSID
    client.println("function selectSsid(ssid){ document.getElementById('ssid').value=ssid; document.getElementById('list').scrollIntoView({behavior: 'smooth'}); }");
    
    // Função de autenticação e ação (CONEXÃO, DESCONEXÃO, SALVAR)
    client.println("function auth(){");
    client.println("  var p=document.getElementById('modalPass').value;");
    client.println("  var mode=document.getElementById('authMode').value;");
    client.println("  var realPass='"+adminPassword+"';"); 
    client.println("  if(p===realPass){");
    client.println("    closeModal();");
    client.println("    if(mode==='disconnect'){");
    client.println("      window.location.href='/disconnect?adm='+encodeURIComponent(p);");
    client.println("    } else if(mode==='configSave'){");
    client.println("      document.getElementById('adm-pass-field').value = p;"); // Injeta a senha
    client.println("      document.getElementById('config-form').submit();");     // Submete o formulário /save
    client.println("    } else {");
    client.println("      document.getElementById('admin-area').classList.remove('hide');");
    client.println("    }");
    client.println("  } else {");
    client.println("    alert('Senha Incorreta!');");
    client.println("  }");
    client.println("}");

    // Função Scan que usa a nova selectSsid
    client.println("function scan(){ document.getElementById('list').innerHTML='<div style=\\'text-align:center;padding:20px;\\'>Buscando redes...</div>'; fetch('/scan').then(r=>r.json()).then(d=>{ let h=''; if(d.length===0){ h='<div style=\\'text-align:center;padding:20px;\\'>Nenhuma rede encontrada</div>'; } else { d.forEach(n=>{ h+=`<div onclick=\\'selectSsid(\\'${n.ssid}\\')\\'>${n.ssid} (${n.rssi}dBm)</div>` }); } document.getElementById('list').innerHTML=h; }); }");
    client.println("</script></head><body><div class='container'>");

    if (WiFi.status() == WL_CONNECTED) {
        // === TELA CONECTADO ===
        client.println("<h1>✅ Conectado</h1>");
        client.println("<h2>ESP32 Balança - Sistema Operacional</h2>");
        
        client.println("<div class='status connected'><b>Status da Rede</b>");
        client.println("<p><strong>SSID:</strong> " + WiFi.SSID() + "</p>");
        client.println("<p><strong>IP Local:</strong> " + WiFi.localIP().toString() + "</p>");
        client.println("<p><strong>MAC Address:</strong> " + WiFi.macAddress() + "</p>");
        client.println("<p><strong>Porta Dados:</strong> " + String(dataPort) + "</p>");
        client.println("<p><strong>Porta Log:</strong> " + String(logPort) + "</p></div>");

        // Botão para abrir configurações (Protegido)
        client.println("<button class='btn scan' onclick=\"showModal('config')\">🔍 Abrir Configurações Avançadas</button>");
        
        // ÁREA PROTEGIDA
        client.println("<div id='admin-area' class='hide'>");
            client.println("<button class='btn disconnect' onclick=\"showModal('disconnect')\">🚫 Desconectar e Reconfigurar</button>");
            
            client.println("<h3>Configurar Nova Rede</h3>");
            client.println("<button class='btn config' onclick='scan()'>🔄 Atualizar Lista de Redes</button>");
            client.println("<div id='list' style='max-height:200px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;'></div>");
            
            client.println("<form action='/connect' method='get'>");
            client.println("<div><label>Nome da Rede (SSID)</label><input id='ssid' name='ssid' type='text' required placeholder='Digite o nome da rede' value='"+lastSSID+"'></div>");
            client.println("<div><label>Senha da Rede</label><input type='password' name='pass' placeholder='Digite a senha' value='"+lastPass+"'></div>");
            
            // CORRIGIDO (Enterprise Checkbox)
            client.println(String("<div class='checkbox-group'><input type='checkbox' name='ent' onclick='toggle(\"ent-box\")'") + 
                           (lastEnterprise ? " checked" : "") + 
                           "><label>Rede Corporativa (WPA2-Enterprise)</label></div>");
            
            // CORRIGIDO (Enterprise User Input)
            client.println(String("<div id='ent-box' class='input-group") +
                           (lastEnterprise ? "" : " hide") + 
                           "'><div><label>Usuário</label><input name='user' placeholder='Seu usuário corporativo' value='" + lastUser + "'></div></div>");
            
            // CORRIGIDO (Static IP Checkbox)
            client.println(String("<div class='checkbox-group'><input type='checkbox' name='st' onclick='toggle(\"ip-box\")'") + 
                           (lastUseStaticIP ? " checked" : "") + 
                           "><label>Configurar IP Estático</label></div>");

            // CORRIGIDO (Static IP Div Start)
            client.println(String("<div id='ip-box' class='input-group") +
                           (lastUseStaticIP ? "" : " hide") + 
                           "'>");
            
            // Campos de IP Estático
            client.println("<div><label>Endereço IP</label><input name='ip' placeholder='Ex: 192.168.0.50' value='"+lastStaticIP+"'></div>");
            client.println("<div><label>Máscara de Sub-rede</label><input style='height:50px;max-height:250px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;width:100%;max-width:600px;' name='msk' placeholder='Ex: 255.255.255.0' value='"+lastSubnet+"'></div>");
            client.println("<div><label>Gateway</label><input style='height:50px;max-height:250px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;width:100%;max-width:600px;' name='gw' placeholder='Ex: 192.168.0.1' value='"+lastGateway+"'></div>");
            client.println("<div><label>DNS Primário</label><input style='height:50px;max-height:250px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;width:100%;max-width:600px;' name='d1' placeholder='Ex: 8.8.8.8' value='"+lastDns1+"'></div>");
            client.println("<div><label>DNS Secundário</label><input style='height:50px;max-height:250px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;width:100%;max-width:600px;' name='d2' placeholder='Ex: 8.8.4.4' value='"+lastDns2+"'></div>"); 
            client.println("</div>");
            
            client.println("<input type='submit' value='🔗 Conectar à Rede'>");
            client.println("</form>");

            // Configurações Técnicas
            client.println("<hr><h3>⚙️ Configurações Avançadas</h3>");
            client.println("<form action='/save' method='post' id='config-form'>"); 
            client.println("<input type='hidden' id='adm-pass-field' name='adm' value=''>");
            client.println("<div><label>Nova Senha de Administrador</label><input type='password' name='newp' placeholder='Mínimo 4 caracteres'></div>");
            
            // CAMPO DE FORMATO DE ENVIO
            client.println("<div><label>Formato de Envio de Dados</label><select name='dsf'>");
            client.println("<option value='0' "+String(dataSendFormat==0?"selected":"")+">0 - JSON Completo (Peso + Timestamp)</option>");
            client.println("<option value='1' "+String(dataSendFormat==1?"selected":"")+">1 - Somente Pesagem (Texto Simples)</option>");
            client.println("</select></div>");

            client.println("<div><label>Porta de Dados</label><input type='number' name='dp' value='"+String(dataPort)+"' min='1' max='65535'></div>");
            client.println("<div><label>Porta de Log</label><input type='number' name='lp' value='"+String(logPort)+"' min='1' max='65535'></div>");
            client.println("<div><label>Velocidade da Balança (Baud Rate)</label><input type='number' name='br' value='"+String(rs232Baud)+"'></div>");
            
            // Botão Salvar que chama a modal
            client.println("<button type='button' onclick=\"showModal('configSave')\" class='btn config'>💾 Salvar Configurações e Reiniciar</button>"); 
            client.println("</form>");
            client.println("<p class='info-text'>*As configurações avançadas requerem reinício para serem aplicadas.</p>");
        client.println("</div>");
        
    } else {
        // === TELA DESCONECTADO / CONFIG ===
        client.println("<h1>🔌 Configuração de Rede</h1>");
        client.println("<h2>Conecte o ESP32 à sua rede Wi-Fi</h2>");
        client.println("<div class='status disconnected'><b>Status: Não Conectado</b>");
        client.println("<p>Configure uma rede Wi-Fi para começar a usar o sistema</p></div>");
        
        client.println("<button class='btn scan' onclick='scan()'>🔍 Procurar Redes Wi-Fi</button>");
        client.println("<div id='list' style='max-height:250px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;'></div>");

        client.println("<form action='/connect' method='get'>");
        client.println("<div><label>Nome da Rede (SSID)</label><input id='ssid' name='ssid' type='text' required placeholder='Digite ou selecione o nome da rede' value='"+lastSSID+"'></div>");
        client.println("<div><label>Senha da Rede</label><input type='password' name='pass' placeholder='Digite a senha da rede' value='"+lastPass+"'></div>");
        
        // LINHA 693: CORRIGIDA (Enterprise Checkbox)
        client.println(String("<div class='checkbox-group'><input type='checkbox' name='ent' onclick='toggle(\"ent-box\")'") +
                       (lastEnterprise?" checked":"")+"><label>Rede Corporativa (WPA2-Enterprise)</label></div>");
        
        // LINHA 698: CORRIGIDA (Enterprise User Input) - ESTE ERA O ERRO 1
        client.println(String("<div id='ent-box' class='input-group") +
                       (lastEnterprise?"":" hide") + 
                       "'><div><label>Usuário</label><input style='height:50px;max-height:250px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;width:100%;max-width:600px;'  name='user' placeholder='Seu usuário corporativo' value='"+lastUser+"'></div></div>");
        
        // CORRIGIDO (Static IP Checkbox)
        client.println(String("<div class='checkbox-group'><input type='checkbox' name='st' onclick='toggle(\"ip-box\")'") +
                       (lastUseStaticIP?" checked":"")+"><label>Configurar IP Estático</label></div>");
        
        // LINHA 704: CORRIGIDA (Static IP Div Start) - ESTE ERA O ERRO 2
        client.println(String("<div id='ip-box' class='input-group") +
                       (lastUseStaticIP?"":" hide") + 
                       "'>");
        
        client.println("<div><label>Endereço IP</label><input style='height:50px;max-height:250px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;width:100%;max-width:600px;' name='ip' placeholder='Ex: 192.168.0.50' value='"+lastStaticIP+"'></div>");
        client.println("<div><label>Máscara de Sub-rede</label><input style='height:50px;max-height:250px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;width:100%;max-width:600px;' name='msk' placeholder='Ex: 255.255.255.0' value='"+lastSubnet+"'></div>");
        client.println("<div><label>Gateway Padrão</label><input style='height:50px;max-height:250px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;width:100%;max-width:600px;' name='gw' placeholder='Ex: 192.168.0.1' value='"+lastGateway+"'></div>");
        client.println("<div><label>DNS Primário</label><input style='height:50px;max-height:250px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;width:100%;max-width:600px;' name='d1' placeholder='Ex: 8.8.8.8' value='"+lastDns1+"'></div>");
        client.println("<div><label>DNS Secundário</label><input style='height:50px;max-height:250px;overflow-y:auto;margin-bottom:18px;border:1px solid #ddd;border-radius:6px;width:100%;max-width:600px;' name='d2' placeholder='Ex: 8.8.4.4' value='"+lastDns2+"'></div>"); 
        client.println("</div>");

        client.println("<input type='submit' value='🔗 Conectar à Rede'>");
        client.println("</form>");
    }

    // MODAL DE SENHA (POPUP)
    client.println("<div id='authModal' class='modal'><div class='modal-content'>");
    client.println("<span class='close' onclick='closeModal()'>&times;</span>");
    client.println("<h3>🔒 Autenticação Requerida</h3>");
    client.println("<p>Digite a senha de administrador para continuar:</p>");
    client.println("<input type='password' id='modalPass' placeholder='Senha de administrador'>");
    client.println("<input type='hidden' id='authMode'>");
    client.println("<button class='btn' onclick='auth()' style='background-color:#28a745;'>🔓 Verificar Senha</button>");
    client.println("</div></div>");

    client.println("</div></body></html>");
}

// ==========================================================
// 9. SETUP E LOOP
// ==========================================================
void setup() {
    Serial.begin(115200);
    pinMode(ledPin, OUTPUT);
    loadCredentials();
    WiFi.onEvent(WiFiEvent);
    attemptReconnect();
    
    unsigned long s = millis();
    while(WiFi.status() != WL_CONNECTED && millis() - s < SOFTAP_STARTUP_TIMEOUT_MS) { delay(500); }
    
    if (WiFi.status() != WL_CONNECTED) startSoftAP();
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(ledPin, HIGH);
        ArduinoOTA.handle();
        if (connectionJustSucceeded && (millis() - connectionSuccessTime > SOFTAP_GRACE_PERIOD_MS)) {
            stopSoftAP(); connectionJustSucceeded = false;
        }
    } else {
        if (millis() - ledPreviousMillis > 500) {
            ledPreviousMillis = millis(); digitalWrite(ledPin, !digitalRead(ledPin));
        }
    }

    checkSerialState();

    if (connectionRequestFromWeb) {
        connectionRequestFromWeb = false;
        
        stopSoftAP(); 
        if(isRs232Active) { SerialRS232.end(); isRs232Active = false; } 

        if (webEnterprise) connectEnterprise(webSsid, webUser, webPass);
        else connectNormal(webSsid, webPass);
    }

    WiFiClient client = server.accept();
    if (client) {
        isHandlingWebRequest = true;
        handleWebClient(client);
        client.stop();
        isHandlingWebRequest = false;
    }

    if (WiFi.status() == WL_CONNECTED && !isHandlingWebRequest) {
        handleLogClients();
        handleDataClient();
    }
}