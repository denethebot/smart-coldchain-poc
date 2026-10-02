import paho.mqtt.client as mqtt
import json
import time

BROKER = "broker.hivemq.com"
TOPIC = "coldchain-poc-test/shipments/s1/telemetry"
SAFE_MAX_TEMP = 8.0
excursion_start_time = None
BUFFER_HOURS = 8



def on_connect(client, userdata, flags, rc):
    print("Connected to broker, subscribing...")
    client.subscribe(TOPIC)


def on_message(client, userdata, msg):
    global excursion_start_time
    reading = json.loads(msg.payload.decode())
    print("Received:", reading)
    if reading["temperature"] > SAFE_MAX_TEMP:
         if excursion_start_time is None:
            excursion_start_time = time.time()
            print("⚠️ Excursion just started")
        
         hours_elapsed = (time.time() - excursion_start_time) / 3600
         time_left = BUFFER_HOURS - hours_elapsed
         print(f"⚠️ EXCURSION ONGOING — {time_left:.2f} hours remaining before critical")
    else:
        print("✅ Temperature within safe range")


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, 1883, 60)
client.loop_forever()
