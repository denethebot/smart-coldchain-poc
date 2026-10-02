import paho.mqtt.client as mqtt
import json

BROKER = "broker.hivemq.com"
TOPIC = "coldchain-poc-test/shipments/s1/telemetry"


def on_connect(client, userdata, flags, rc):
    print("Connected to broker, subscribing...")
    client.subscribe(TOPIC)


def on_message(client, userdata, msg):
    reading = json.loads(msg.payload.decode())
    print("Received:", reading)


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, 1883, 60)
client.loop_forever()
