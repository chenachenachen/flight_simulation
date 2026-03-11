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

        # 确保 BlueSky 里的 OWN001 存在
        f.write(f"00:00:00.00>CRE OWN001,B744,{own_lat:.6f},{own_lon:.6f},{own_hdg:.1f},{alt_ft}, 150\n")
        
        for t in targets:
            f.write(f"00:00:00.00>CRE {t['callsign']},{t['model']},{t['lat']:.6f},{t['lon']:.6f},{t['hdg']:.1f},{alt_ft}, {t['spd']}\n")

        f.write("\n00:00:00.00>OP\n")

def main():
    client = xpc.XPlaneConnect()
    scen_dir = ROOT / "Bluesky" / "scenario"
    
    print("Live Scenario Engine (Dynamic Intercept Math Mode) is running...")
    print("This engine reads your real-time airspeed to guarantee perfect encounters!")
    
    while True:
        try:
            pos = client.getPOSI(0)
            if not pos:
                time.sleep(1)
                continue

            base_lat, base_lon, alt_m, _, _, base_hdg, _ = pos
            alt_ft = alt_m * 3.28084
            own_hdg = base_hdg

            # 核心更新：实时读取本机的 真空速 (True Airspeed) 以消除物理速度差
            try:
                tas_ms = client.getDREF("sim/flightmodel/position/true_airspeed")[0]
                own_spd = max(tas_ms * 1.94384, 80.0)
            except:
                own_spd = 150.0

            def get_pos(fwd_nm, right_nm):
                hdg_rad = math.radians(own_hdg)
                dn = fwd_nm * math.cos(hdg_rad) - right_nm * math.sin(hdg_rad)
                de = fwd_nm * math.sin(hdg_rad) + right_nm * math.cos(hdg_rad)
                return offset_lat_lon(base_lat, base_lon, dn, de)

            # =====================================================================
            # 🌟 升级版：动态拦截数学引擎 (Explicit Relative Angle Control)
            # 通过显式指定 rel_hdg_deg (相对航向差)，可以创造绝对正中心的对撞！
            # =====================================================================
            def calc_intercept(target_spd, t_cpa_sec, d_cpa_nm, rel_hdg_deg, cross_from_right=True):
                T_hrs = t_cpa_sec / 3600.0
                dist_own = own_spd * T_hrs
                
                # 计算相交点 (相对于当前本机的偏移)
                cross_fwd = dist_own
                cross_right = d_cpa_nm if cross_from_right else -d_cpa_nm
                
                # 目标机真实的航向 (本机航向 + 相对航向角)
                tgt_true_hdg = (own_hdg + rel_hdg_deg) % 360
                rel_hdg_rad = math.radians(rel_hdg_deg)
                dist_tgt = target_spd * T_hrs
                
                # 反推目标机出生点
                start_fwd = cross_fwd - dist_tgt * math.cos(rel_hdg_rad)
                start_right = cross_right - dist_tgt * math.sin(rel_hdg_rad)
                
                lat, lon = get_pos(start_fwd, start_right)
                return lat, lon, tgt_true_hdg

            # ==========================================
            # S1: Populated Airspace (Baseline - 全安全)
            # ==========================================
            t_s1 = [
                {'callsign': 'TFC1',   'model': 'A320', 'lat': get_pos(4.0, -1.0)[0], 'lon': get_pos(4.0, -1.0)[1], 'hdg': own_hdg, 'spd': own_spd},
                {'callsign': 'HEAVY2', 'model': 'B744', 'lat': get_pos(5.0, 1.5)[0],  'lon': get_pos(5.0, 1.5)[1],  'hdg': own_hdg, 'spd': own_spd},
                {'callsign': 'HELI1',  'model': 'R44',  'lat': get_pos(2.0, 1.0)[0],  'lon': get_pos(2.0, 1.0)[1],  'hdg': own_hdg, 'spd': own_spd},
            ]
            write_scn(scen_dir / "eval_S1_Baseline.scn", "S1 Populated Airspace", base_lat, base_lon, own_hdg, alt_ft, t_s1)
            
            # ==========================================
            # S2: Target Isolation (Caution)
            # 修复：改为 160 度微小侧角斜劈，使其出生在视野偏右 6 度的地方 (绝对在屏幕内)
            # ==========================================
            lat_c, lon_c, hdg_c = calc_intercept(target_spd=200, t_cpa_sec=40, d_cpa_nm=1.2, rel_hdg_deg=160, cross_from_right=True)
            t_s2 = [
                # 背景飞机拉回屏幕内 (前4，左1)
                {'callsign': 'TFC1',  'model': 'A320', 'lat': get_pos(4.0, -1.0)[0], 'lon': get_pos(4.0, -1.0)[1], 'hdg': own_hdg, 'spd': own_spd},
                {'callsign': 'CONV1', 'model': 'B738', 'lat': lat_c, 'lon': lon_c, 'hdg': hdg_c, 'spd': 200}
            ]
            write_scn(scen_dir / "eval_S2_Caution.scn", "S2 Target Isolation", base_lat, base_lon, own_hdg, alt_ft, t_s2)

            # ==========================================
            # S3: Priority Stress Test (Warning) 
            # 修复：将红色和黄色飞机都压缩到前风挡 20 度夹角以内！
            # ==========================================
            # 1. 致命威胁 (Red): 绝对 180 度正对撞！居中。
            lat_w, lon_w, hdg_w = calc_intercept(target_spd=300, t_cpa_sec=28, d_cpa_nm=0.0, rel_hdg_deg=180, cross_from_right=True)
            
            # 2. 潜在威胁 (Yellow): 改为从左侧以 -165 度切入 (避开中间的红隧道)
            # 它会出生在偏左边 11 度的地方，非常安分地在屏幕左侧画出一条擦肩而过的黄线。
            lat_c2, lon_c2, hdg_c2 = calc_intercept(target_spd=200, t_cpa_sec=45, d_cpa_nm=1.5, rel_hdg_deg=-165, cross_from_right=False)
            
            t_s3 = [
                # 背景飞机拉回右前方 (前4.5，右1.2)
                {'callsign': 'TFC1',  'model': 'A320', 'lat': get_pos(4.5, 1.2)[0], 'lon': get_pos(4.5, 1.2)[1], 'hdg': own_hdg, 'spd': own_spd},
                {'callsign': 'CONV1', 'model': 'B738', 'lat': lat_c2, 'lon': lon_c2, 'hdg': hdg_c2, 'spd': 200}, # 左侧黄飞机
                {'callsign': 'HDON1', 'model': 'A320', 'lat': lat_w,  'lon': lon_w,  'hdg': hdg_w,  'spd': 300}  # 正中心红飞机
            ]
            write_scn(scen_dir / "eval_S3_Warning.scn", "S3 Priority Stress Test", base_lat, base_lon, own_hdg, alt_ft, t_s3)

            # ==========================================
            # S4: Proximity Override (Safety Fallback)
            # 修复：拉长前方距离，大幅缩小侧向距离，将其“挤”回前风挡正中心
            # 数学验证：前 0.4，侧 0.05 -> 绝对距离 0.403 NM < 0.5 NM (完美触发 Override)
            # 视角验证：水平夹角仅 7.1 度 -> 绝对居中，任你怎么晃都不会跑出视野！
            # ==========================================
            t_s4 = [
                {'callsign': 'UAV1', 'model': 'MQ9',  'lat': get_pos(0.4, 0.05)[0],  'lon': get_pos(0.4, 0.05)[1],  'hdg': own_hdg, 'spd': own_spd},
                {'callsign': 'UAV2', 'model': 'MQ9',  'lat': get_pos(0.4, -0.05)[0], 'lon': get_pos(0.4, -0.05)[1], 'hdg': own_hdg, 'spd': own_spd}
            ]
            write_scn(scen_dir / "eval_S4_Override.scn", "S4 Proximity Override", base_lat, base_lon, own_hdg, alt_ft, t_s4)

            time.sleep(1.0)

        except Exception as e:
            print(f"Engine Warning: {e}")
            time.sleep(2)

if __name__ == "__main__":
    main()