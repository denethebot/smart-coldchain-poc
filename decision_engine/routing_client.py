import requests

OSRM_BASE_URL = "https://router.project-osrm.org"


def get_travel_time_minutes(origin_lat, origin_lon, dest_lat, dest_lon):

    # Returns driving travel time in minutes between two points, using OSRM.

    url = f"{OSRM_BASE_URL}/route/v1/driving/{origin_lon},{origin_lat};{dest_lon},{dest_lat}"
    params = {"overview": "false"}

    response = requests.get(url, params=params)
    data = response.json()

    if data.get("code") != "Ok":
        print("OSRM error:", data)
        return None

    duration_seconds = data["routes"][0]["duration"]
    return duration_seconds / 60  # convert to minutes

