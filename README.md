# NeerNetra - IoT Water Quality Monitor (ESP32 + AWS + Node.js)

"नीर नेत्र" — paani ki nigraani karne wali aankh. Indicative readings only (pH, TDS, turbidity, temperature). Arsenic, lead, bacteria detect NAHI hote. Demo video mein ye limitation clearly bolo.

**Koi AWS CLI, SAM CLI, ya npm install install karne ki zaroorat nahi** — sab kuch AWS Console (browser) se hota hai. Lambda ka Node.js 22.x runtime mein AWS SDK v3 pehle se built-in hai, isliye code seedha paste-and-save ho jata hai.

## 1. Hardware wiring

Power: ESP32 USB/power bank se. Sensor modules 5V (ESP32 VIN pin) se. ADS1115 3.3V se.

| From | To | Note |
|---|---|---|
| ESP32 3V3 | ADS1115 VDD | ADS1115 3.3V pe chalao taaki I2C 3.3V rahe |
| ESP32 GND | ADS1115 GND, sab sensors GND | Common ground zaroori |
| ESP32 GPIO21 | ADS1115 SDA | I2C |
| ESP32 GPIO22 | ADS1115 SCL | I2C |
| ADS1115 ADDR | GND | Address 0x48 |
| ESP32 GPIO21/22 | OLED SDA/SCL, VCC 3V3 | Same I2C bus (0x3C) |
| ESP32 GPIO4 | DS18B20 data (yellow) | + 4.7k resistor between data and 3V3 |
| DS18B20 | red -> 3V3, black -> GND | |
| pH module Po | divider -> ADS1115 A0 | |
| TDS module A | divider -> ADS1115 A1 | |
| Turbidity module A | divider -> ADS1115 A2 | |
| pH / TDS / Turbidity VCC | ESP32 VIN (5V) | |

Voltage divider (har analog output ke liye): sensor out -> 10k -> node -> 20k -> GND; node ko ADS1115 channel se jodo. Isse 5V signal 3.3V ke andar rehta hai (ratio 0.667, firmware wapas 1.5 se multiply karta hai).

## 2. pH calibration
1. Probe saaf paani se dhoke pH 4.01 buffer mein daalo, 1 min ruko, Serial pe "V: ph=" voltage dekho -> `V_PH4`.
2. Dhokar pH 9.18 buffer mein -> `V_PH9`. Firmware mein update karo.
3. Probe hamesha geela rakho.

## 3. AWS setup — pura Console se (region: Mumbai / ap-south-1)

### 3.1 DynamoDB table
AWS Console -> DynamoDB -> Create table
- Table name: `Readings`
- Partition key: `device_id` (String)
- Sort key: `ts` (Number)
- Baaki default -> Create.

### 3.2 SNS alert topic
SNS -> Topics -> Create topic (type: Standard) -> name `NeerNetraAlerts`
- Create subscription -> Protocol: Email -> apna email daalo.
- Inbox mein "AWS Notification - Subscription Confirmation" email aayega, **Confirm subscription** click karo.
- Topic ka **ARN** copy kar ke rakho (ingest Lambda mein chahiye hoga).

### 3.3 Ingest Lambda (data aate hi classify + save + alert)
Lambda -> Create function -> Author from scratch
- Name: `neernetra-ingest`, Runtime: **Node.js 22.x**
- Create function ke baad, code editor mein `backend/ingest_lambda.js` ka pura content paste karo (file name `index.js` rakhna, ya `ingest_lambda.js` bana ke Lambda ke "Handler" setting mein `ingest_lambda.handler` likh dena), **Deploy** dabao.
- Configuration -> Environment variables -> Add:
  - `TABLE` = `Readings`
  - `ALERT_TOPIC` = (SNS topic ka ARN jo 3.2 mein copy kiya)
- Configuration -> Permissions -> role link pe click -> us IAM role mein ye policies attach karo: `AmazonDynamoDBFullAccess`, `AmazonSNSFullAccess` (hackathon ke liye quick tareeka; production mein scoped policy banana better hai).

### 3.4 API Lambda (dashboard ko data dega)
Lambda -> Create function -> Author from scratch
- Name: `neernetra-api`, Runtime: Node.js 22.x
- `backend/api_lambda.js` ka content paste karo, Handler `api_lambda.handler` rakho, Deploy.
- Environment variable: `TABLE` = `Readings`.
- Role mein `AmazonDynamoDBReadOnlyAccess` attach karo.

### 3.5 IoT Core — ESP32 se connect karne ke liye
1. IoT Core -> Connect -> Connect one device -> thing name `neernetra-01` -> platform/language chuno -> certificates **download** karo: device certificate, private key, aur **Amazon Root CA 1**. (Ye teeno `secrets.h` mein jaayenge.)
2. IoT Core -> Security -> Policies -> naya policy banao jo allow kare: `iot:Connect`, `iot:Publish`, `iot:Subscribe` on resource `*` (ya scoped: `arn:aws:iot:ap-south-1:ACCOUNT:topic/neernetra/*`) -> is policy ko upar wale certificate se attach karo (Certificates page -> cert select -> Policies -> Attach).
3. IoT Core -> Settings -> **Device data endpoint** copy karo (jaisa `xxxx-ats.iot.ap-south-1.amazonaws.com`) -> `secrets.h` mein `AWS_ENDPOINT`.

### 3.6 IoT Rule — MQTT message aate hi Lambda chalaye
IoT Core -> Message routing -> Rules -> Create rule
- Name: `NeerNetraIngestRule`
- SQL statement: `SELECT * FROM 'neernetra/+/data'`
- Action: Add action -> Lambda -> function select karo `neernetra-ingest` -> Add.

### 3.7 API Gateway — dashboard ko internet se Lambda tak pahunchaye
API Gateway -> Create API -> HTTP API -> Build
- Integration: Lambda -> `neernetra-api` select karo.
- Route: `GET /readings`.
- CORS: Configure -> Access-Control-Allow-Origin: `*`.
- Deploy karo (default stage theek hai) -> **Invoke URL** copy karo -> ye tumhara `API_URL` hai.

### 3.8 (Optional) AWS CloudShell
Console ke top-right mein CloudShell icon hota hai — browser ke andar hi terminal milta hai jisme AWS CLI pehle se installed hota hai, agar kabhi command line se kuch check karna pade, laptop pe kuch install kiye bina.

## 4. Firmware
1. Arduino IDE mein ESP32 board package + libraries install karo (neernetra.ino ke upar list hai).
2. `secrets.h.example` ko `secrets.h` banao, WiFi, endpoint (3.5.3) aur 3 certificates (3.5.1) paste karo.
3. Upload karo. Serial Monitor (115200) mein JSON dikhna chahiye. IoT Core -> MQTT test client mein `neernetra/#` subscribe karke verify karo ki data aa raha hai.

## 5. Dashboard
`dashboard/index.html` mein top ka `API_URL = ""` line dhoondo, 3.7 wala Invoke URL paste karo.
- Sirf dekhna ho: file double-click karo, browser mein khul jayega.
- Online link chahiye (judges ko dena): Amplify Console -> "Deploy without Git" -> `dashboard` folder zip karke upload karo, live link milega.

## 6. Hardware ke bina pipeline test
`backend/simulator.js` ek Node.js script hai jo fake readings AWS IoT pe bhejta hai. Isko chalane ke liye laptop pe Node.js + `npm install` chahiye hoga (ye ek hi jagah hai jaha local install lagta hai, baaki sab Console se hai):
```
cd backend
npm install
node simulator.js
```
Agar ye bhi avoid karna hai, to IoT Core -> MQTT test client mein manually ek JSON message publish kar ke bhi pipeline test kar sakte ho (topic: `neernetra/neernetra-01/data`).

## 7. Architecture
```
Sensors -> ADS1115 -> ESP32 -> MQTT (TLS) -> IoT Core -> Rule -> neernetra-ingest Lambda -> DynamoDB + SNS
Dashboard -> API Gateway -> neernetra-api Lambda -> DynamoDB
```

## 8. Bonus ideas
Bedrock se Hindi salah, GPS module + map, community reporting, MicroSD offline logging.
