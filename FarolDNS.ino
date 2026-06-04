#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>

// --- CONFIGURAÇÕES ---
const char* ssid = "santper";
const char* password = "aptodenill2";

// Upstream DNS (Cloudflare)
const IPAddress PRIMARY_DNS(1, 1, 1, 1); 
const unsigned int LOCAL_DNS_PORT = 53; 

// Tamanho máximo para pacotes DNS UDP
#define MAX_DNS_PACKET_SIZE 512 
byte packetBuffer[MAX_DNS_PACKET_SIZE]; 
byte replyBuffer[MAX_DNS_PACKET_SIZE]; 

// Instâncias UDP
WiFiUDP UdpServer; // Escuta requisições dos clientes (porta 53)
WiFiUDP UdpClient; // Envia requisições para o DNS público e escuta a resposta

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\nIniciando servidor DNS encaminhador...");

    // Conecta ao Wi-Fi
    WiFi.begin(ssid, password);
    Serial.print("Conectando ao Wi-Fi ");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("\nConectado!");
    Serial.print("Endereço IP do ESP32: ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway IP: ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("DNS Server 1: ");
    Serial.println(WiFi.dnsIP(0));

    // Teste de conectividade HTTP básica
    Serial.println("Testando conectividade HTTP externa...");
    HTTPClient http;
    http.begin("http://example.com");
    int httpCode = http.GET();
    if (httpCode > 0) {
        Serial.printf("HTTP GET bem-sucedido. Código: %d\n", httpCode);
    } else {
        Serial.printf("Falha no HTTP GET. Erro: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();

    // Inicializa o servidor UDP na porta 53
    if (UdpServer.begin(LOCAL_DNS_PORT)) {
        Serial.printf("Servidor UDP ouvindo na porta %d\n", LOCAL_DNS_PORT);
    } else {
        Serial.println("Erro ao iniciar UdpServer na porta 53!");
    }

    // Inicializa o UdpClient em uma porta aleatória/efêmera
    if (UdpClient.begin(0)) {
        Serial.printf("UdpClient iniciado na porta local %d\n", UdpClient.localPort());
    } else {
        Serial.println("Erro ao iniciar UdpClient!");
    }

    Serial.print("Upstream DNS configurado: ");
    Serial.println(PRIMARY_DNS);
}

void loop() {
    // Verifica se há pacotes de clientes DNS
    int packetSize = UdpServer.parsePacket();
    if (packetSize > 0) {
        IPAddress remoteIP = UdpServer.remoteIP();
        unsigned int remotePort = UdpServer.remotePort();

        Serial.printf("\n[DNS Query] Recebida de %s:%d (Tamanho: %d bytes)\n", 
                      remoteIP.toString().c_str(), remotePort, packetSize);

        // Lê o pacote
        int bytesRead = UdpServer.read(packetBuffer, min(packetSize, MAX_DNS_PACKET_SIZE));
        if (bytesRead <= 0) {
            Serial.println("Erro: Falha ao ler dados do pacote.");
            return;
        }

        // Encaminha para o DNS público
        Serial.printf("Encaminhando consulta para o DNS público %s:53...\n", PRIMARY_DNS.toString().c_str());
        UdpClient.beginPacket(PRIMARY_DNS, 53);
        UdpClient.write(packetBuffer, bytesRead);
        if (UdpClient.endPacket()) {
            Serial.println("Pacote enviado com sucesso. Aguardando resposta...");
        } else {
            Serial.println("Falha ao enviar pacote para o DNS público!");
            return;
        }

        // Aguarda a resposta (bloqueio temporário máximo de 2 segundos)
        long startTime = millis();
        int replySize = 0;
        while (true) {
            replySize = UdpClient.parsePacket(); // Armazena a resposta em uma variável
            if (replySize > 0) {
                break; // Resposta recebida!
            }
            if (millis() - startTime > 2000) {
                Serial.println("Timeout: Nenhuma resposta do DNS público.");
                break;
            }
            delay(5);
        }

        // Se recebemos uma resposta, encaminha de volta para o cliente
        if (replySize > 0) {
            Serial.printf("Resposta recebida do DNS público (%d bytes). Enviando ao cliente...\n", replySize);
            int replyBytesRead = UdpClient.read(replyBuffer, min(replySize, MAX_DNS_PACKET_SIZE));
            if (replyBytesRead > 0) {
                UdpServer.beginPacket(remoteIP, remotePort);
                UdpServer.write(replyBuffer, replyBytesRead);
                if (UdpServer.endPacket()) {
                    Serial.println("Resposta encaminhada com sucesso para o cliente.");
                } else {
                    Serial.println("Falha ao enviar resposta de volta para o cliente.");
                }
            } else {
                Serial.println("Erro: Resposta vazia.");
            }
        }
        Serial.println("------------------------------------");
    }
}