#include <FS.h>
#include <EEPROM.h>
#include <Regexp.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <RemoteDebug.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

#define HOSTNAME       "LOCK2MQTT"
#define OTA_PASSWORD   "password"
#define HTTP_PORT      80
#define WIFI_TIMEOUT   30000
#define TIME_ZONE      8;

const char* update_path = "/update";                                          //OTA页面地址
const char* update_username = "admin";                                        //OTA用户名
const char* update_password = "admin";                                     //OTA密码

static unsigned long last_loop;

long LAST_RECONNECT_ATTEMPT = 0;

char MQTT_HOST[64] = "";
char MQTT_PORT[6]  = "";
char MQTT_USER[32] = "";
char MQTT_PASS[32] = "";

String infoBuffer;

RemoteDebug Debug;
WiFiClient espClient;
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
PubSubClient mqtt_client(espClient);

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
}

void configModeCallback(WiFiManager *myWiFiManager) {
    Serial.println(F("Entered config mode"));
    Serial.println(WiFi.softAPIP());
    Serial.println(myWiFiManager->getConfigPortalSSID());
}

bool mqtt_reconnect() {
    while (!mqtt_client.connected()) {
        if (mqtt_client.connect(HOSTNAME, MQTT_USER, MQTT_PASS)) {
            Serial.println(F("MQTT connected!"));
            mqtt_client.publish("LOCK2MQTT/status", "alive");
            mqtt_client.subscribe("LOCK2MQTT/set");
        }
        else {
            Serial.print(F("MQTT Connection failed: rc="));
            Serial.println(mqtt_client.state());
            Serial.println(F(" Retrying in 5 seconds"));
            delay(5000);
        }
    }
    return true;
}

String read_eeprom(int offset, int len) {
    String res = "";
    for (int i = 0; i < len; ++i) {
        res += char(EEPROM.read(i + offset));
    }
    Serial.print(F("read_eeprom(): "));
    Serial.println(res.c_str());
     return res;
}

void write_eeprom(int offset, int len, String value) {
    Serial.print(F("write_eeprom(): "));
    Serial.println(value.c_str());
    for (int i = 0; i < len; ++i) {
        if ((unsigned)i < value.length()) {
            EEPROM.write(i + offset, value[i]);
        }
        else {
            EEPROM.write(i + offset, 0);
        }
    }
}

bool shouldSaveConfig = false;

void save_wifi_config_callback () {
    shouldSaveConfig = true;
}

void setup_ota() {
    httpUpdater.setup(&httpServer, update_path, update_username, update_password);
    httpServer.begin();
    //Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\n", host, update_path, update_username, update_password);
}

void setup_mdns() {
    bool mdns_result = MDNS.begin(HOSTNAME);
}

//获取串口数据中含有method字符的json，基本上涵盖了网关状态、红外收发编码、蓝牙事件、指示灯状态等信息
//我手里没有别的蓝牙设备，没办法抓数据，猜测如果单纯作为蓝牙网关的话只匹配ble_event就够了
String get_json(String data) {
    MatchState parse_result;
    char match_result[data.length() - 30];
    char buf[data.length() + 1];
    data.toCharArray(buf, data.length() + 1);
    parse_result.Target(buf);
    if (parse_result.Match("\{.*method.*\}") == REGEXP_MATCHED) {
      return String(parse_result.GetMatch(match_result));
    }
    else {
      return "UNKNOWN";
    }
}

//十六进制字符串转整数
//switch只支持int类型
int hex_to_int(String paload) {
    char* str = const_cast<char *>(paload.c_str());
    return (int) strtol(str, 0, 16);
}

String hex_to_time(String hex_timestamp) {
  
    //不懂位运算，用字符串简单粗暴解决
    String str = hex_timestamp.substring(6, 8) + hex_timestamp.substring(4, 6) + hex_timestamp.substring(2, 4) + hex_timestamp.substring(0, 2);
    unsigned long timestamp = hex_to_dec(str) + 3600 * TIME_ZONE;
    char time[32];
    
    //把时间戳格式化成yyyy-mm-dd hh:mm:ss
    sprintf(time, "%02d-%02d-%02d %02d:%02d:%02d", year(timestamp), month(timestamp), day(timestamp), hour(timestamp), minute(timestamp), second(timestamp));
    return time;
}

//十六进制字符串转十进制
unsigned int hex_to_dec(String hexString) {
  unsigned int decValue = 0;
  int nextInt;
  for (int i = 0; i < hexString.length(); i++) {
    nextInt = int(hexString.charAt(i));
    if (nextInt >= 48 && nextInt <= 57) nextInt = map(nextInt, 48, 57, 0, 9);
    if (nextInt >= 65 && nextInt <= 70) nextInt = map(nextInt, 65, 70, 10, 15);
    if (nextInt >= 97 && nextInt <= 102) nextInt = map(nextInt, 97, 102, 10, 15);
    nextInt = constrain(nextInt, 0, 15);
    decValue = (decValue * 16) + nextInt;
  }
  return decValue;
}

void parse_json(String json) {
  
    //米家智能锁的json类似这样：{"id":1518998071,"method":"_async.ble_event","params":{"dev":{"did":"1011078646","mac":"AA:BB:CC:DD:EE:FF","pdid":794},"evt":[{"eid":7,"edata":"0036f6e45e"}],"frmCnt":97,"gwts":2362}}
    //状态信息在这里：{"eid":7,"edata":"0036f6e45e"}
    char buf[json.length() + 1];
    json.toCharArray(buf, json.length() + 1);
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, buf);
    JsonObject params = doc["params"];
    if (!error) {

        //提取eid
        int eid = params["evt"][0]["eid"];

        //提取edata
        String edata = params["evt"][0]["edata"];

        //智能锁状态
        if (eid == 7) {
            String lock_state = edata.substring(0, 2);
            String time = hex_to_time(edata.substring(2, 10));
            String state_msg = "";
            switch(hex_to_int(lock_state)) {
                case 0: {state_msg.concat("开门");} break;
                case 1: {state_msg.concat("关门");} break;
                case 2: {state_msg.concat("超时未关");} break;
                case 3: {state_msg.concat("敲门");} break;
                case 4: {state_msg.concat("撬门");} break;
                case 5: {state_msg.concat("门卡住");} break;
                default: Debug.println(lock_state); break;
            }
            String state = "{\"state\":\"" + state_msg + "\",\"time\":\"" + String(time) + "\"}";
            String state_raw = "{\"state\":\"" + lock_state + "\",\"time\":\"" + String(time) + "\"}";
            Debug.println(state);
            mqtt_client.publish("LOCK2MQTT/state", state.c_str(), true);
            mqtt_client.publish("LOCK2MQTT/state/raw", state_raw.c_str(), true);
            //合并门锁状态 用于HA binary_sensor
            if (lock_state == "00" || lock_state == "02" || lock_state == "05") {
              mqtt_client.publish("LOCK2MQTT/lock/state", "unlock", true);
            }
            else if (lock_state == "01" || lock_state == "03" || lock_state == "04") {
              mqtt_client.publish("LOCK2MQTT/lock/state", "lock", true);
            }
        }

        //智能锁事件
        if (eid == 11) {
            String method = edata.substring(0, 1);
            String action = edata.substring(1, 2);
            String key_id = edata.substring(2, 10);
            String time = hex_to_time(edata.substring(10, 18));
            String event_msg,alert_msg,alert = "";
            //判断开锁方式
            switch(hex_to_int(method)) {
                case 0: {event_msg.concat("蓝牙");} break;
                case 1: {event_msg.concat("密码");} break;
                case 2: {event_msg.concat("指纹");} break;
                case 3: {event_msg.concat("应急钥匙");} break;
                case 4: {event_msg.concat("转盘方式");} break;
                case 5: {event_msg.concat("NFC");} break;
                case 6: {event_msg.concat("一次性密码");} break;
                case 7: {event_msg.concat("双重验证");} break;
                case 10: {event_msg.concat("");} break; //a 人工
                case 11: {event_msg.concat("自动");} break; //b
                case 15: {event_msg.concat("异常");} break; //f
                default: Debug.println(method); break;
            }
            //判断门锁动作
            switch(hex_to_int(action)) {
                case 0: {event_msg.concat("开门");} break; //门外开锁
                case 1: {event_msg.concat("上提把手锁门");} break; //门外上锁
                case 2: {event_msg.concat("反锁");} break; //门内反锁
                case 3: {event_msg.concat("解除反锁");} break;
                case 4: {event_msg.concat("室内开门");} break; //门内开锁
                case 5: {event_msg.concat("门内上锁");} break;
                case 6: {event_msg.concat("开启童锁");} break;
                case 7: {event_msg.concat("关闭童锁");} break;
                case 15: {event_msg.concat("");} break; //f异常
                default: Debug.println(action); break;
            }
            //判断是否有外部触发的异常
            if (key_id.substring(2, 8) == "00dec0") {
                alert.concat(key_id.substring(1, 2));
                switch(hex_to_int(alert)) {
                    case 0: {alert_msg.concat("密码多次验证失败");} break; //密码频繁开锁
                    case 1: {alert_msg.concat("指纹多次验证失败");} break; //指纹频繁开锁
                    case 2: {alert_msg.concat("密码输入超时");} break;
                    case 3: {alert_msg.concat("撬锁");} break;
                    case 4: {alert_msg.concat("重置按键按下");} break;
                    case 5: {alert_msg.concat("有人撬锁或钥匙未拔");} break; //钥匙频繁开锁
                    case 6: {alert_msg.concat("钥匙孔异物");} break;
                    case 7: {alert_msg.concat("钥匙未取出");} break;
                    case 8: {alert_msg.concat("NFC设备多次验证失败");} break; //NFC频繁开锁
                    case 9: {alert_msg.concat("超时未按要求上锁");} break;
                    default: Debug.println(edata); break;
                }
            String msg = "{\"alert\":\"" + alert_msg + "\",\"time\":\"" + String(time) + "\"}";
            String msg_raw = "{\"alert\":\"" + alert + "\",\"time\":\"" + String(time) + "\"}";
            Debug.println(msg);
            mqtt_client.publish("LOCK2MQTT/alert", msg.c_str(), true);

            //原始数据，玩nodered可能用得到，不需要的可以注释掉
            mqtt_client.publish("LOCK2MQTT/alert/raw", msg_raw.c_str(), true);
            }
            //判断是否有内部触发的异常
            else if (key_id.substring(2, 8) == "10dec0") {
                alert.concat(key_id.substring(1, 2));
                switch(hex_to_int(alert)) {
                      case 0: {alert_msg.concat("电量低于10%");} break;
                      case 1: {alert_msg.concat("电量低于5%");} break;
                      case 2: {alert_msg.concat("指纹传感器异常");} break;
                      default: Debug.println(edata); break;
                }
            String msg = "{\"alert\":\"" + alert_msg + "\",\"time\":\"" + String(time) + "\"}";
            String msg_raw = "{\"alert\":\"" + alert + "\",\"time\":\"" + String(time) + "\"}";
            Debug.println(msg);
            mqtt_client.publish("LOCK2MQTT/alert", msg.c_str(), true);

            //原始数据，玩nodered可能用得到，不需要的可以注释掉
            mqtt_client.publish("LOCK2MQTT/alert/raw", msg_raw.c_str());
            }
            //无异常输出事件消息
            else {
              
                // 作废，改由ha端处理
                // 判断事件操作者
                // 00000000:锁的管理员
                // 01000180:第一个密码
                // 02000180:第一个指纹
                // if (key_id == "00000000" || key_id == "01000180" || key_id == "02000180") {
                //     event_msg = "我 " + event_msg;
                // }
                // if (key_id == "03000180") {
                //     event_msg = "老婆 " + event_msg;
                // }

                String msg = "{\"event\":\"" + event_msg + "\",\"key\":\"" + key_id + "\",\"time\":\"" + String(time) + "\"}";
                String msg_raw = "{\"method\":\"" + method + "\",\"action\":\"" + action + "\",\"key\":\"" + key_id + "\",\"time\":\"" + String(time) + "\"}";
                Debug.println(msg);
                mqtt_client.publish("LOCK2MQTT/event", msg.c_str(), true);

                //原始数据，玩nodered可能用得到，不需要的可以注释掉
                mqtt_client.publish("LOCK2MQTT/event/raw", msg_raw.c_str(), true);
            }
        }
        
        //智能锁电量事件
        if (eid == 4106) {
            String battery_state = String(hex_to_dec(edata.substring(0, 2)));
            String battery_msg = "{\"power\":\"" + battery_state + "\"}";
            mqtt_client.publish("LOCK2MQTT/battery", battery_msg.c_str(), true);
        }
        
        //智能锁锁舌事件
        if (eid == 4110) {
            String bolt_state = "";
            switch(hex_to_int(edata)) {
                case 0: {bolt_state.concat("所有锁舌收回");} break;
                case 4: {bolt_state.concat("斜舌弹出");} break;
                case 5: {bolt_state.concat("方舌/斜舌弹出");} break;
                case 6: {bolt_state.concat("呆舌/斜舌弹出");} break;
                case 7: {bolt_state.concat("所有锁舌弹出");} break;
                default: Debug.println(edata); break;
            }
            String bolt_state_msg = "{\"state\":\"" + bolt_state + "\"}";
            String bolt_state_msg_raw = "{\"state\":\"" + edata + "\"}";
            mqtt_client.publish("LOCK2MQTT/bolt", bolt_state_msg.c_str(), true);

            //原始数据，玩nodered可能用得到，不需要的可以注释掉
            mqtt_client.publish("LOCK2MQTT/bolt/raw", bolt_state_msg_raw.c_str(), true);
            Debug.println(bolt_state_msg.c_str());
        }

        //貌似是温湿度计，一种是圆的一种是方的
        //尝试发送一下
        if (eid == 4102 || eid == 4109) {
            String temp_msg_raw = "{\"eid\":\"" + String(eid) + "\",\"state\":\"" + edata + "\"}";

            //具体啥格式咱也不清楚，原样发出去给玩nodered的大神们
            //看完nodered流后猜测edata前半部分是温度，后半部分是湿度，高低位互换后再hex2dec，如果能确定的话，可以在这里处理
            mqtt_client.publish("LOCK2MQTT/temp/raw", temp_msg_raw.c_str(), true);
        }
    }
    else {
        Debug.printf("deserializeJson failed: %s\n", error.c_str());
        Debug.printf("Json data: %s\n", buf);
    }
}

void mqtt_publish(String topic, String payload,boolean retained) {
    mqtt_client.publish(topic.c_str(), payload.c_str(), retained);
}

void setup() {
    //尝试解决串口乱码
    //关闭串口输出
    Serial.begin(115200, SERIAL_8N1, SERIAL_RX_ONLY);
    Serial.setDebugOutput (false);
    //接收缓冲区增大一倍
    Serial.setRxBufferSize (512);
    //超时时间减半
    Serial.setTimeout(500);
    
    EEPROM.begin(512);
    Debug.begin("LOCK2MQTT");
    Debug.setResetCmdEnabled(true);

    String settings_available = read_eeprom(134, 1);
    if (settings_available == "1") {
        read_eeprom(0, 64).toCharArray(MQTT_HOST, 64);   // * 0-63
        read_eeprom(64, 6).toCharArray(MQTT_PORT, 6);    // * 64-69
        read_eeprom(70, 32).toCharArray(MQTT_USER, 32);  // * 70-101
        read_eeprom(102, 32).toCharArray(MQTT_PASS, 32); // * 102-133
    }
    WiFiManagerParameter CUSTOM_MQTT_HOST("host", "MQTT服务器","", 64);
    WiFiManagerParameter CUSTOM_MQTT_PORT("port", "MQTT端口", "", 6);
    WiFiManagerParameter CUSTOM_MQTT_USER("user", "MQTT账号","", 32);
    WiFiManagerParameter CUSTOM_MQTT_PASS("pass", "MQTT密码","", 32);

    WiFiManager wifiManager;

    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setConfigPortalTimeout(WIFI_TIMEOUT);
    wifiManager.setSaveConfigCallback(save_wifi_config_callback);

    wifiManager.addParameter(&CUSTOM_MQTT_HOST);
    wifiManager.addParameter(&CUSTOM_MQTT_PORT);
    wifiManager.addParameter(&CUSTOM_MQTT_USER);
    wifiManager.addParameter(&CUSTOM_MQTT_PASS);

    String ssid = "LOCK2MQTT_" + String(ESP.getChipId()).substring(0, 4);
    if (!wifiManager.autoConnect(ssid.c_str(), NULL)) {
        Serial.println(F("Failed to connect to WIFI and hit timeout"));
        ESP.reset();
        delay(WIFI_TIMEOUT);
    }

    if (settings_available != "1") {
    strcpy(MQTT_HOST, CUSTOM_MQTT_HOST.getValue());
    strcpy(MQTT_PORT, CUSTOM_MQTT_PORT.getValue());
    strcpy(MQTT_USER, CUSTOM_MQTT_USER.getValue());
    strcpy(MQTT_PASS, CUSTOM_MQTT_PASS.getValue());
    }

    if (shouldSaveConfig) {
        Serial.println(F("Saving WiFiManager config"));
        write_eeprom(0, 64, MQTT_HOST);   // * 0-63
        write_eeprom(64, 6, MQTT_PORT);   // * 64-69
        write_eeprom(70, 32, MQTT_USER);  // * 70-101
        write_eeprom(102, 32, MQTT_PASS); // * 102-133
        write_eeprom(134, 1, "1");        // * 134 --> always "1"
        EEPROM.commit();
    }

    setup_ota();
    setup_mdns();

    mqtt_client.setServer(MQTT_HOST, atoi(MQTT_PORT));
    mqtt_client.setCallback(mqtt_callback);

    Serial.printf("MQTT active: %s:%s\n", MQTT_HOST, MQTT_PORT);
}

void loop() {
    last_loop = millis();

    Debug.handle();
    httpServer.handleClient();

    if (!mqtt_client.connected()) {
        long now = millis();
        if (now - LAST_RECONNECT_ATTEMPT > 5000) {
            LAST_RECONNECT_ATTEMPT = now;
            if (mqtt_reconnect()) {
                LAST_RECONNECT_ATTEMPT = 0;
            }
        }
    }
    else {
        mqtt_client.loop();
    }

    while (Serial.available()){
        //只匹配含有method的json
        String result = get_json(Serial.readStringUntil('\n'));
        if ( result != "UNKNOWN" ){
          
            //处理数据
            parse_json(result);
            
            //通过mqtt发送原始json数据，玩nodered的朋友应该用得上
            //PubSubClient缓冲区大小有限制，有些数据可以在日志里看到但是发送不出去
            //超长的数据基本上没啥用，如果想要的话可以通过修改PubSubClient增大缓冲区解决
            mqtt_client.publish("LOCK2MQTT/json", result.c_str(), false);
            
            //telnet到8266的ip可以查看日志
            Debug.println(result);
            }
        else {
            //Debug.println("UNKNOWN");
        }
    }
}