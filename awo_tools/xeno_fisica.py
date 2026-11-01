"""Contrato XV5S: cadenas SCD/ESK locales, pesos y ventanas para el runtime.

Datos construidos de la copia local. El controlador es propio [H], no reproduce
los coeficientes SCD del ejecutable XV2. Sin pieza con SCD no se emite nada.
El bloque se guarda tras los nombres AWG, fuera de los nb nombres del motor.
"""
import struct
import numpy as np


def metadata(rig, groups):
    from xeno_a_b3 import order
    nodes, vertices, rigs = [], [], {}
    cursor = 0
    for bone in sorted(groups):
        for group in groups[bone]:
            piece = group.get('physics_piece')
            unique = {}
            for k, window in enumerate(group['win']):
                if window in unique:
                    continue
                unique[window] = len(unique)
                if not piece:
                    continue
                extra, attachment = piece['physics_esk'], piece['attachment']
                key = piece['part']
                if key not in rigs:
                    indices = {}
                    A = rig.invW[bone] @ rig.matrices[attachment] @ rig.esk.world_matrix[rig.esk.index[attachment]]
                    for i in order(extra.parent):
                        if not extra.names[i].startswith('SCD_'):
                            continue
                        parent = indices.get(extra.parent[i], -1)
                        local = A[:3,:3] @ extra.world[i] + A[:3,3]
                        indices[i] = len(nodes)
                        nodes.append((bone, parent+1, int(parent < 0), 0, *local, .045))
                    rigs[key] = {extra.names[i]: n for i,n in indices.items()}
                by_name = rigs[key]
                sm = piece['sm']
                ids, weights = [], []
                for bi, w in zip(sm.bones[k], sm.weights[k]):
                    node = by_name.get(sm.bone_names[bi])
                    ids.append(node+1 if node is not None else 0)
                    weights.append(float(w))
                vertices.append((cursor+unique[window], bone, *ids, *weights))
            cursor += len(unique)
    if not nodes or not vertices:
        return b'', dict(nodes=0, vertices=0, controller='off_no_SCD')
    body = b''.join(struct.pack('>4I4f', *n) for n in nodes)
    body += b''.join(struct.pack('>6I4f', *v) for v in vertices)
    size = 32+len(body)
    header = struct.pack('>4s7I', b'XV5S', 1, len(nodes), len(vertices), size, cursor, rig.tpl.nb, 0)
    data = header+body
    data += bytes(-len(data)%32)
    return data, dict(nodes=len(nodes), vertices=len(vertices), bytes=len(data),
                      controller='XV5S_fixed60_springs_body_spheres', coefficients='[H] propios, no SCD XV2',
                      source='BCS physics attachment + ESK SCD_ hierarchy + EMD weights')


def selftest():
    from types import SimpleNamespace as Obj
    from xeno_a_b3 import synthetic_template, Retarget
    e,t=synthetic_template(); rig=Retarget(e,t)
    head=t.bone('HEAD')
    # Padre despues de su hijo en el archivo; el contrato debe ser topologico.
    extra=Obj(names=['anchor','SCD_tip','SCD_root'],parent=[-1,2,0],
              world=np.array([[0,0,0],[0,1,0],[0,0,0]],float))
    sm=Obj(bone_names=['SCD_tip'],bones=np.zeros((3,4),int),weights=np.tile([1.,0,0,0],(3,1)))
    p=dict(physics_esk=extra,attachment='b_C_Head',part='test',sm=sm)
    groups={0:[dict(win=[b'body'])],head:[dict(win=[b'a',b'a',b'b'],physics_piece=p)]}
    b,stats=metadata(rig,groups)
    assert len(b)%32==0 and stats['nodes']==2 and stats['vertices']==2
    _,_,nn,nv,size,count,nb,_=struct.unpack_from('>4s7I',b)
    assert (nn,nv,size,count,nb)==(2,2,176,3,t.nb)
    assert struct.unpack_from('>4I',b,32)==(head,0,1,0)
    assert struct.unpack_from('>4I',b,64)==(head,1,0,0)
    v=struct.unpack_from('>6I4f',b,96)
    assert v[:6]==(1,head,2,2,2,2) and v[6:]==(1.,0.,0.,0.)
    assert struct.unpack_from('>I',b,136)[0]==2
    assert metadata(rig,{0:[dict(win=[b'body'])]})[0]==b''
    print('xeno_fisica: N0 OK (jerarquia desordenada, indices tras deduplicar, cuatro pesos, sin SCD)')


if __name__=='__main__':
    import argparse
    ap=argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--selftest',action='store_true')
    args=ap.parse_args()
    if args.selftest: selftest()
    else: ap.error('usa --selftest; la exportacion la invoca xeno_a_b3.py')
