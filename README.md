# IoT Temperature Monitoring System

# Overview
A full-stack IoT system featuring a FastAPI backend with MongoDB database and an ESP32 client that logs humidity data after being authenticated via custom API headers. The client can also receive real-time remote configuration of humidity limits before alarms go off over MQTT.

## Setup 

### API

To set up and run the server run the following commands:

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
cd ECSE3038-mock-test2/api
python3 -m venv venv
source venv/bin/activate
python -m pip install -r requirements.txt
```

4. Edit the `.env` file to have your `MONGODB_URI` and `MQTT_NAME`:
   
```bash
nano .env  
```

5. Run the API on the virtual machine:
   
```bash
uvicorn app:app --host 127.0.0.1 --port 8000
```

### ESP32
1. Open the `board/` folder in VS Code using PlatformIO.
2. Navigate to the `secrets.h` file in `board/include/`.
3. Fill out the following information and save the file:

```cpp
#define WIFI_SSID "Your_SSID"
#define WIFI_PASSWORD "Your_Password"
#define WIFI_CHANNEL 6
#define DEVICE_KEY "your-registered-api-key-here"
```

4. Click the Build (checkmark) and Upload (right arrow) buttons to flash the ESP32, or run it directly in your Wokwi simulation.

