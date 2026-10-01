import struct
from dbg import call
def dbg():
    r = call('read_ram', addr=0x9F000000, len=80)
    v = struct.unpack('<4s2I9iI4i2iI', bytes.fromhex(r['hex']))
    return dict(tag=v[0], phase=v[1], frame=v[2], pos=v[3:6], hand=v[6:9], camT=v[9:12], monkey=hex(v[12]),
                valid=v[13], h=v[14], ofx=v[15]/65536, ofy=v[16]/65536, dir=(v[17], v[18]), on=v[19])
