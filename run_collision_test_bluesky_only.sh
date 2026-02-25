#!/usr/bin/env bash
# 仅 BlueSky 测试（不启动 X-Plane / Bridge / Qt）
# 用法: ./run_collision_test_bluesky_only.sh [场景名]
# 场景名可选: collision_G1_no_collision | collision_G2_collision | collision_G3_collision | collision_G4_no_collision
# 默认: collision_G2_collision

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SCENARIO="${1:-collision_G2_collision}"
PY="${SCRIPT_DIR}/Bluesky/venv/bin/python"
BS_PY="${SCRIPT_DIR}/Bluesky/BlueSky.py"
# BlueSky 会把 --scenfile 与 scenario_path 拼成路径，只传场景名避免 scenario/scenario/ 重复
SCENFILE="${SCENARIO}"

if [[ ! -x "$PY" ]]; then
  echo "错误: 未找到 Bluesky venv，请确认路径: $PY"
  exit 1
fi
if [[ ! -f "$BS_PY" ]]; then
  echo "错误: 未找到 BlueSky: $BS_PY"
  exit 1
fi

echo "=========================================="
echo "  碰撞几何 仅 BlueSky 测试"
echo "  场景: $SCENARIO"
echo "=========================================="
echo "  在 BlueSky 中点击 OP 开始仿真"
echo "=========================================="

cd "${SCRIPT_DIR}/Bluesky"
exec "$PY" BlueSky.py --scenfile "$SCENFILE"
