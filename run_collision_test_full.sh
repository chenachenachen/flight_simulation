#!/usr/bin/env bash
# 三方联测：X-Plane + BlueSky + Bridge + Qt 叠加层
#
# 设计思路：
#   1）先手动启动 X-Plane（选择机场/飞机，进入驾驶舱即可）
#   2）之后只需要点击/运行本脚本，就自动启动 BlueSky、Bridge 和 Qt 叠加层
#
# 用法: ./run_collision_test_full.sh
# 启动完成后，在 BlueSky 控制台里用 IC 切换场景，例如：
#   IC scenario/collision_G2_collision

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PY="${SCRIPT_DIR}/Bluesky/venv/bin/python"
BS_PY="${SCRIPT_DIR}/Bluesky/BlueSky.py"
BRIDGE="${SCRIPT_DIR}/bluesky_bridge.py"
# Qt 叠加层可执行文件路径（请按实际情况修改），为空则不自动启动 Qt：
QT_EXEC="${SCRIPT_DIR}/QtBlueSkyDemo.app/Contents/MacOS/QtBlueSkyDemo"

if [[ ! -x "$PY" ]]; then
  echo "错误: 未找到 Bluesky venv，请确认路径: $PY"
  exit 1
fi
if [[ ! -f "$BS_PY" ]]; then
  echo "错误: 未找到 BlueSky: $BS_PY"
  exit 1
fi
if [[ ! -f "$BRIDGE" ]]; then
  echo "错误: 未找到 bridge: $BRIDGE"
  exit 1
fi

echo "=========================================="
echo "  碰撞几何 三方联测 (X-Plane + BlueSky + Bridge + Qt)"
echo "  场景由 BlueSky 内部 IC 命令控制"
echo "=========================================="

# 1. 启动 BlueSky（后台，工作目录设为 Bluesky；场景由你在 BlueSky 里手动 IC 加载）
echo "[1/3] 启动 BlueSky..."
(cd "${SCRIPT_DIR}/Bluesky" && "$PY" BlueSky.py) &
BS_PID=$!
sleep 3
if ! kill -0 "$BS_PID" 2>/dev/null; then
  echo "错误: BlueSky 启动失败"
  exit 1
fi
echo "      BlueSky PID: $BS_PID"

# 2. 启动 Bridge（后台），并禁用自动生成 HELI/HEAVY 测试机
echo "[2/3] 启动 Bridge..."
BS_DISABLE_AUTOSPAWN=1 "$PY" "$BRIDGE" &
BRIDGE_PID=$!
sleep 1
# 🚨 新增：启动实时动态场景引擎（后台）
echo "[2.5/3] 启动 Live Scenario Engine..."
"$PY" "${SCRIPT_DIR}/generate_collision_scn_from_xplane.py" &
LIVE_ENGINE_PID=$!

if ! kill -0 "$BRIDGE_PID" 2>/dev/null; then
  echo "错误: Bridge 启动失败"
  kill "$BS_PID" 2>/dev/null || true
  exit 1
fi
echo "      Bridge PID: $BRIDGE_PID"

# 3. 启动 Qt 叠加层（若路径有效）
QT_PID=""
if [[ -n "$QT_EXEC" && -x "$QT_EXEC" ]]; then
  echo "[3/3] 启动 Qt 叠加层..."
  "$QT_EXEC" &
  QT_PID=$!
  sleep 1
  if ! kill -0 "$QT_PID" 2>/dev/null; then
    echo "警告: Qt 叠加层启动失败（请检查 QT_EXEC 路径），但 BlueSky 与 Bridge 已启动。"
    QT_PID=""
  else
    echo "      Qt PID: $QT_PID"
  fi
else
  echo "[3/3] 跳过自动启动 Qt（如需自动启动，请修改脚本顶部 QT_EXEC 路径并确保其可执行）。"
fi

echo "  结束测试: kill $BS_PID $BRIDGE_PID $LIVE_ENGINE_PID ${QT_PID:+$QT_PID}"
wait

echo ""
echo "------------------------------------------"
echo "  使用说明："
echo "  1. 请先手动启动 X-Plane 12，进入驾驶舱（本脚本不启动 X-Plane）。"
echo "  2. 运行本脚本后："
echo "     - BlueSky: 已启动（在 BlueSky 里用 IC 命令加载任意场景，例如：IC scenario/collision_G2_collision，并点击 OP）；"
echo "     - Bridge : 已连接 BlueSky + X-Plane，自动把场景飞机同步到 X-Plane AI；"
echo "     - Qt     : 若 QT_EXEC 配置正确，已自动启动并叠加在 X-Plane 上。"
echo "  3. 之后在 BlueSky 控制台使用 IC 切换任意 .scn，X-Plane 会自动显示对应测试场景；"
echo "     Bridge 会自动把新场景的飞机同步到 X-Plane 和 Qt。"
echo "------------------------------------------"
echo "  结束测试: kill $BS_PID $BRIDGE_PID ${QT_PID:+$QT_PID}"
echo "  或直接关闭本终端（会一并结束子进程）"
echo "=========================================="

wait
