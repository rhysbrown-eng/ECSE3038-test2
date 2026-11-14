# IoT-based Bathroom Fan Controller

## Overview

A bathroom extractor fan controller built from an ESP32 and a FastAPI backend.

An ESP32 with a DHT22 sensor sends a humidity reading to the API every 10 s, authenticated with a per-fan `X-API-Key`. The API stores fans, readings and fan settings in MongoDB Atlas. Someone sets a fan's `mode` (`auto`, `on` or `off`) and `max_humidity` through the API, and the API publishes those settings over MQTT, retained, to the board. An LED on the board stands for the fan: `on` and `off` force it, and `auto` turns it on when the humidity is above the limit. The board reports the fan's state and its online status back over MQTT.

## Setup 

### API

To set up the server, run the following commands:

1. SSH into your virtual machine:

```shell
ssh -i $HOME\.ssh\ecse3038-key.pem azureuser@<IP>
```

2. If a previous API is running on your machine, stop it:
   
```bash
sudo systemctl stop iot-api        
```

3. Clone repo, open the `api/` folder then create and activate the environment:
   
```bash
git clone https://github.com/rhysbrown-eng/ECSE3038-test2.git
cd ECSE3038-test2/api
python3 -m venv venv
source venv/bin/activate
python -m pip install -r requirements.txt
```

4. Create the `.env` file (it is never committed) with your `MONGODB_URI` and `MQTT_NAME`:
   
```bash
nano .env  
```

```
MONGODB_URI=<your Atlas connection string>
MQTT_NAME=<your mqtt name>
```

### ESP32
1. Open the folder containing `board/` in VS Code using PlatformIO.
2. Create the `secrets.h` file in `board/include/` (it is never committed).
3. Fill out the following information and save the file. Leave `DEVICE_KEY` empty for now, you get the key when you run the board:

```cpp
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL 6
#define DEVICE_KEY ""
```

## Running

### API

1. On the virtual machine, from within `ECSE3038-test2/api` with the environment active, start the server:

```bash
source venv/bin/activate
uvicorn app:app --host 127.0.0.1 --port 8000
```

2. After a code change, run `git pull`, press Ctrl+C, and start uvicorn again.

3. When you are finished, press Ctrl+C and restart the tutorial service:

```bash
sudo systemctl start iot-api
```

### Board

1. Build the project and start the Wokwi simulation from the VS Code window that has `board/` open.
2. Copy the MAC address from the Serial Monitor. A 401 response at this point is expected, since the board has no key yet.
3. Register the fan by sending `POST /fans` to the API with this body, and copy the `X-API-Key` in the response:

```json
{"mac": "<MAC>", "room": "<room>"}
```

4. Paste the key between the quotes of `DEVICE_KEY` in `board/include/secrets.h`, then build and start the simulation again. Readings are now accepted every 10 s.
5. Set the fan with `PUT /fans/<MAC>/settings`:

```json
{"mode": "auto", "max_humidity": 65}
```

6. Click the DHT22 in Wokwi and drag the humidity above 65. The LED turns on, and `fan/state` updates on the MQTT page.