import paho.mqtt.client as mqtt
import json
import time
from simulation.fake_fleet_data import storage_points, vehicles
from decision_engine.routing_client import get_travel_time_minutes

BROKER = "broker.hivemq.com"
TOPIC = "coldchain-poc-test/shipments/s1/telemetry"
SAFE_MAX_TEMP = 8.0
excursion_start_time = None
BUFFER_HOURS = 0.5


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
        print(
            f"⚠️ EXCURSION ONGOING — {time_left:.2f} hours remaining before critical")

        print("Checking recovery options...")
        candidates = []

        for point in storage_points:
            t = get_travel_time_minutes(
                reading["lat"], reading["lon"], point["lat"], point["lon"])
            if t is not None:
                candidates.append(
                    {"action": f"Divert to {point['id']}", "time": t})

        for v in vehicles:
            if v["spare_capacity"]:
                t = get_travel_time_minutes(
                    reading["lat"], reading["lon"], v["lat"], v["lon"])
                if t is not None:
                    candidates.append(
                        {"action": f"Rendezvous with {v['id']}", "time": t})

        time_left_minutes = time_left * 60
        feasible = [c for c in candidates if c["time"] < time_left_minutes]

        if not feasible:
            print("🚨 UNRECOVERABLE — no option is faster than time remaining")
        else:
            best = min(feasible, key=lambda c: c["time"])
            print(
                f"✅ RECOMMENDATION: {best['action']} — {best['time']:.1f} min (time left: {time_left_minutes:.1f} min)")

    else:
        excursion_start_time = None
        print("✅ Temperature within safe range")


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, 1883, 60)
client.loop_forever()
