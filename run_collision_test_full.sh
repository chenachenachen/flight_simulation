#!/usr/bin/env bash
# 三方联测：X-Plane + BlueSky + Bridge + Qt 叠加层 + 实时场景引擎

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ==========================================
# 🚨 新增：每次启动前，强制清理所有相关的后台幽灵进程！
# 这能 100% 解决端口被抢占、无法生成 scn 的问题。
# ==========================================
echo "清理历史后台进程 (防冲突)..."
pkill -f bluesky_bridge || true
pkill -f generate_collision_scn || true
pkill -f BlueSky || true
pkill -f QtBlueSkyDemo || true
sleep 1

PY="${SCRIPT_DIR}/Bluesky/venv/bin/python"
BS_PY="${SCRIPT_DIR}/Bluesky/BlueSky.py"
BRIDGE="${SCRIPT_DIR}/bluesky_bridge.py"
QT_EXEC="${SCRIPT_DIR}/QtBlueSkyDemo.app/Contents/MacOS/QtBlueSkyDemo"

if [[ ! -x "$PY" ]]; then
  echo "错误: 未找到 Bluesky venv，请确认路径: $PY"
  exit 1
fi

echo "=========================================="
echo "  碰撞几何 三方联测 (双目标版)"
echo "=========================================="

# 1. 启动 BlueSky
echo "[1/3] 启动 BlueSky..."
(cd "${SCRIPT_DIR}/Bluesky" && "$PY" BlueSky.py) &
BS_PID=$!
sleep 3
if ! kill -0 "$BS_PID" 2>/dev/null; then
  echo "错误: BlueSky 启动失败"
  exit 1
fi

# 2. 启动 Bridge 与 Live Scenario Engine
echo "[2/3] 启动 Bridge 与 Live Engine..."
BS_DISABLE_AUTOSPAWN=1 "$PY" "$BRIDGE" &
BRIDGE_PID=$!

"$PY" "${SCRIPT_DIR}/generate_collision_scn_from_xplane.py" &
LIVE_ENGINE_PID=$!
sleep 1

# 3. 启动 Qt 叠加层
QT_PID=""
if [[ -n "$QT_EXEC" && -x "$QT_EXEC" ]]; then
  echo "[3/3] 启动 Qt 叠加层..."
  "$QT_EXEC" &
  QT_PID=$!
else
  echo "[3/3] 跳过自动启动 Qt。"
fi

echo ""
echo "  ✅ 测试环境已全部启动！"
echo "  请在 BlueSky 控制台输入: IC scenario/collision_G2_collision.scn"
echo "=========================================="
wait