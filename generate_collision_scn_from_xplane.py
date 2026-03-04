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
                # X-Plane true_airspeed 单位是 m/s，需要乘以 1.94384 转换为节 (knots)
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
            # 动态拦截数学引擎 (Dynamic Intercept Math)
            # 无论你飞多快，保证目标机在恰好的时间点 (t_cpa) 擦过你指定的距离 (d_cpa)
            # 并且保证目标机是从你的正前方风挡视野内切入的！
            # =====================================================================
            def calc_intercept(target_spd, t_cpa_sec, d_cpa_nm, from_right=True, is_headon=False):
                T_hrs = t_cpa_sec / 3600.0
                dist_own = own_spd * T_hrs
                
                # 计算相交点 (相对于当前本机的偏移)
                cross_fwd = dist_own
                cross_right = d_cpa_nm if from_right else -d_cpa_nm
                
                # 设定切入角度：Warning 接近对头飞(175度)，Caution 斜前方切入(150度)
                rel_hdg_deg = 175 if is_headon else 150
                if not from_right: rel_hdg_deg = -rel_hdg_deg 
                
                tgt_true_hdg = (own_hdg + rel_hdg_deg) % 360
                rel_hdg_rad = math.radians(rel_hdg_deg)
                dist_tgt = target_spd * T_hrs
                
                # 反推目标机的出生点
                start_fwd = cross_fwd - dist_tgt * math.cos(rel_hdg_rad)
                start_right = cross_right - dist_tgt * math.sin(rel_hdg_rad)
                
                lat, lon = get_pos(start_fwd, start_right)
                return lat, lon, tgt_true_hdg
            # ==========================================
            # S1: Populated Airspace (Baseline - 全安全)
            # 修复：将所有飞机收束到正前方的 60 度视场角 (FOV) 走廊内
            # 期待画面：前挡风玻璃内出现多个青色框 (Cyan)，证明系统未产生视觉杂波
            # ==========================================
            t_s1 = [
                # A320：正前方 4海里，稍微偏左 1海里，同速平行飞
                {'callsign': 'TFC1',   'model': 'A320', 'lat': get_pos(4.0, -1.0)[0], 'lon': get_pos(4.0, -1.0)[1], 'hdg': own_hdg, 'spd': own_spd},
                # B744：正前方 5海里，稍微偏右 1.5海里，同速平行飞
                {'callsign': 'HEAVY2', 'model': 'B744', 'lat': get_pos(5.0, 1.5)[0],  'lon': get_pos(5.0, 1.5)[1],  'hdg': own_hdg, 'spd': own_spd},
                # 直升机：正前方 2.5海里，稍微偏右 0.5海里，悬停在空中 (速度0)
                {'callsign': 'HELI1',  'model': 'R44',  'lat': get_pos(2.5, 0.5)[0],  'lon': get_pos(2.5, 0.5)[1],  'hdg': own_hdg, 'spd': 0},
            ]
            write_scn(scen_dir / "eval_S1_Baseline.scn", "S1 Populated Airspace", base_lat, base_lon, own_hdg, alt_ft, t_s1)
            
            # ==========================================
            # S2: Target Isolation (Caution - 完美触发版)
            # 动态计算：保证40秒后，精准在右侧 1.5海里处擦肩而过
            # ==========================================
            lat_c, lon_c, hdg_c = calc_intercept(target_spd=250, t_cpa_sec=40, d_cpa_nm=1.5, from_right=True, is_headon=False)
            t_s2 = [
                {'callsign': 'TFC1',  'model': 'A320', 'lat': get_pos(1.5, -2.5)[0], 'lon': get_pos(1.5, -2.5)[1], 'hdg': own_hdg, 'spd': own_spd},
                {'callsign': 'CONV1', 'model': 'B738', 'lat': lat_c, 'lon': lon_c, 'hdg': hdg_c, 'spd': 250}
            ]
            write_scn(scen_dir / "eval_S2_Caution.scn", "S2 Target Isolation", base_lat, base_lon, own_hdg, alt_ft, t_s2)

            # ==========================================
            # S3: Priority Stress Test (Warning - 完美触发版)
            # 动态计算：
            # 1. 致命威胁 (Warning): 25秒后，偏离度仅 0.1海里，直冲面门 (触发红隧道)
            # 2. 潜在威胁 (Caution): 50秒后，左侧 1.8海里擦过 (触发黄线)
            # ==========================================
            lat_w, lon_w, hdg_w = calc_intercept(target_spd=300, t_cpa_sec=25, d_cpa_nm=0.1, from_right=False, is_headon=True)
            lat_c2, lon_c2, hdg_c2 = calc_intercept(target_spd=250, t_cpa_sec=50, d_cpa_nm=1.8, from_right=True, is_headon=False)
            t_s3 = [
                {'callsign': 'TFC1',  'model': 'A320', 'lat': get_pos(2, -4)[0], 'lon': get_pos(2, -4)[1], 'hdg': own_hdg, 'spd': own_spd},
                {'callsign': 'CONV1', 'model': 'B738', 'lat': lat_c2, 'lon': lon_c2, 'hdg': hdg_c2, 'spd': 250},
                {'callsign': 'HDON1', 'model': 'A320', 'lat': lat_w,  'lon': lon_w,  'hdg': hdg_w,  'spd': 300}
            ]
            write_scn(scen_dir / "eval_S3_Warning.scn", "S3 Priority Stress Test", base_lat, base_lon, own_hdg, alt_ft, t_s3)

            # ==========================================
            # S4: Proximity Override (Safety Fallback)
            # 修复：将平行伴飞的飞机从 3点/9点钟方向移到前风挡 11点/1点钟方向
            # 数学验证：前 0.3，侧 0.15 -> 绝对距离 0.33 NM < 0.5 NM 阈值
            # 期待画面：正前方极近距离出现两个红色 Warning 框（无隧道），证明安全兜底生效
            # ==========================================
            t_s4 = [
                {'callsign': 'UAV1', 'model': 'MQ9',  'lat': get_pos(0.3, 0.15)[0],  'lon': get_pos(0.3, 0.15)[1],  'hdg': own_hdg, 'spd': own_spd},
                {'callsign': 'UAV2', 'model': 'MQ9',  'lat': get_pos(0.3, -0.15)[0], 'lon': get_pos(0.3, -0.15)[1], 'hdg': own_hdg, 'spd': own_spd}
            ]
            write_scn(scen_dir / "eval_S4_Override.scn", "S4 Proximity Override", base_lat, base_lon, own_hdg, alt_ft, t_s4)

            time.sleep(1.0)

        except Exception as e:
            print(f"Engine Warning: {e}")
            time.sleep(2)

if __name__ == "__main__":
    main()