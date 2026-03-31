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
    sys.path.exit(1)

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
            # 允许单架飞机有独立高度，否则使用本机高度
            t_alt = t.get('alt', alt_ft)
            f.write(f"00:00:00.00>CRE {t['callsign']},{t['model']},{t['lat']:.6f},{t['lon']:.6f},{t['hdg']:.1f},{t_alt}, {t['spd']}\n")
            
            # 如果配置了垂直爬升率 (Vertical Speed)
            if 'vs' in t:
                f.write(f"00:00:00.10>VS {t['callsign']} {t['vs']}\n")

        f.write("\n00:00:00.00>OP\n")

def main():
    client = xpc.XPlaneConnect()
    scen_dir = ROOT / "Bluesky" / "scenario"
    
    print("SOP Scenario Generation Engine is running...")
    print("Ready to inject Task 1/2 and Task 3 encounters into BlueSky!")
    
    while True:
        try:
            pos = client.getPOSI(0)
            if not pos:
                time.sleep(1)
                continue

            base_lat, base_lon, alt_m, _, _, base_hdg, _ = pos
            alt_ft = alt_m * 3.28084
            own_hdg = base_hdg

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
            # 🎬 Task 1 & 2: The "Looming Threat" (黄变红 + 正确避让测试)
            # =====================================================================
            t_task1_2 = [
                # 假目标A (左侧15度，擦肩距离扩大到 1.8，确保其永远被卡在 Caution 阶段不触发红警)
                {'callsign': 'FAKEL', 'model': 'B737', 'lat': get_pos(5.8, -1.8)[0], 'lon': get_pos(5.8, -1.8)[1], 'hdg': (own_hdg + 180) % 360, 'spd': 200},
                
                # 假目标B (右侧15度，擦肩距离 1.8)
                {'callsign': 'FAKER', 'model': 'B737', 'lat': get_pos(5.8, 1.8)[0],  'lon': get_pos(5.8, 1.8)[1],  'hdg': (own_hdg + 180) % 360, 'spd': 200},
                
                # 真凶C (右侧约5度，带5度斜插角直奔机头，极小 D_CPA 确保击穿 1.5NM 触发 Warning)
                {'callsign': 'THREAT', 'model': 'B737', 'lat': get_pos(6.0, 0.4)[0], 'lon': get_pos(6.0, 0.4)[1], 'hdg': (own_hdg + 185) % 360, 'spd': 200}
            ]
            write_scn(scen_dir / "eval_Task1_2_LoomingThreat.scn", "Task 1 & 2: Looming Threat", base_lat, base_lon, own_hdg, alt_ft, t_task1_2)

            # =====================================================================
            # 🎬 Task 3: Modified SAGAT (空间深度与形状分类冻结测试)
            # 修复：将所有飞机收拢至正前方视野 (+/- 15度以内)，赋予直升机真实速度防剔除
            # 新增：加入普通单框飞机，完美凑齐四大形状（单框、双框、球体、倒三角）同框！
            # =====================================================================
            t_task3 = [
                # 干扰项A: 无人机/倒三角 (正前方 3.0 海里，偏右 0.8。平行飞，速度150)
                {'callsign': 'UAV01', 'model': 'QUAD', 'lat': get_pos(3.0, 0.8)[0], 'lon': get_pos(3.0, 0.8)[1], 'hdg': own_hdg, 'spd': 150},
                
                # 干扰项B: 直升机/球体 (正前方 2.5 海里，偏左 0.8。向右前慢飞，速度80)
                {'callsign': 'HELI1', 'model': 'R22', 'lat': get_pos(2.5, -0.8)[0], 'lon': get_pos(2.5, -0.8)[1], 'hdg': (own_hdg + 45) % 360, 'spd': 70},
                
                # 干扰项C: 普通客机/单框 (正前方 4.5 海里，偏右 1.5。平行同向飞，速度250)
                {'callsign': 'TFC01', 'model': 'B738', 'lat': get_pos(4.5, 1.5)[0], 'lon': get_pos(4.5, 1.5)[1], 'hdg': own_hdg, 'spd': 250},
                
                # 真凶D: 重型客机/双框 (距离从 4.0 缩进到 3.5，确保其初始 T_CPA <= 30秒，瞬间触发红色 Warning！)
                {'callsign': 'HEAVY', 'model': 'A380', 'lat': get_pos(3.5, 0.1)[0], 'lon': get_pos(3.5, 0.1)[1], 'hdg': (own_hdg + 175) % 360, 'spd': 300, 'alt': alt_ft - 500, 'vs': 2000}
            ]
            write_scn(scen_dir / "eval_Task3_SAGAT.scn", "Task 3: Modified SAGAT", base_lat, base_lon, own_hdg, alt_ft, t_task3)        

            time.sleep(1.0)

        except Exception as e:
            print(f"Engine Warning: {e}")
            time.sleep(2)

if __name__ == "__main__":
    main()