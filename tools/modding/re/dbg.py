# Tiny client for the recomp's TCP debug server (JSON per line, port 4480).
import json, socket, sys
def call(cmd, port=4480, **kw):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall((json.dumps({"id": 1, "cmd": cmd, **kw}) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"): buf += s.recv(1 << 20)
    s.close(); return json.loads(buf)
if __name__ == "__main__":
    args = {k: (int(v, 0) if v[:1].isdigit() else v) for k, v in (a.split("=", 1) for a in sys.argv[2:])}
    print(json.dumps(call(sys.argv[1], **args))[:4000])
