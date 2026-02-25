#!/usr/bin/env python3
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
# ✈️ X-Plane AI 槽位配置表 (🚨 严格匹配你最新截图的顺序)
# =========================================================================
XP_SLOT_CONFIG = {
    "ROTOR": [1],          # 槽位 1: Sikorsky S-76C
    "UAV":   [2],          # 槽位 2: F-4 Phantom II
    "HEAVY": [3],          # 槽位 3: Airbus A330-300
    "DEFAULT": [4, 5, 6],  # 槽位 4, 5, 6: Boeing 737-800
    "EXTRA": list(range(7, 20))
}

class BlueSkyBridge:
    def __init__(self, qt_host='127.0.0.1', qt_port=49004):
        self.qt_host = qt_host
        self.qt_port = qt_port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.client = Client()
        self.xp_client = xpc.XPlaneConnect()
        
        self.ai_mapping = {} 
        # 使用深拷贝，防止污染原始配置
        self.slots_pool = copy.deepcopy(XP_SLOT_CONFIG)
        
        self.ownship_callsign = "OWN001"
        self.scenario_created = False
        self.last_spawn_check = 0
        self.last_ownship_alt_m = None
        self.model_cache = {} 
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

    def check_and_spawn_traffic(self):
        if self.disable_autospawn: return
        pass # 此处省略原 autospawn 逻辑

    def assign_slot(self, callsign, model):
        if callsign in self.ai_mapping:
            return self.ai_mapping[callsign]
        
        m = model.upper()
        c = callsign.upper()
        needed_type = "DEFAULT"
        
        # 🚨 双保险：同时识别机型和呼号
        if m.startswith("R44") or m.startswith("HELI") or "HELI" in c: needed_type = "ROTOR"
        elif m.startswith("MQ") or m.startswith("UAV") or "UAV" in c: needed_type = "UAV"
        elif m.startswith("B74") or m.startswith("A38") or m.startswith("A33") or m.startswith("HEAVY") or "HEAVY" in c: needed_type = "HEAVY"
            
        slot = -1
        # 1. 优先拿匹配的专属槽位
        if len(self.slots_pool[needed_type]) > 0:
            slot = self.slots_pool[needed_type].pop(0)
        # 2. 🚨 修复隐形 Bug：如果专属用完了，先去拿 DEFAULT (4,5,6)，绝对不能先去拿不存在的 EXTRA (7+)
        elif len(self.slots_pool["DEFAULT"]) > 0:
            slot = self.slots_pool["DEFAULT"].pop(0)
        # 3. 实在没有了再去 EXTRA
        elif len(self.slots_pool["EXTRA"]) > 0:
            slot = self.slots_pool["EXTRA"].pop(0)
            
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

                # 🚨 核心修复：应对 BlueSky 网络丢包，通过呼号强制反推模型！
                # 这将直接决定 Qt 画什么符号，以及 X-Plane 选什么 3D 模型。
                if not model or model == "B744":
                    c_up = cs.upper()
                    if "HELI" in c_up or "R44" in c_up: model = "R44"
                    elif "UAV" in c_up or "MQ" in c_up: model = "MQ9"
                    elif "HEAVY" in c_up: model = "B744"
                    elif "TFC" in c_up: model = "A320"
                    else: model = "A320" # 兜底

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

        # 垃圾回收逻辑保持不变...
        dead_callsigns = [c for c in self.ai_mapping.keys() if c not in current_callsigns]
        for c in dead_callsigns:
            freed_slot = self.ai_mapping.pop(c)
            for cat, slots_list in XP_SLOT_CONFIG.items():
                if freed_slot in slots_list:
                    self.slots_pool[cat].append(freed_slot)
                    self.slots_pool[cat].sort()
                    print(f"♻️  Recycled slot {freed_slot} from deleted aircraft {c}")
                    break

        return data

    def sync_ownship_to_scene(self):
        """将 X-Plane 自机位置/航向对齐到 BlueSky 场景中的 OWN001。

        只在“明显换场景”时瞬移一次，避免每帧强行改姿态导致剧烈摇晃。
        """
        if not self.ownship_state:
            return
        try:
            lat = self.ownship_state['lat']
            lon = self.ownship_state['lon']
            alt_m = self.ownship_state['alt_m']
            hdg = self.ownship_state['hdg']

            # 第一次看到 OWN001：一定对齐一次
            if self.last_ownship_sync_state is None:
                self.xp_client.sendPOSI([lat, lon, alt_m, 0, 0, hdg, 1.0], 0)
                self.last_ownship_sync_state = dict(lat=lat, lon=lon, alt_m=alt_m, hdg=hdg)
                return

            # 之后只有在“变化很大”时才认为是切换了新场景，再对齐一次
            prev = self.last_ownship_sync_state

            # 估算水平位移（粗略）：1° ≈ 60 NM
            dlat_deg = lat - prev['lat']
            dlon_deg = lon - prev['lon']
            approx_nm = math.sqrt(dlat_deg**2 + dlon_deg**2) * 60.0
            dalt = abs(alt_m - prev['alt_m'])

            # 阈值：水平位移 > 0.1 NM 或 高度差 > 50 ft，认为是“换场景”
            # 普通飞行每帧位移远小于 0.1 NM，不会触发；IC 加载新场景时会触发一次
            if approx_nm > 0.1 or dalt > 50.0:
                self.xp_client.sendPOSI([lat, lon, alt_m, 0, 0, hdg, 1.0], 0)
                self.last_ownship_sync_state = dict(lat=lat, lon=lon, alt_m=alt_m, hdg=hdg)
        except Exception as e:
            print(f"Ownship sync error: {e}")

    def update_xplane(self, traffic):
        if not self.xp_client: return
        
        try:
            overrides = [0] + [1] * 19
            self.xp_client.sendDREF("sim/operation/override/override_planepath", overrides)
        except: pass
        
        for ac in traffic:
            cs = ac['callsign']
            if cs in self.ai_mapping:
                slot = self.ai_mapping[cs]
                
                # 目标位置（来自 BlueSky）
                target_alt_m = ac['altitude'] / 3.28084
                target_lat = ac['latitude']
                target_lon = ac['longitude']
                target_hdg = ac['heading']

                # 上一次已发送的位置，用于平滑
                prev = self.last_sent_state.get(cs)
                if prev is not None:
                    dlat = target_lat - prev['lat']
                    dlon = target_lon - prev['lon']
                    dalt = target_alt_m - prev['alt_m']

                    # 若场景刚切换或跳变很大（> ~3 NM），直接跳过去，避免慢慢飘过去
                    # 1 deg ~ 60 NM，这里用 3 NM 阈值
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

                # 记录本次发送，用于下一帧平滑
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

    def run(self):
        print("Bridge Running (20Hz XP / 50Hz Qt)...")
        next_qt = time.time()
        next_xp = time.time()
        
        while True:
            now = time.time()
            self.read_ownship_from_xp()

            # 获取 BlueSky 流量
            traffic = self.get_traffic_list()
            # 场景加载后，将 X-Plane 自机对齐到 BlueSky 的 OWN001（一次性对齐）
            # self.sync_ownship_to_scene()
            
            # 发送给 Qt (50Hz)
            if now >= next_qt:
                if traffic:
                    msg = {'type': 'aircraft_data', 'data': traffic}
                    self.sock.sendto(json.dumps(msg).encode(), (self.qt_host, self.qt_port))
                next_qt += 0.02
            
            # 发送给 X-Plane (20Hz)
            if now >= next_xp:
                if traffic:
                    self.update_xplane(traffic)
                next_xp += 0.05
            
            time.sleep(0.001)

if __name__ == '__main__':
    BlueSkyBridge().run()