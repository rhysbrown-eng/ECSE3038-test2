from datetime import datetime, timezone
from typing import Literal
import json

from fastapi import FastAPI, HTTPException, Response, Header, Query
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

import os
from dotenv import load_dotenv
from pymongo import MongoClient, ASCENDING
from pymongo.errors import DuplicateKeyError

import paho.mqtt.publish as publish

import secrets

load_dotenv()
client = MongoClient(os.getenv("MONGODB_URI"), tz_aware=True)
mqtt_name = os.getenv("MQTT_NAME")
db = client["test-2"]

fans = db["fans"]
readings = db["readings"]
settings = db["settings"]

fans.create_index([("mac", ASCENDING)], unique=True)

MQTT_BROKER = "135.232.203.144"

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
) # allows ui from laptop to access server. see tutorial 7 for details.

class Fan(BaseModel):
    mac: str
    room: str

class Reading(BaseModel):
    mac: str
    humidity: float = Field(ge=0, le=100)

class Settings(BaseModel):
    mode: Literal["auto", "on", "off"]
    max_humidity: float = Field(ge=0, le=100)

@app.post("/fans", status_code=201)
def create_fan(fan: Fan):
    new_fan = fan.model_dump()
    x_api_key = secrets.token_urlsafe(16)
    new_fan["X-API-Key"] = x_api_key

    try:
        fans.insert_one(new_fan)
    except DuplicateKeyError:
        raise HTTPException(status_code=409, detail=f"Fan with MAC {fan.mac} already exists big bro.")

    new_fan.pop("_id")

    return new_fan

@app.get("/fans")
def get_fans():
    cursor = fans.find({}, {"_id": 0, "X-API-Key": 0})
    return list(cursor)

@app.get("/fans/{mac}")
def get_fan(mac: str):
    fan = fans.find_one({"mac": mac}, {"_id": 0, "X-API-Key": 0})
    if fan is None:
        raise HTTPException(status_code=404, detail=f"Fan with MAC {mac} does not exist big bro.")
    return fan

@app.post("/readings", status_code=201)
def create_reading(reading: Reading, x_api_key: str | None = Header(default=None)):
    new_reading = reading.model_dump()

    if not x_api_key:
        raise HTTPException(status_code=401, detail=f"You don't have any credentials big bro. give credentials")

    fan = fans.find_one({"X-API-Key": x_api_key})

    if fan is None:
        raise HTTPException(status_code=401, detail=f"I never issued that key big bro.")

    if fan["mac"] != reading.mac:
        raise HTTPException(status_code=403, detail=f"That's not your credentials big bro.")

    new_reading["time"] = datetime.now(timezone.utc)
    readings.insert_one(new_reading)
    new_reading.pop("_id")
    return new_reading

@app.get("/readings")
def get_reading(mac: str | None = None, since: str | None = None,
                limit: int = Query(default=10, ge=1, le=100)):
    query = {}

    if mac:
        query["mac"] = mac

    if since:
        try:
            since_datetime = datetime.fromisoformat(since)
            query["time"] = {"$gte": since_datetime}
        except ValueError:
            raise HTTPException(status_code = 400, detail="Invalid format for time used big bro. Please use ISO format.")

    found_readings = readings.find(query).sort("time", -1).limit(limit)
    results = []
    for reading in found_readings:
        reading.pop("_id", None)
        results.append(reading)
        
    return results

@app.put("/fans/{mac}/settings")
def put_settings(mac: str, new_settings_body: Settings, response: Response):
    fan = fans.find_one({"mac": mac})
    if fan is None:
        raise HTTPException(status_code=404, detail=f"Fan with MAC {mac} does not exist big bro.")
    
    new_settings = new_settings_body.model_dump()
    new_settings["mac"] = mac

    result = settings.replace_one({"mac": mac}, new_settings, upsert=True)
    if result.upserted_id is not None:
        response.status_code = 201
    new_settings.pop("_id", None)

    try:
        payload = json.dumps({"mode": new_settings["mode"],
                              "max_humidity": new_settings["max_humidity"]})
        publish.single("ecse3038/" + mqtt_name + "/fan/settings", payload,
                       retain=True, hostname=MQTT_BROKER)
    except Exception as error:
        print("Could not publish the settings:", error)

    return new_settings

@app.delete("/fans/{mac}", status_code=204)
def delete_fan(mac: str, x_api_key: str | None = Header(default=None)):
    if not x_api_key or fans.find_one({"X-API-Key": x_api_key}) is None:
        raise HTTPException(status_code=401, detail=f"No valid credentials big bro.")

    fan = fans.find_one({"mac": mac})
    if fan is None:
        raise HTTPException(status_code=404, detail=f"Fan with MAC {mac} does not exist big bro.")

    if fan["X-API-Key"] != x_api_key:
        raise HTTPException(status_code=403, detail=f"That's not your credentials big bro.")

    fans.delete_one({"mac": mac})
    settings.delete_one({"mac": mac})
    return Response(status_code=204)