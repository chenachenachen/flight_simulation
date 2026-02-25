# 四种碰撞几何 (G1–G4) 测试说明

## 0. 避免 `ModuleNotFoundError: No module named 'numpy'`

请用 **BlueSky 自带的虚拟环境** 运行 Python（该环境已装好 numpy、bluesky 等）：

```bash
# 进入项目根目录 Qt
cd /path/to/Qt

# 用 Bluesky 的 venv 启动 BlueSky（二选一）
./Bluesky/venv/bin/python Bluesky/BlueSky.py --scenfile scenario/collision_G2_collision

# 用 Bluesky 的 venv 启动 Bridge
./Bluesky/venv/bin/python bluesky_bridge.py
```

或在当前 shell 里先激活再运行：

```bash
source Bluesky/venv/bin/activate   # macOS/Linux
python Bluesky/BlueSky.py --scenfile scenario/collision_G2_collision
python bluesky_bridge.py
```

---

## 1. 这些 .scn 和 X-Plane 的关系

- **.scn 是 BlueSky 的场景文件**：在 BlueSky 里创建 OWN001、TFC001 等飞机并设置初始位置/航向。
- **当前数据流**：
  - **本机 (Ownship)**：来自 **X-Plane**（Qt 用 XPlaneReceiver 读）。
  - **目标机 (Traffic)**：来自 **BlueSky**，由 **Bridge** 发给 Qt 显示，并同步到 **X-Plane 的 AI 槽位**（`update_xplane`）。
- 因此：**只要 BlueSky 里跑的是这些 .scn，Bridge 就会把场景里的 TFC001 同步到 X-Plane 里当 AI 机**，你在 X-Plane 里能看到并测试。

---

## 2. 测试方式一：带 X-Plane + Qt 全链路（推荐）

让 .scn 里的目标机出现在 X-Plane 里，本机你开，目标机由 BlueSky 驱动并同步到 X-Plane AI。

1. **启动 X-Plane 12**，选好机场、飞机，进入驾驶舱（可先不起飞）。
2. **用指定场景启动 BlueSky**（在项目根目录 `Qt` 下执行，**务必用 Bluesky 的 venv**）：
   ```bash
   ./Bluesky/venv/bin/python Bluesky/BlueSky.py --scenfile scenario/collision_G2_collision
   ```
   或 G1/G3/G4：把 `collision_G2_collision` 换成  
   `collision_G1_no_collision`、`collision_G3_collision`、`collision_G4_no_collision`。
3. **启动 Bridge**（在项目根目录，**用同一 venv**）：
   ```bash
   ./Bluesky/venv/bin/python bluesky_bridge.py
   ```
   Bridge 会检测到 BlueSky 里已有 OWN001 + TFC001，**不再自动生成 HELI01/HEAVY1**，只把 TFC001 同步到 X-Plane AI 和 Qt。
4. **启动 Qt 叠加层**，连接 X-Plane（本机）和 49004（收 Bridge 的 traffic）。
5. 在 BlueSky 里点 **OP** 开始仿真，X-Plane 里会看到 AI 机 TFC001 按场景几何运动，Qt 上也会显示该目标。

**预期**：
- G2、G3：本机与 TFC001 航迹交汇，应出现冲突/告警。
- G1、G4：航迹分离，无冲突。

---

## 3. 测试方式二：仅 BlueSky（不看 X-Plane）

不启动 X-Plane/Bridge/Qt，只验证几何与冲突逻辑。

1. 启动 BlueSky（用 Bluesky 的 venv）：
   ```bash
   ./Bluesky/venv/bin/python Bluesky/BlueSky.py --scenfile scenario/collision_G2_collision
   ```
2. 在 BlueSky 里点 **OP** 运行。
3. 在雷达/SSD 上观察 OWN001 与 TFC001 是否按预期交汇（G2/G3）或分离（G1/G4）。

也可在 BlueSky 控制台输入：
```text
IC scenario/collision_G1_no_collision
```
或 `collision_G2_collision` 等，再 **OP**。

---

## 4. 场景文件一览

| 文件 | 预期 | 说明 |
|------|------|------|
| `collision_G1_no_collision.scn` | 不碰撞 | OWN001 向北，TFC001 向东北，航迹分离 |
| `collision_G2_collision.scn`    | 碰撞   | OWN001 西北，TFC001 西南，航迹交汇 |
| `collision_G3_collision.scn`    | 碰撞   | 与 G2 类似，航迹交汇 |
| `collision_G4_no_collision.scn` | 不碰撞 | 与 G1 类似，航迹分离 |

所有场景均包含 OWN001（B747）和 TFC001（B747），高度 FL200 左右，速度 250 kt。
