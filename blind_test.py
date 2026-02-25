import socket
import struct
import time

UDP_IP = "127.0.0.1"
UDP_PORT = 49009
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(0.5)

print("🔍 正在启动 X-Plane 协议扫描器...")
print("目标: 找到能成功写入 DREF 的数据包格式\n")

def check_pause_state():
    """读取当前的暂停状态"""
    # 发送标准 GETDREF 请求 (这个通常没变)
    # 格式: GETD + 0 + Count(1) + NameLen + Name
    dref = b"sim/time/paused"
    cmd = b'GETD\x00'
    body = struct.pack('<B B %ds' % len(dref), 1, len(dref), dref)
    sock.sendto(cmd + body, (UDP_IP, UDP_PORT))
    
    try:
        data = sock.recv(4096)
        # 解析返回: Count(1) + Value(f/d)
        if len(data) >= 6: # 至少有头+数量+值
            # 尝试按 float 解析
            val_f = struct.unpack('<f', data[6:10])[0]
            if val_f > 0.5: return True
            # 尝试按 double 解析 (如果是10字节以上)
            if len(data) >= 14:
                val_d = struct.unpack('<d', data[6:14])[0]
                if val_d > 0.5: return True
    except:
        pass
    return False

def try_format(name, payload):
    """尝试一种格式"""
    print(f"👉 尝试格式: [{name}] ... ", end="")
    
    # 1. 构造完整数据包 (Header + Payload)
    # 尝试标准头 DREF+0
    header = b'DREF\x00' 
    sock.sendto(header + payload, (UDP_IP, UDP_PORT))
    
    # 2. 稍等并检查是否生效
    time.sleep(0.1)
    if check_pause_state():
        print("✅ 成功！模拟器已暂停！")
        return True
    
    # 3. 尝试 6字节头 (Header Padding)
    header_pad = b'DREF\x00\x00'
    sock.sendto(header_pad + payload, (UDP_IP, UDP_PORT))
    time.sleep(0.1)
    if check_pause_state():
        print("✅ 成功！(需6字节头) 模拟器已暂停！")
        return True
        
    print("❌ 无效")
    return False

# ================= 准备测试数据 =================
dref_str = b"sim/time/paused"
val = 1.0

# 格式 A: NASA 标准 (NameLen + Name + Count + Float)
# <B (名长) + s (名) + B (数量) + f (值)
fmt_a = '<B%dsBf' % len(dref_str)
payload_a = struct.pack(fmt_a, len(dref_str), dref_str, 1, val)

# 格式 B: NASA 变种 (NameLen + Name + Count + Double) -- XP12 常用
fmt_b = '<B%dsBd' % len(dref_str)
payload_b = struct.pack(fmt_b, len(dref_str), dref_str, 1, val)

# 格式 C: 逆序 (Count + Float + NameLen + Name)
fmt_c = '<BfB%ds' % len(dref_str)
payload_c = struct.pack(fmt_c, 1, val, len(dref_str), dref_str)

# 格式 D: 逆序 + Double (Count + Double + NameLen + Name)
fmt_d = '<BdB%ds' % len(dref_str)
payload_d = struct.pack(fmt_d, 1, val, len(dref_str), dref_str)

# 格式 E: 纯结构体 (Name 500字节 + Count + Float)
dref_pad = dref_str + b'\x00' * (500 - len(dref_str))
fmt_e = '<500sBf'
payload_e = struct.pack(fmt_e, dref_pad, 1, val)

# ================= 开始扫描 =================

# 0. 先确保是非暂停状态 (如果已经是暂停，脚本没法判断是否生效)
if check_pause_state():
    print("⚠️ 警告：模拟器当前已经是 PAUSED 状态。")
    print("请手动【取消暂停】后再运行此脚本，否则无法测试写入功能！")
    exit()

# 1. 扫描
if try_format("标准 NASA (Float)", payload_a):
    print("\n🎉 结论：请使用 [标准 NASA 协议]。我将为你生成对应 xpc.py。")
elif try_format("XP12 混合 (Double)", payload_b):
    print("\n🎉 结论：请使用 [双精度名字优先协议]。我将为你生成对应 xpc.py。")
elif try_format("逆序 (Float)", payload_c):
    print("\n🎉 结论：请使用 [逆序浮点协议]。我将为你生成对应 xpc.py。")
elif try_format("逆序 (Double)", payload_d):
    print("\n🎉 结论：请使用 [逆序双精度协议]。我将为你生成对应 xpc.py。")
elif try_format("500字节定长名", payload_e):
    print("\n🎉 结论：请使用 [定长名字协议]。我将为你生成对应 xpc.py。")
else:
    print("\n💀 所有已知格式均失败。")
    print("可能原因：")
    print("1. 端口错误 (请再次确认插件设置是 49009)")
    print("2. 插件损坏或版本过旧")
    print("3. 防火墙拦截")