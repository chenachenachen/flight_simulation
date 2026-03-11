#!/usr/bin/env python3
from posix import listdir
import sys
import json
import socket
import time
import os
import math
import copy

# 尝试加载 XPC
try:
    import xpc
    print("✓ XPC loaded")
except ImportError:
    print("✗ Error: xpc.py not found")

# 加载 BlueSky 库
bluesky_path = os.path.join(os.path.dirname(__file__), 'Bluesky')
if os.path.exists(bluesky_path) and bluesky_path not in sys.path:
    sys.path.insert(0, bluesky_path)

try:
    import bluesky as bs
    from bluesky import stack
    import importlib
    Client = importlib.import_module("bluesky.network.client").Client
    TrafficProxy = importlib.import_module("bluesky.core.trafficproxy").TrafficProxy
except ImportError:
    sys.exit(1)

# =========================================================================
#  X-Plane AI slots configuration
# =========================================================================
XP_SLOT_CONFIG = {
    "ROTOR": [1, 2],          # 槽位 1, 2: Sikorsky S-76C
    "UAV":   [3, 4],          # 槽位 3, 4: Cirrus Vision SF50 (小型替身)
    "HEAVY": [5, 6],          # 槽位 5, 6: Airbus A330-300
    "DEFAULT": [7, 8, 9, 10], # 槽位 7~10: Boeing 737-800
    "EXTRA": list(range(11, 20)) # 槽位 11~19: B737-800 (溢出备用槽)
}

class BlueSkyBridge:
    def __init__(self, qt_host='127.0.0.1', qt_port=49004):
        self.qt_host = qt_host
        self.qt_port = qt_port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.client = Client()
        self.xp_client = xpc.XPlaneConnect()
        
        self.ai_mapping = {} 
        self.slots_pool = copy.deepcopy(XP_SLOT_CONFIG)
        
        self.ownship_callsign = "OWN001"
        self.last_ownship_alt_m = None
        self.disable_autospawn = os.getenv("BS_DISABLE_AUTOSPAWN", "0") == "1"
        self.ownship_state = None  
        self.last_ownship_sync_state = None  
        self.last_sent_state = {}  
        self.smooth_alpha = float(os.getenv("BS_SMOOTH_ALPHA", "0.35"))
        
        self.connect_to_bluesky()

    def read_ownship_from_xp(self):
        try:
            pos = self.xp_client.getPOSI(0)
            if pos:
                self.last_ownship_alt_m = pos[2]
        except:
            pass

    def connect_to_bluesky(self):
        try:
            self.client.connect(hostname='127.0.0.1', recv_port=11000, send_port=11001)
            time.sleep(0.5)
            self.traf_proxy = TrafficProxy()
            self.client.subscribe(b'ACDATA', actonly=True)
        except: pass

    def assign_slot(self, callsign, model):
        if callsign in self.ai_mapping:
            return self.ai_mapping[callsign]
        
        m = model.upper()
        c = callsign.upper()
        needed_type = "DEFAULT"
        
        # 严格匹配类型，确保与生成脚本中的型号 (MQ9, R44, B744, A320) 对齐
        if "R44" in m or "HELI" in m or "HELI" in c: 
            needed_type = "ROTOR"
        elif "MQ" in m or "UAV" in m or "UAV" in c: 
            needed_type = "UAV"
        elif "B74" in m or "A33" in m or "HEAVY" in m or "HEAVY" in c: 
            needed_type = "HEAVY"
        else:
            needed_type = "DEFAULT" # A320, B738 等都在这里
            
        slot = -1
        # 从对应的池子里拿槽位
        if len(self.slots_pool[needed_type]) > 0:
            slot = self.slots_pool[needed_type].pop(0)
        elif len(self.slots_pool["DEFAULT"]) > 0:
            slot = self.slots_pool["DEFAULT"].pop(0) # 降级：如果特定机型没了，给个普通客机
        elif len(self.slots_pool["EXTRA"]) > 0:
            slot = self.slots_pool["EXTRA"].pop(0)   # 终极降级
            
        if slot != -1:
            self.ai_mapping[callsign] = slot
            print(f"✈️ Assigned: {callsign} ({model}) -> Slot {slot} [{needed_type}]")
        return slot

    def get_traffic_list(self):
        self.client.update()
        data = []
        self.ownship_state = None
        current_callsigns = set()

        if self.traf_proxy and self.traf_proxy.ntraf > 0:
            for i in range(self.traf_proxy.ntraf):
                cs = str(self.traf_proxy.id[i])

                hdg = float(getattr(self.traf_proxy, 'trk', [0.0])[i]) if hasattr(self.traf_proxy, 'trk') else 0.0
                if hasattr(self.traf_proxy, 'hdg'):
                    hdg = float(self.traf_proxy.hdg[i])

                if cs in ["OWN001", "OWN"]:
                    self.ownship_state = {
                        'lat': float(self.traf_proxy.lat[i]),
                        'lon': float(self.traf_proxy.lon[i]),
                        'alt_m': float(self.traf_proxy.alt[i]),
                        'hdg': hdg,
                    }
                    continue

                current_callsigns.add(cs)
                gs_ms = float(self.traf_proxy.gs[i])
                speed_kts = gs_ms * 1.94384

                model = ""
                raw_model = getattr(self.traf_proxy, 'type', getattr(self.traf_proxy, 'actype', None))
                if raw_model is not None:
                    if isinstance(raw_model, list) and i < len(raw_model): raw_model = raw_model[i]
                    if isinstance(raw_model, bytes): model = raw_model.decode('utf-8', errors='ignore').strip()
                    else: model = str(raw_model).strip()

                if not model or model == "B744":
                    c_up = cs.upper()
                    if "HELI" in c_up or "R44" in c_up: model = "R44"
                    elif "UAV" in c_up or "MQ" in c_up: model = "MQ9"
                    elif "HEAVY" in c_up: model = "B744"
                    elif "TFC" in c_up: model = "A320"
                    else: model = "A320"

                self.assign_slot(cs, model)

                lat = float(self.traf_proxy.lat[i])
                lon = float(self.traf_proxy.lon[i])
                alt_ft = float(self.traf_proxy.alt[i]) * 3.28084

                data.append({
                    'callsign': cs,
                    'latitude': lat,
                    'longitude': lon,
                    'altitude': alt_ft,
                    'heading': hdg,
                    'speed': speed_kts,
                    'isOwnship': False,
                    'model': model
                })

        # 垃圾回收：如果飞机不在当前列表中，释放槽位并清除缓存
        dead_callsigns = [c for c in self.ai_mapping.keys() if c not in current_callsigns]
        for c in dead_callsigns:
            freed_slot = self.ai_mapping.pop(c)
            self.last_sent_state.pop(c, None) # 清除旧飞机的平滑状态，防止再次生成时漂移
            for cat, slots_list in XP_SLOT_CONFIG.items():
                if freed_slot in slots_list:
                    self.slots_pool[cat].append(freed_slot)
                    self.slots_pool[cat].sort()
                    print(f"♻️  Recycled slot {freed_slot} from deleted aircraft {c}")
                    break

        return data

    def update_xplane(self, traffic):
        if not self.xp_client: return
        
        try:
            overrides = [0] + [1] * 19
            self.xp_client.sendDREF("sim/operation/override/override_planepath", overrides)
        except: pass
        
        active_slots = set() # 🌟 新增：记录当前帧正在活跃的槽位
        
        for ac in traffic:
            cs = ac['callsign']
            if cs in self.ai_mapping:
                slot = self.ai_mapping[cs]
                active_slots.add(slot) # 标记为使用中
                
                target_alt_m = ac['altitude'] / 3.28084
                target_lat = ac['latitude']
                target_lon = ac['longitude']
                target_hdg = ac['heading']

                prev = self.last_sent_state.get(cs)
                if prev is not None:
                    dlat = target_lat - prev['lat']
                    dlon = target_lon - prev['lon']
                    dalt = target_alt_m - prev['alt_m']

                    deg_3nm = 3.0 / 60.0
                    if abs(dlat) > deg_3nm or abs(dlon) > deg_3nm:
                        smooth_lat = target_lat
                        smooth_lon = target_lon
                        smooth_alt_m = target_alt_m
                    else:
                        a = self.smooth_alpha
                        smooth_lat = prev['lat'] + a * dlat
                        smooth_lon = prev['lon'] + a * dlon
                        smooth_alt_m = prev['alt_m'] + a * dalt
                else:
                    smooth_lat = target_lat
                    smooth_lon = target_lon
                    smooth_alt_m = target_alt_m

                self.last_sent_state[cs] = {
                    'lat': smooth_lat,
                    'lon': smooth_lon,
                    'alt_m': smooth_alt_m,
                    'hdg': target_hdg,
                }

                self.xp_client.sendPOSI([
                    smooth_lat, smooth_lon, smooth_alt_m,
                    0, 0, target_hdg, 1.0
                ], slot)

        # 🌟🌟🌟 新增核心逻辑：清理幽灵飞机 (Ghost AI Cleanup) 🌟🌟🌟
        # 遍历 1~19 号槽位，凡是没被标记为活跃的，全部扔到南极地下
        for s in range(1, 20):
            if s not in active_slots:
                try:
                    self.xp_client.sendPOSI([-89.0, 0.0, -5000.0, 0, 0, 0, 0], s)
                except:
                    pass

    def run(self):
        print("Bridge Running (20Hz XP / 50Hz Qt)...")
        next_qt = time.time()
        next_xp = time.time()
        
        while True:
            now = time.time()
            self.read_ownship_from_xp()

            traffic = self.get_traffic_list()
            
            # 发送给 Qt (50Hz)
            if now >= next_qt:
                # 即使 traffic 是空列表，也必须发送！这样 Qt 才知道天空中没飞机了。
                msg = {'type': 'aircraft_data', 'data': traffic}
                self.sock.sendto(json.dumps(msg).encode(), (self.qt_host, self.qt_port))
                next_qt += 0.02
            
            # 发送给 X-Plane (20Hz)
            if now >= next_xp:
                self.update_xplane(traffic) # 即使 traffic 是空列表，也会进入 update_xplane 触发全槽位清空
                next_xp += 0.05
            
            time.sleep(0.001)

if __name__ == '__main__':
    BlueSkyBridge().run()