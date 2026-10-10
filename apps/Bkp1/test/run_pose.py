import importlib.util, time, subprocess, sys, re
spec=importlib.util.spec_from_file_location("rc","/home/cnrai/palmos_sdk/emulator/rc.py")
rc=importlib.util.module_from_spec(spec); spec.loader.exec_module(rc)
def R(c,t=30):
    for _ in range(8):
        try:
            r=rc.rc(c,timeout=t)
            if not r.startswith('ERR busy'): return r
        except Exception: pass
        time.sleep(0.5)
    return "ERR"
prc=sys.argv[1]
subprocess.run("for p in $(ps aux | grep 'build/pose64' | grep -v grep | awk '{print $2}'); do kill $p; done", shell=True); time.sleep(2)
subprocess.Popen("cd /home/cnrai/palmos_sdk/pose/pose64 && DISPLAY=:10.0 ./build/pose64 -rom /home/cnrai/palmos_sdk/roms/Palm-III-4.1-en.rom -ram_size 8192 -device PalmIII > /dev/null 2>&1", shell=True)
time.sleep(10)
print("install", R("install "+prc)[:30], flush=True)
for n in range(60):
    R("launch ExgTest")
    msg=None
    for _ in range(40):
        time.sleep(0.5)
        if 'blocked' in R('state'):
            d=R('dialog'); msg='CRASH: '+' '.join(d.split())[:150]
            R('dialog respond reset'); time.sleep(3)
            if 'blocked' in R('state'): R('dialog respond continue'); time.sleep(2)
            R('reset'); time.sleep(10); break
        u=R('ui')
        m=re.search(r'FIELD id=\d+ \([^)]*\) "([^"]*)"', u)
        if 'ExgTest' in u.splitlines()[0] and m:
            msg=m.group(1); R('tap-id 12004') if 'id=12004' in u else R('tap 20 150'); time.sleep(1); break
    print(n, msg, flush=True)
    if msg is None or 'ALL DONE' in msg: break
subprocess.run("for p in $(ps aux | grep 'build/pose64' | grep -v grep | awk '{print $2}'); do kill $p; done", shell=True)
