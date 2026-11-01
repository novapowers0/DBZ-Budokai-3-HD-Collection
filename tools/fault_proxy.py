"""Test-only UDP loopback fault injector, not a game service. No installations."""
import socket
import threading
import heapq
import time
import os

BURST=int(os.environ.get("DBZ3_PROXY_BURST","0"))
JITTER=int(os.environ.get("DBZ3_PROXY_JITTER_MS","0"))/1000.0   # retraso extra aleatorio 0..J por paquete (semilla fija)
import random
_RND=random.Random(1234)

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
        self.inputs=self.forwarded=0
    def start(self):
        for incoming,outgoing,is_guest in [(self.front,self.back,True),(self.back,self.front,False)]:
            t=threading.Thread(target=self.pump,args=(incoming,outgoing,is_guest))
            t.start(); self.threads.append(t)
    def pump(self,incoming,outgoing,is_guest):
        incoming.settimeout(.02)
        count=0
        pending=[]
        serial=0
        while not self.stop.is_set():
            now=time.monotonic()
            while pending and pending[0][0]<=now:
                _,_,data,target=heapq.heappop(pending)
                outgoing.sendto(data,target)
                self.forwarded+=1
            incoming.settimeout(min(.02,max(.001,pending[0][0]-now)) if pending else .02)
            try: data,addr=incoming.recvfrom(4096)
            except ConnectionResetError:
                # WinSock reports ICMP from a peer that already exited normally.
                continue
            except socket.timeout:
                continue
            if is_guest: self.guest=addr
            target=self.host if is_guest else self.guest
            if target is None: continue
            # Every fifth input lost; every seventh duplicated; every eleventh
            # held until the next packet (or <=20ms). Handshake unmodified.
            input_packet=len(data)>=6 and data[:4]==b'DBZ3' and data[4]==9
            if input_packet:
                count+=1
                self.inputs+=1
            if input_packet and self.loss_every and count%self.loss_every==0:
                self.dropped+=1; continue
            # DBZ3_PROXY_BURST=N: corte de 15 mandos seguidos (~250 ms) cada N mandos
            if input_packet and BURST and count%BURST<15:
                self.dropped+=1; continue
            extra=.02 if input_packet and count%11==0 else 0
            if extra: self.reordered+=1
            serial+=1
            heapq.heappush(pending,(time.monotonic()+self.delay+extra+(_RND.random()*JITTER if JITTER else 0),serial,data,target))
            if input_packet and count%7==0:
                serial+=1
                heapq.heappush(pending,(time.monotonic()+self.delay+extra+.001,serial,data,target))
                self.duplicates+=1
    def close(self):
        self.stop.set()
        for t in self.threads: t.join(timeout=2)
        self.front.close(); self.back.close()
