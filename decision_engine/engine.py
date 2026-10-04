import paho.mqtt.client as mqtt
import json
import time

from simulation.fake_fleet_data import storage_points, vehicles
from decision_engine.routing_client import get_travel_time_minutes

BROKER = "broker.hivemq.com"
TELEMETRY_TOPIC = "coldchain-poc-test/shipments/s1/telemetry"
STATUS_TOPIC = "coldchain-poc-test/shipments/s1/status"

SAFE_MAX_TEMP = 8.0
BUFFER_HOURS = 8  # set back to 8 for normal demo, lower temporarily only for testing

excursion_start_time = None


def publish_status(client, status_type, message, extra=None):
    payload = {
        "type": status_type,  # "info", "excursion", "recommendation", "unrecoverable"
        "message": message,
        "timestamp": time.time()
    }
    if extra:
        payload.update(extra)
    client.publish(STATUS_TOPIC, json.dumps(payload))


def on_connect(client, userdata, flags, rc):
    print("Connected to broker, subscribing...")
    client.subscribe(TELEMETRY_TOPIC)


def on_message(client, userdata, msg):
    global excursion_start_time
    reading = json.loads(msg.payload.decode())
    print("\n" + "=" * 50)
    print("Received:", reading)

    if reading["temperature"] > SAFE_MAX_TEMP:
        if excursion_start_time is None:
            excursion_start_time = time.time()
            print("⚠️ Excursion just started")
            publish_status(client, "excursion", f"Excursion detected on {reading['shipment_id']}", {
                           "temp": reading["temperature"]})

        hours_elapsed = (time.time() - excursion_start_time) / 3600
        time_left = BUFFER_HOURS - hours_elapsed
        time_left_minutes = time_left * 60

        print(
            f"⚠️ EXCURSION ONGOING — {time_left:.2f} hours remaining before critical")
        publish_status(
            client, "info", f"Time remaining: {time_left_minutes:.1f} min")

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

        feasible = [c for c in candidates if c["time"] < time_left_minutes]

        if not feasible:
            print("🚨 UNRECOVERABLE — no option is faster than time remaining")
            publish_status(client, "unrecoverable",
                           "No feasible recovery option found")
        else:
            best = min(feasible, key=lambda c: c["time"])
            print(
                f"✅ RECOMMENDATION: {best['action']} — {best['time']:.1f} min (time left: {time_left_minutes:.1f} min)")
            publish_status(client, "recommendation",
                           best["action"], {"eta_min": best["time"]})

    else:
        excursion_start_time = None
        print("✅ Temperature within safe range")
        publish_status(client, "info", "Temperature within safe range")


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, 1883, 60)
client.loop_forever()
