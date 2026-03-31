import socket

UDP_IP = "127.0.0.1"
UDP_PORT = 8888 

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
print("🎛️ 实验遥控器启动 | [1] Baseline框框 | [2] Glow隧道 | [3] Ribbed半透明骨架隧道 | [q] 退出")

while True:
    cmd = input(">>> 请输入模式数字 (1/2/3): ")
    if cmd.lower() == 'q': break
    if cmd in ['1', '2', '3']:
        msg = f"SYS_MODE:{cmd}"
        sock.sendto(msg.encode('utf-8'), (UDP_IP, UDP_PORT))
        print(f"✅ 发送成功")