import socket
import struct
import sys

class XPlaneConnect:
    def __init__(self, xpHost='127.0.0.1', xpPort=49009, port=0, timeout=100):
        self.xpIp = xpHost
        self.xpPort = xpPort
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(timeout / 1000.0)
        self.sock.bind(('', port))

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def close(self):
        self.sock.close()

    def sendUDP(self, cmd, body):
        if not isinstance(cmd, bytes): cmd = cmd.encode('utf-8')
        # 标准头: 4字节指令 + 1字节0
        msg = struct.pack('<4sB', cmd, 0) + body
        self.sock.sendto(msg, (self.xpIp, self.xpPort))

    def readUDP(self):
        try: return self.sock.recv(16384)
        except socket.timeout: return None

    def getPOSI(self, ac=0):
        """读取位置: 兼容 X-Plane 12 混合精度"""
        self.sendUDP(b'GETP', struct.pack('<B', ac))
        buffer = self.readUDP()
        if not buffer: return None
        # 混合精度: ID + 3xDouble + 4xFloat
        if len(buffer) == 46:
            try: return struct.unpack('<dddffff', buffer[6:46])
            except: pass
        # 旧版精度: ID + 7xFloat
        elif len(buffer) >= 34:
            try: return struct.unpack('<fffffff', buffer[6:34])
            except: pass
        return None

    def sendDREF(self, dref, values):
        """
        [标准协议] 写入 DataRef
        结构: NameLen(1B) + Name(String) + Count(1B) + Values(Floats)
        """
        if not isinstance(values, (list, tuple)): values = [values]
        if len(values) > 255: values = values[:255]
        
        float_values = [float(v) for v in values]
        
        if not isinstance(dref, bytes): dref = dref.encode('utf-8')
        if len(dref) > 255: dref = dref[:255]

        # NameLen + Name + Count + FloatArray
        fmt = '<B%dsB%df' % (len(dref), len(float_values))
        body = struct.pack(fmt, len(dref), dref, len(float_values), *float_values)
        
        self.sendUDP(b'DREF', body)

    def sendPOSI(self, values, ac=0):
        """
        [复活版 sendPOSI]
        回归二进制 POSI 指令 (因为这个之前能通！)
        核心修正：发送 Double 精度的经纬度高，解决位置偏差。
        """
        # 补全数据
        data = list(values) + [-998] * (7 - len(values))
        lat, lon, alt, pitch, roll, hdg, gear = data
        idx = int(ac)
        
        # 尝试 XP12 混合精度格式 (3个Double + 4个Float)
        # 结构: ID(1B) + Lat(8B) + Lon(8B) + Alt(8B) + Pitch(4B) + Roll(4B) + Hdg(4B) + Gear(4B)
        # 总长: 1 + 24 + 16 = 41字节 (+头5字节 = 46字节)
        try:
            body = struct.pack('<Bdddffff', idx, float(lat), float(lon), float(alt), 
                               float(pitch), float(roll), float(hdg), float(gear))
            self.sendUDP(b'POSI', body)
        except Exception as e:
            print(f"XPC Error: {e}")