#!/bin/bash
# X-Plane连接诊断脚本

echo "=========================================="
echo "X-Plane连接诊断"
echo "=========================================="
echo ""

echo "1. 检查端口49000状态..."
if lsof -i :49000 > /dev/null 2>&1; then
    echo "   端口49000被占用："
    lsof -i :49000 | grep -v COMMAND
    echo ""
    echo "   ⚠️  注意：X-Plane应该发送数据到端口49000，而不是监听它"
    echo "   如果X-Plane正在监听端口49000，说明配置可能有问题"
else
    echo "   ✓ 端口49000未被占用（这是正常的）"
fi
echo ""

echo "2. 检查Qt应用是否正在运行..."
if pgrep -f QtBlueSkyDemo > /dev/null; then
    echo "   ✓ Qt应用正在运行"
    QT_PID=$(pgrep -f QtBlueSkyDemo | head -1)
    echo "   PID: $QT_PID"
else
    echo "   ✗ Qt应用未运行"
fi
echo ""

echo "3. 检查X-Plane是否正在运行..."
if pgrep -i x-plane > /dev/null || pgrep -i "X-Plane" > /dev/null; then
    echo "   ✓ X-Plane正在运行"
    XP_PID=$(pgrep -i x-plane | head -1 || pgrep -i "X-Plane" | head -1)
    echo "   PID: $XP_PID"
else
    echo "   ✗ X-Plane未运行"
fi
echo ""

echo "4. 测试UDP端口49000..."
echo "   尝试发送测试数据包到127.0.0.1:49000..."
echo "TEST" | nc -u -w 1 127.0.0.1 49000 2>&1
if [ $? -eq 0 ]; then
    echo "   ✓ 可以发送数据到端口49000"
else
    echo "   ⚠️  无法发送数据到端口49000（可能是正常的，如果Qt应用未绑定）"
fi
echo ""

echo "5. X-Plane数据输出配置检查清单："
echo "   □ 进入 X-Plane → Settings → Data Output"
echo "   □ 找到 'UDP Output' 部分"
echo "   □ 添加新的数据输出："
echo "     - IP Address: 127.0.0.1"
echo "     - Port: 49000"
echo "     - 选择数据项："
echo "       • Index 17: Latitude"
echo "       • Index 18: Longitude"
echo "       • Index 19: Altitude (MSL)"
echo "       • Index 20: Heading"
echo "       • Index 21: Pitch"
echo "       • Index 22: Roll"
echo "       • Index 3: Ground Speed"
echo "       • Index 4: Vertical Speed"
echo ""

echo "6. 建议的故障排除步骤："
echo "   1. 确认X-Plane数据输出配置为'发送'到127.0.0.1:49000"
echo "   2. 确认Qt应用已启动并绑定到端口49000"
echo "   3. 在X-Plane中移动飞机，观察数据是否发送"
echo "   4. 查看Qt应用的调试输出（如果可用）"
echo ""

echo "=========================================="

