import paho.mqtt.client as mqtt
import json
import time
import random

BROKER = "broker.hivemq.com"
TOPIC = "coldchain-poc-test/shipments/s1/telemetry"

client = mqtt.Client()
client.connect(BROKER, 1883, 60)

temp = 5.0  # starting temp, within safe range (2-8°C)

print("Publisher started. Sending fake readings every 5 seconds...")

while True:
    # simulate a slow temperature rise (an excursion developing)
    temp += random.uniform(0.3, 0.8)

    reading = {
        "shipment_id": "s1",
        "temperature": round(temp, 2),
        "lat": 12.9716 + random.uniform(-0.01, 0.01),
        "lon": 77.5946 + random.uniform(-0.01, 0.01),
        "timestamp": time.time()
    }

    client.publish(TOPIC, json.dumps(reading))
    print("Published:", reading)

    time.sleep(5)
