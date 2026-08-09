// XIAO ESP32C6 x4 - painlessMesh 체인 릴레이 (A -> B -> C -> D)
//
// 4대 모두 이 파일 그대로 쓰되, 보드마다 MY_ROLE 한 줄만 바꿔서 업로드한다.
// A보드 시리얼 모니터에 문자를 입력하면 B, C를 순서대로 거쳐 D의 시리얼
// 모니터에 최종 도착 메시지가 출력된다.
//
// painlessMesh 자체는 플러딩 기반이라 전체 노드에 브로드캐스트되지만,
// 메시지 안에 "to"(다음 목적지 역할)를 넣어서 해당 역할이 아닌 보드는
// 무시하고, 맞는 보드만 처리 후 다음 역할로 다시 보내는 방식으로
// A-B-C-D 순서를 흉내낸다.
//
// [내결함성] 각 보드는 2초마다 "나 살아있음(역할)" 하트비트를 브로드캐스트하고,
// 서로 최근 5초 안에 하트비트를 받은 역할만 "살아있다"고 판단한다. 메시지를
// 넘길 때는 고정된 다음 역할이 아니라 "역할 순서(A-B-C-D)상 나보다 뒤에 있는,
// 현재 살아있는 가장 가까운 역할"을 찾아서 보낸다. 즉 B가 꺼져 있으면 A가
// 자동으로 C에게 바로 보내고, C·D만 살아있으면 A가 직접 D로 보낸다. 꺼졌던
// 보드가 다시 켜지면 하트비트가 재개되면서 별도 조작 없이 자동으로 경로에
// 다시 포함된다. 살아있는 다음 역할이 아무도 없으면 지금 처리 중인 보드가
// 스스로 "최종 도착"으로 처리한다.

#include "painlessMesh.h"
#include "mesh_secrets.h"

#define MESH_PREFIX "groundingMesh"
#define MESH_PASSWORD "grounding1234"
#define MESH_PORT 5555

// ★ 보드마다 이 줄만 바꿔서 업로드: 'A', 'B', 'C', 'D' 중 하나
#define MY_ROLE 'D'

// D는 체인의 마지막 노드이면서, 동시에 mesh <-> grounding MQTT 브로커를
// 잇는 브릿지 역할도 겸한다. mesh 내부의 모든 브로드캐스트(하트비트/체인
// 메시지)는 어차피 모든 노드에 도달하므로, D는 자기 앞으로 온 메시지뿐
// 아니라 지나가는 모든 메시지를 관찰해 MQTT로 그대로 흘려보낸다.
// D가 꺼지면 체인 자체는 C가 새 최종 목적지가 되어 계속 동작하지만,
// 그 사실을 MQTT/대시보드로 알려줄 브릿지가 없으므로 대시보드는 멈춘다.
#if MY_ROLE == 'D'
#include <PubSubClient.h>
WiFiClient bridgeWifiClient;
PubSubClient mqttClient(bridgeWifiClient);
bool bridgeAliveState[4] = {false, false, false, true}; // A,B,C,D (D=나 자신)
#endif

#define ALIVE_TIMEOUT_MS 5000
#define HEARTBEAT_INTERVAL_MS 2000

const char ROLE_ORDER[] = {'A', 'B', 'C', 'D'};
const int ROLE_COUNT = 4;

painlessMesh mesh;
Scheduler userScheduler;
Task taskHeartbeat(HEARTBEAT_INTERVAL_MS, TASK_FOREVER, []() {
  String hb = "{\"hb\":\"" + String(MY_ROLE) + "\"}";
  mesh.sendBroadcast(hb);
});

unsigned long lastSeen[ROLE_COUNT] = {0, 0, 0, 0}; // ROLE_ORDER와 같은 순서

int roleIndex(char role) {
  for (int i = 0; i < ROLE_COUNT; i++) {
    if (ROLE_ORDER[i] == role) return i;
  }
  return -1;
}

bool isAlive(int idx) {
  if (lastSeen[idx] == 0) return false; // 하트비트를 한 번도 못 받음
  return (millis() - lastSeen[idx]) < ALIVE_TIMEOUT_MS;
}

// role 다음 순서부터 훑어서 현재 살아있는 가장 가까운 역할을 찾는다.
// 아무도 없으면 0을 반환 (= role이 최종 목적지).
char nextAliveRoleAfter(char role) {
  int start = roleIndex(role);
  for (int i = start + 1; i < ROLE_COUNT; i++) {
    if (isAlive(i)) return ROLE_ORDER[i];
  }
  return 0;
}

// 최소한의 수작업 파싱. 메시지 형식이 고정되어 있으므로 ArduinoJson 없이 처리.
String extractField(const String &msg, const String &key) {
  String needle = "\"" + key + "\":\"";
  int start = msg.indexOf(needle);
  if (start == -1) return "";
  start += needle.length();
  int end = msg.indexOf("\"", start);
  if (end == -1) return "";
  return msg.substring(start, end);
}

// payload를 이어받아, 지금 살아있는 다음 역할로 넘기거나(있으면),
// 없으면 지금 노드에서 "최종 도착"으로 처리한다.
void relayOrArrive(const String &payload, const String &pathSoFar) {
  char next = nextAliveRoleAfter(MY_ROLE);
  if (next == 0) {
    Serial.printf("도착! 경로 %s : %s\n", pathSoFar.c_str(), payload.c_str());
#if MY_ROLE == 'D'
    String finalJson = "{\"path\":\"" + pathSoFar + "\",\"msg\":\"" + payload + "\"}";
    mqttClient.publish("grounding/mesh/final", finalJson.c_str(), true);
#endif
    return;
  }
  String json = "{\"to\":\"" + String(next) + "\",\"path\":\"" + pathSoFar + "\",\"msg\":\"" + payload + "\"}";
  mesh.sendBroadcast(json);
  Serial.printf("[%c] -> %c 전달: %s\n", MY_ROLE, next, payload.c_str());
}

#if MY_ROLE == 'D'
// D는 브로드캐스트되는 모든 메시지를 어차피 다 받으므로(자기 앞이 아니어도),
// 대시보드용으로 그대로 MQTT에 흘려보낸다.
void publishStatus(char role, bool online) {
  String topic = "grounding/mesh/" + String(role) + "/status";
  mqttClient.publish(topic.c_str(), online ? "online" : "offline", true);
}

void checkAliveTransitions() {
  for (int i = 0; i < ROLE_COUNT; i++) {
    if (ROLE_ORDER[i] == MY_ROLE) continue; // 나(D) 자신은 아래서 접속 시 한 번만 처리
    bool alive = isAlive(i);
    if (alive != bridgeAliveState[i]) {
      bridgeAliveState[i] = alive;
      publishStatus(ROLE_ORDER[i], alive);
    }
  }
}
Task taskCheckAlive(1000, TASK_FOREVER, &checkAliveTransitions);

void connectBridge() {
  mesh.stationManual(WIFI_SSID, WIFI_PASS);
  mesh.setRoot(true);
  mesh.setContainsRoot(true);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
}

void ensureMqttConnected() {
  if (mqttClient.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return; // 팀 WiFi에 아직 연결 중
  if (mqttClient.connect("grounding-mesh-bridge")) {
    Serial.println("MQTT 브릿지 연결됨");
    publishStatus('D', true);
  }
}
#endif

void receivedCallback(uint32_t from, String &msg) {
  String hb = extractField(msg, "hb");
  if (hb.length() > 0) {
    int idx = roleIndex(hb.charAt(0));
    if (idx != -1) lastSeen[idx] = millis();
    return;
  }

  String to = extractField(msg, "to");
  if (to.length() == 0) return; // 형식에 맞지 않는 메시지

  String path = extractField(msg, "path");
  String payload = extractField(msg, "msg");

#if MY_ROLE == 'D'
  String hopJson = "{\"path\":\"" + path + "\",\"to\":\"" + to + "\",\"msg\":\"" + payload + "\"}";
  mqttClient.publish("grounding/mesh/hop", hopJson.c_str());
#endif

  if (to.charAt(0) != MY_ROLE) return; // 내 차례가 아니면 여기서 멈춤 (브릿지 관찰은 이미 끝)

  String newPath = path + "-" + String(MY_ROLE);
  Serial.printf("[%c] 수신 (경로 %s): %s\n", MY_ROLE, newPath.c_str(), payload.c_str());
  relayOrArrive(payload, newPath);
}

void setup() {
  Serial.begin(115200);
  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  userScheduler.addTask(taskHeartbeat);
  taskHeartbeat.enable();
#if MY_ROLE == 'D'
  connectBridge();
  userScheduler.addTask(taskCheckAlive);
  taskCheckAlive.enable();
#endif
  Serial.printf("역할 %c 준비 완료. nodeId=%u\n", MY_ROLE, mesh.getNodeId());
  if (MY_ROLE == 'A') {
    Serial.println("시리얼 모니터에 문자를 입력하고 Enter 를 누르세요 (예: hi)");
  }
}

void loop() {
  mesh.update();
#if MY_ROLE == 'D'
  ensureMqttConnected();
  mqttClient.loop();
#endif

  if (MY_ROLE == 'A' && Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      relayOrArrive(line, "A");
    }
  }
}
