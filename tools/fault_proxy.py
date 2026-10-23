"""Test-only UDP loopback fault injector, not a game service. No installations."""
import socket
import threading

class FaultProxy:
    def __init__(self, front_port, host_port, delay_ms=0, loss_every=5):
        self.delay=delay_ms/1000.0; self.loss_every=loss_every   # X1: latencia fija por paquete; 0 = sin perdidas
        self.front=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.front.bind(('127.0.0.1',front_port))
        self.back=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.back.bind(('127.0.0.1',0))
        self.host=('127.0.0.1',host_port)
        self.guest=None
        self.stop=threading.Event()
        self.threads=[]
        self.dropped=self.duplicates=self.reordered=0
    def start(self):
        for incoming,outgoing,is_guest in [(self.front,self.back,True),(self.back,self.front,False)]:
            t=threading.Thread(target=self.pump,args=(incoming,outgoing,is_guest))
            t.start(); self.threads.append(t)
    def pump(self,incoming,outgoing,is_guest):
        incoming.settimeout(.02)
        count=0
        held=None
        while not self.stop.is_set():
            try: data,addr=incoming.recvfrom(4096)
            except ConnectionResetError:
                # WinSock reports ICMP from a peer that already exited normally.
                continue
            except socket.timeout:
                if held:
                    outgoing.sendto(*held); held=None
                continue
            if is_guest: self.guest=addr
            target=self.host if is_guest else self.guest
            if target is None: continue
            count+=1
            # Every fifth input lost; every seventh duplicated; every eleventh
            # held until the next packet (or <=20ms). Handshake unmodified.
            input_packet=len(data)>=6 and data[:4]==b'DBZ3' and data[4]==9
            if input_packet and self.loss_every and count%self.loss_every==0:
                self.dropped+=1; continue
            if input_packet and count%11==0 and held is None:
                held=(data,target); self.reordered+=1; continue
            if self.delay:
                threading.Timer(self.delay,outgoing.sendto,(data,target)).start()
            else:
                outgoing.sendto(data,target)
            if held: outgoing.sendto(*held); held=None
            if input_packet and count%7==0:
                outgoing.sendto(data,target); self.duplicates+=1
    def close(self):
        self.stop.set()
        for t in self.threads: t.join(timeout=2)
        self.front.close(); self.back.close()
