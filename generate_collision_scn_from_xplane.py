#!/usr/bin/env python3
"""
【实时动态场景引擎】(Live Scenario Engine)
在后台持续运行。每 0.5 秒抓取一次 X-Plane 本机坐标和航向，
并实时覆写 4 个 G1-G4 .scn 测试场景。
实现“走到哪，测到哪，瞬间无缝刷新”。
"""

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


def write_scn(path: Path, title: str,
              own_lat: float, own_lon: float, own_hdg: float,
              tfc_lat: float, tfc_lon: float, tfc_hdg: float,
              alt_ft: float, collision: bool):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        f.write(f"# {title}\n")
        f.write(f"# Live-updated relative to X-Plane.\n\n")

        # 🚨 自动清场命令！你再也不用手动输入 DEL * 了！
        f.write("00:00:00.00>DEL *\n")
        
        f.write("00:00:00.00>HOLD\n")
        f.write(f"00:00:00.00>PAN {own_lat:.6f} {own_lon:.6f}\n")
        f.write("00:00:00.00>ZOOM 2.0\n")
        f.write("00:00:00.00>ASAS ON\n\n")

        spd_kts = 250
        f.write(f"00:00:00.00>CRE OWN001,B744,{own_lat:.6f},{own_lon:.6f},{own_hdg:.1f},{alt_ft}, {spd_kts}\n")
        f.write(f"00:00:00.00>CRE TFC001,B744,{tfc_lat:.6f},{tfc_lon:.6f},{tfc_hdg:.1f},{alt_ft}, {spd_kts}\n\n")

        if collision:
            f.write("00:00:00.00>ECHO Geometry: Collision expected\n")
        else:
            f.write("00:00:00.00>ECHO Geometry: No collision expected\n")
        f.write("00:00:00.00>OP\n")


def main():
    client = xpc.XPlaneConnect()
    scen_dir = ROOT / "Bluesky" / "scenario"
    
    print("🚀 Live Scenario Engine is running...")
    print("It will continuously update G1-G4 .scn files based on your current X-Plane position.")
    print("Leave this running in the background. Press Ctrl+C to stop.\n")

    # 🚨 核心逻辑：无限循环，实时监控并写入！
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

            # G1
            g1_tfc_lat, g1_tfc_lon = get_pos(0.0, 1.0)
            write_scn(scen_dir / "collision_G1_no_collision.scn", "G1 Parallel",
                      base_lat, base_lon, own_hdg, g1_tfc_lat, g1_tfc_lon, own_hdg, alt_ft, False)

            # G2
            g2_tfc_lat, g2_tfc_lon = get_pos(3.5, 0.0)
            g2_tfc_hdg = (own_hdg + 180.0) % 360.0
            write_scn(scen_dir / "collision_G2_collision.scn", "G2 Head-on",
                      base_lat, base_lon, own_hdg, g2_tfc_lat, g2_tfc_lon, g2_tfc_hdg, alt_ft, True)

            # G3
            g3_tfc_lat, g3_tfc_lon = get_pos(2.5, 2.5)
            g3_tfc_hdg = (own_hdg - 90.0) % 360.0
            write_scn(scen_dir / "collision_G3_collision.scn", "G3 Crossing",
                      base_lat, base_lon, own_hdg, g3_tfc_lat, g3_tfc_lon, g3_tfc_hdg, alt_ft, True)

            # G4
            g4_tfc_lat, g4_tfc_lon = get_pos(1.5, 2.5)
            g4_tfc_hdg = (own_hdg - 90.0) % 360.0
            write_scn(scen_dir / "collision_G4_no_collision.scn", "G4 Tail-Cross",
                      base_lat, base_lon, own_hdg, g4_tfc_lat, g4_tfc_lon, g4_tfc_hdg, alt_ft, False)
            
            # 每 0.5 秒更新一次文件，性能消耗极小
            time.sleep(0.5)

        except Exception as e:
            print(f"Engine Warning: {e}")
            time.sleep(2)

if __name__ == "__main__":
    main()

