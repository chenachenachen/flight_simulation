import math
import os
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

try:
    import xpc
except ImportError:
    print("Error: cannot import xpc.py. Please make sure it is in the project root.")
    sys.exit(1)

def offset_lat_lon(lat_deg: float, lon_deg: float, d_n_nm: float, d_e_nm: float):
    lat_rad = math.radians(lat_deg)
    dlat = d_n_nm / 60.0
    if abs(math.cos(lat_rad)) < 1e-6:
        dlon = 0.0
    else:
        dlon = d_e_nm / (60.0 * math.cos(lat_rad))
    return lat_deg + dlat, lon_deg + dlon

def write_scn(path: Path, title: str, own_lat: float, own_lon: float, own_hdg: float, alt_ft: float, targets: list):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write(f"# {title}\n")
        f.write("00:00:00.00>DEL *\n")
        f.write("00:00:00.00>HOLD\n")
        f.write(f"00:00:00.00>PAN {own_lat:.6f} {own_lon:.6f}\n")
        f.write("00:00:00.00>ZOOM 2.0\n")
        f.write("00:00:00.00>ASAS ON\n\n")

        # 本机默认 250 节
        f.write(f"00:00:00.00>CRE OWN001,B744,{own_lat:.6f},{own_lon:.6f},{own_hdg:.1f},{alt_ft}, 250\n")
        
        # 批量生成目标机
        for t in targets:
            f.write(f"00:00:00.00>CRE {t['callsign']},{t['model']},{t['lat']:.6f},{t['lon']:.6f},{t['hdg']:.1f},{alt_ft}, {t['spd']}\n")

        f.write("\n00:00:00.00>OP\n")

def main():
    client = xpc.XPlaneConnect()
    scen_dir = ROOT / "Bluesky" / "scenario"
    
    print("Live Scenario Engine (Dual-Target Mode) is running...")
    
    while True:
        try:
            pos = client.getPOSI(0)
            if not pos:
                time.sleep(1)
                continue

            base_lat, base_lon, alt_m, _, _, base_hdg, _ = pos
            alt_ft = alt_m * 3.28084
            own_hdg = base_hdg

            def get_pos(fwd_nm, right_nm):
                hdg_rad = math.radians(own_hdg)
                dn = fwd_nm * math.cos(hdg_rad) - right_nm * math.sin(hdg_rad)
                de = fwd_nm * math.sin(hdg_rad) + right_nm * math.cos(hdg_rad)
                return offset_lat_lon(base_lat, base_lon, dn, de)

            # ==========================================
            # G1: 虚警过滤 (无隧道/箭头)
            # UAV1 (MQ9, 无人机): 右侧 1NM 平行飞
            # HEAVY2 (B744, 重型): 左侧 1NM，向外飞
            # ==========================================
            lat1, lon1 = get_pos(0.0, 1.0)
            lat2, lon2 = get_pos(1.0, -1.0)
            t_g1 = [
                {'callsign': 'UAV1', 'model': 'MQ9', 'lat': lat1, 'lon': lon1, 'hdg': own_hdg, 'spd': 250},
                {'callsign': 'HEAVY2', 'model': 'B744', 'lat': lat2, 'lon': lon2, 'hdg': (own_hdg - 90)%360, 'spd': 250}
            ]
            write_scn(scen_dir / "collision_G1.scn", "G1 Nuisance Filter", base_lat, base_lon, own_hdg, alt_ft, t_g1)

            # ==========================================
            # G2: 分级告警 (1D 箭头 vs 3D 隧道) - 解决重叠问题
            # TFC1 (A320, 普飞): 前方 3NM, 偏左 0.5NM
            # HEAVY2 (B744, 重型): 前方 7NM, 偏右 0.5NM
            # ==========================================
            lat1, lon1 = get_pos(3.0, -0.5)
            lat2, lon2 = get_pos(7.0, 0.5)
            t_g2 = [
                {'callsign': 'TFC1', 'model': 'A320', 'lat': lat1, 'lon': lon1, 'hdg': (own_hdg + 180)%360, 'spd': 250},
                {'callsign': 'HEAVY2', 'model': 'B744', 'lat': lat2, 'lon': lon2, 'hdg': (own_hdg + 180)%360, 'spd': 250}
            ]
            write_scn(scen_dir / "collision_G2.scn", "G2 1D vs 3D Escalation", base_lat, base_lon, own_hdg, alt_ft, t_g2)

            # ==========================================
            # G3: 航权规则双重校验 (白线提示)
            # TFC1 (A320): 右侧切入(触发右侧法则白线)
            # HELI2 (R44, 直升机): 左侧切入(触发机型最高优先级白线+圆形UI)
            # ==========================================
            lat1, lon1 = get_pos(2.5, 2.5)
            lat2, lon2 = get_pos(2.5, -2.5)
            t_g3 = [
                {'callsign': 'TFC1', 'model': 'A320', 'lat': lat1, 'lon': lon1, 'hdg': (own_hdg - 90)%360, 'spd': 250},
                {'callsign': 'HELI2', 'model': 'R44',  'lat': lat2, 'lon': lon2, 'hdg': (own_hdg + 90)%360, 'spd': 250}
            ]
            write_scn(scen_dir / "collision_G3.scn", "G3 Priority Cues", base_lat, base_lon, own_hdg, alt_ft, t_g3)

            # ==========================================
            # G4: Dcpa 擦肩距离视觉化
            # UAV1 (MQ9): 从身后擦过
            # TFC2 (A320): 从身前擦过
            # ==========================================
            lat1, lon1 = get_pos(2.0, 3.0)
            lat2, lon2 = get_pos(4.0, -3.0)
            t_g4 = [
                {'callsign': 'UAV1', 'model': 'MQ9', 'lat': lat1, 'lon': lon1, 'hdg': (own_hdg - 90)%360, 'spd': 250},
                {'callsign': 'TFC2', 'model': 'A320', 'lat': lat2, 'lon': lon2, 'hdg': (own_hdg + 90)%360, 'spd': 250}
            ]
            write_scn(scen_dir / "collision_G4.scn", "G4 Dcpa Visualization", base_lat, base_lon, own_hdg, alt_ft, t_g4)
            time.sleep(0.5)

        except Exception as e:
            print(f"Engine Warning: {e}")
            time.sleep(2)

if __name__ == "__main__":
    main()
