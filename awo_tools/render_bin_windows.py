#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""render_win.py <bin> <out.png> [yaw_deg] - Render front-view del modelo de
VENTANAS (44B) + IB lista, modelo-space = world[bone]·pos."""
import sys, os, math
import numpy as np
from PIL import Image
sys.path.insert(0, sys.argv[3])
from awg_vertex_buffer import AwgVertexBuffer, mvec

def render(path, out, W=700, H=900, yaw_deg=0.0):
    a = AwgVertexBuffer.load(path)
    world, _ = a.bind_worlds()
    verts = a.vertices()
    ib = a.indices()
    P = []
    for v in verts:
        m = mvec(world[v["bone"]], [v["pos"][0], v["pos"][1], v["pos"][2], 1.0])
        P.append((m[0], m[1], m[2]))
    P = np.array(P)
    tris = []
    strip = "--strip" in sys.argv
    if strip:
        for i in range(0, len(ib) - 2):
            a, b, c = ib[i], ib[i+1], ib[i+2]
            t = (a, b, c) if i % 2 == 0 else (b, a, c)
            if len(set(t)) < 3 or max(t) >= len(P):
                continue
            tris.append(t)
    else:
        for i in range(0, len(ib) - 2, 3):
            t = (ib[i], ib[i+1], ib[i+2])
            if len(set(t)) < 3 or max(t) >= len(P):
                continue
            tris.append(t)
    T = np.array([P[list(t)] for t in tris]) if tris else np.zeros((0,3,3))
    yaw = math.radians(yaw_deg); cy, sy = math.cos(yaw), math.sin(yaw)
    R = np.array([[cy,0,sy],[0,1,0],[-sy,0,cy]])
    Q = T.reshape(-1,3).dot(R.T).reshape(-1,3,3) if len(T) else T
    mn = P.min(0); mx = P.max(0); ctr = (mn+mx)/2
    ext = max(mx[1]-mn[1], mx[0]-mn[0], mx[2]-mn[2])
    scale = min(W,H)*0.9/max(ext,1e-6)
    X = (Q[:,:,0]-ctr[0])*scale + W/2
    Y = -(Q[:,:,1]-ctr[1])*scale + H/2
    Z = Q[:,:,2]
    img = np.zeros((H,W,3), np.uint8)+30
    zb = np.full((H,W), -1e9)
    v0 = Q[:,1]-Q[:,0]; v1 = Q[:,2]-Q[:,0]
    nrm = np.cross(v0,v1); ln = np.linalg.norm(nrm,axis=1); ln[ln==0]=1
    nrm = nrm/ln[:,None]
    light = np.array([0.3,0.5,-0.8]); light/=np.linalg.norm(light)
    shade = np.clip(nrm.dot(light),0.08,1.0)*220
    for k in range(len(T)):
        xs,ys,zs = X[k],Y[k],Z[k]
        x0=int(max(0,np.floor(xs.min()))); x1=int(min(W-1,np.ceil(xs.max())))
        y0=int(max(0,np.floor(ys.min()))); y1=int(min(H-1,np.ceil(ys.max())))
        if x1<x0 or y1<y0: continue
        px=np.arange(x0,x1+1); py=np.arange(y0,y1+1)
        gx,gy=np.meshgrid(px,py)
        d0=(xs[1]-xs[0])*(ys[2]-ys[0])-(xs[2]-xs[0])*(ys[1]-ys[0])
        if abs(d0)<1e-9: continue
        w0=((xs[1]-gx)*(ys[2]-gy)-(xs[2]-gx)*(ys[1]-gy))/d0
        w1=((xs[2]-gx)*(ys[0]-gy)-(xs[0]-gx)*(ys[2]-gy))/d0
        w2=1-w0-w1
        m=(w0>=-0.003)&(w1>=-0.003)&(w2>=-0.003)
        if not m.any(): continue
        zi=w0*zs[0]+w1*zs[1]+w2*zs[2]
        sub=zb[y0:y1+1,x0:x1+1]; upd=m&(zi>sub); sub[upd]=zi[upd]
        col=int(shade[k]); img[y0:y1+1,x0:x1+1][upd]=(col,min(255,col+18),min(255,col+36))
    Image.fromarray(img).save(out)
    print("render %s -> %s  verts=%d tris=%d  bbox_min=%s bbox_max=%s"
          % (os.path.basename(path), out, len(P), len(T), np.round(mn,3), np.round(mx,3)))

if __name__ == "__main__":
    render(sys.argv[1], sys.argv[2], yaw_deg=float(sys.argv[4]) if len(sys.argv)>4 else 0.0)
