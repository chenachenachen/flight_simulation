#!/usr/bin/env python3
import sys
import json
import socket
import time
import os
import math

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
# ✈️ X-Plane AI 槽位配置表
# =========================================================================
XP_SLOT_CONFIG = {
    "ROTOR": [1],       # Slot 1: Rotorcraft  
    "UAV":   [2],       # Slot 2: UAV  
    "HEAVY": [3],       # Slot 3: Heavy
    "DEFAULT": list(range(4, 20)) 
}

class BlueSkyBridge:
    def __init__(self, qt_host='127.0.0.1', qt_port=49004):
        self.qt_host = qt_host
        self.qt_port = qt_port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.client = Client()
        self.xp_client = xpc.XPlaneConnect()
        
        self.ai_mapping = {} 
        self.slots_pool = {
            "ROTOR": list(XP_SLOT_CONFIG["ROTOR"]),
            "UAV":   list(XP_SLOT_CONFIG["UAV"]),
            "HEAVY": list(XP_SLOT_CONFIG["HEAVY"]),
            "DEFAULT": list(XP_SLOT_CONFIG["DEFAULT"])
        }
        
        self.ownship_callsign = "OWN001"
        self.scenario_created = False
        self.last_spawn_check = 0
        self.last_ownship_alt_m = None
        self.model_cache = {} 
        # 当处于“场景驱动测试模式”时，禁用自动生成 HELI/HEAVY 等测试机
        self.disable_autospawn = os.getenv("BS_DISABLE_AUTOSPAWN", "0") == "1"
        # 记录 BlueSky 场景中的本机 (OWN001)，用于对齐 X-Plane 自机
        self.ownship_state = None   # dict(lat, lon, alt_m, hdg)
        # 上一次将 X-Plane 自机对齐到 OWN001 时的场景状态（用于检测是否真的“换场景”）
        self.last_ownship_sync_state = None  # dict(lat, lon, alt_m, hdg)
        # 简单位置平滑，减少 X-Plane 端 AI 抖动
        self.last_sent_state = {}  # callsign -> dict(lat, lon, alt_m, hdg)
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

    # =========================================================
    # 🛠️ 修改部分：调整生成位置 & 高度
    # =========================================================
    def check_and_spawn_traffic(self):
        # 显式禁用自动生成（用于 collision 场景测试）
        if self.disable_autospawn:
            return

        if self.scenario_created:
            return
        try:
            # 若已有目标机（除 OWN001 外的飞机），视为已加载场景，不再自动生成
            existing = self.get_traffic_list()
            if existing:
                self.scenario_created = True
                print(">>> Using existing BlueSky traffic (e.g. from .scn), skip auto-spawn")
                return
        except Exception:
            pass
        try:
            pos = self.xp_client.getPOSI(0)
            if not pos: return
            lat, lon, alt_m, _, _, hdg, _ = pos
            
            print(f">>> Auto-Spawning Traffic at {alt_m:.1f}m")
            alt_ft = alt_m * 3.28084
            
            # 1. 生成本机
            self.model_cache[self.ownship_callsign] = "B744"
            stack.stack(f"CRE {self.ownship_callsign} B744 {lat} {lon} {hdg} {alt_ft} 100")
            
            # 2. 生成两架测试机 (故意设置高度差，以便测试 Altitude Tether)
            
            # Target A: 直升机 (HELI01) - 低 500ft
            # 位置: 前方 1500m
            self.spawn_target("HELI01", lat, lon, alt_ft - 500, hdg, fwd_m=1500, side_m=-400, model="R44")
            
            # Target B: 重型机 (HEAVY1) - 高 2000ft
            # 位置: 前方 4000m
            self.spawn_target("HEAVY1", lat, lon, alt_ft + 2000, hdg, fwd_m=3000, side_m=600, model="B747")
            
            self.scenario_created = True
        except Exception as e:
            print(f"Spawn Error: {e}")

    def spawn_target(self, callsign, lat, lon, alt_ft, hdg, fwd_m, side_m, model="B744"):
        self.model_cache[callsign] = model
        wm = self.make_target(callsign, lat, lon, alt_ft, hdg, fwd_m, side_m, model)
        # 注意: 这里的 alt_ft 已经是调整过的高度了
        stack.stack(f"CRE {callsign} {model} {wm['latitude']:.6f} {wm['longitude']:.6f} {hdg} {alt_ft:.1f} 150")

    def make_target(self, callsign, lat, lon, alt_ft, hdg, fwd_m, side_m, model="B744"):
        hdg_rad = math.radians(hdg)
        dn = fwd_m * math.cos(hdg_rad) - side_m * math.sin(hdg_rad)
        de = fwd_m * math.sin(hdg_rad) + side_m * math.cos(hdg_rad)
        dlat = dn / 111111.0
        dlon = de / (111111.0 * math.cos(math.radians(lat)))
        return {
            'callsign': callsign,
            'latitude': lat + dlat,
            'longitude': lon + dlon
        }

    def assign_slot(self, callsign, model):
        if callsign in self.ai_mapping:
            return self.ai_mapping[callsign]
        
        m = model.upper()
        needed_type = "DEFAULT"
        if m.startswith("R44") or m.startswith("HELI"): needed_type = "ROTOR"
        elif m.startswith("MQ") or m.startswith("UAV"): needed_type = "UAV"
        elif m.startswith("B74") or m.startswith("A38") or m.startswith("HEAVY"): needed_type = "HEAVY"
            
        slot = -1
        if self.slots_pool[needed_type]:
            slot = self.slots_pool[needed_type].pop(0)
        elif self.slots_pool["DEFAULT"]:
            slot = self.slots_pool["DEFAULT"].pop(0)
            
        if slot != -1:
            self.ai_mapping[callsign] = slot
        return slot

    def get_traffic_list(self):
        """从 BlueSky 读取所有 traffic。
        
        - 记录场景本机 OWN001 的绝对位置/航向（ownship_state）
        - 其余飞机原样镜像到 X-Plane（不再做动态锚定），
          这样只要 X-Plane 自机对齐到 OWN001，几何就一致。
        """
        self.client.update()
        data = []
        self.ownship_state = None

        if self.traf_proxy and self.traf_proxy.ntraf > 0:
            for i in range(self.traf_proxy.ntraf):
                cs = str(self.traf_proxy.id[i])

                hdg = float(getattr(self.traf_proxy, 'trk', [0.0])[i]) if hasattr(self.traf_proxy, 'trk') else 0.0
                if hasattr(self.traf_proxy, 'hdg'):
                    hdg = float(self.traf_proxy.hdg[i])

                # 记录 OWN001，用于对齐 X-Plane 自机
                if cs in ["OWN001", "OWN"]:
                    self.ownship_state = {
                        'lat': float(self.traf_proxy.lat[i]),
                        'lon': float(self.traf_proxy.lon[i]),
                        'alt_m': float(self.traf_proxy.alt[i]),
                        'hdg': hdg,
                    }
                    continue

                gs_ms = float(self.traf_proxy.gs[i])
                speed_kts = gs_ms * 1.94384

                model = "B744"
                raw_model = None
                if hasattr(self.traf_proxy, 'type'):
                    raw_model = self.traf_proxy.type[i]
                elif hasattr(self.traf_proxy, 'actype'):
                    raw_model = self.traf_proxy.actype[i]

                if isinstance(raw_model, bytes):
                    model = raw_model.decode('utf-8', errors='ignore').strip()
                elif raw_model is not None:
                    model = str(raw_model).strip()

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