"""Tablas de seguimiento solAST2: evidencia por minijuego, eventos, B2 y Satan."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct

def details(ctx, modes, games, inv, dec, out, root, ref, write_csv):
    eu=ctx['eu'];er=ctx['eu_rows'];banks=ctx['banks']
    # Censo AP7 del BSK: los valores son eventos IW, NO IDs AFS.
    event_rows=[];source=(root/'src/anim_iw.cpp').read_text(encoding='utf-8')
    selected=set((int(b),int(c,16)) for c,b in re.findall(r'\{(0x[0-9A-Fa-f]+),\s*(9\d\d)\b',source))
    selected.update((int(b),int(c,16)) for b,c in re.findall(r'\{(9\d\d),\s*(0x[0-9A-Fa-f]+)\b',source))
    def read_leaf(r):
        with eu.open(r['archivo']) as f:
            f.seek(r['offset_archivo']);return f.read(r['bytes'])
    for r in er:
        if r['tipo']!='#BSK' or not 912<=r['entrada']<=932 or 'DATA_CMN' not in r['archivo']:continue
        b=read_leaf(r)
        n,tab=struct.unpack_from('<II',b,0x10)
        assert tab+n*4<=len(b)
        for code in range(n):
            blk=struct.unpack_from('<I',b,tab+4*code)[0]
            if not blk:continue
            assert blk+48<=len(b)
            nl,off=struct.unpack_from('<II',b,blk+40)
            if not off:continue
            assert off+8*nl<=len(b)
            for j in range(nl):
                typ,count,ptr=struct.unpack_from('<HHI',b,off+j*8)
                if typ!=7:continue
                assert ptr+16*count<=len(b)
                for k in range(count):
                    t,ident,active,ev,val,queue=struct.unpack_from('<HBBIII',b,ptr+k*16)
                    event_rows.append(dict(asset=r['id'],codigo_bsk=hex(code),cuadro=t,tipo=ev,
                        valor_iw=hex(val),activo_crudo=active,offset_hoja=hex(ptr+k*16),
                        selector_literal_en_port='si' if (r['entrada'],code) in selected else 'no_resuelto',
                        limite='Presencia en BSK original; no demuestra ejecucion de este evento ni equivale a ID AFS'))
    write_csv(out/'EVENTOS_IW_BSK.csv',event_rows,list(event_rows[0]))
    counts=Counter(int(r['tipo']) for r in event_rows if r['selector_literal_en_port']=='si')
    ctx['events']=event_rows
    ctx['summary']['eventos_bsk']=len(event_rows)
    ctx['summary']['eventos_selectores_literales_port']=dict(sorted(counts.items()))
    # Guia viva: leer el indice, no congelar nombres/orden de otra version.
    guide=root/'studies 5/ESTUDIO_IW/GUIA_MINIJUEGOS.md'
    names={int(k,16):(int(i),name,mod) for k,i,name,mod in
           re.findall(r'^\| (0x[0-9A-F]+) \| (\d+) \| ([^|]+) \| ([^|]+) \|',guide.read_text(encoding='utf-8'),re.M)}
    ground={0:(887,903),1:(888,897),2:(889,884),10:(893,900),14:(894,906),16:(896,909)}
    actor={0:[922],1:[922,912],2:[922],3:[922],5:[921,925],6:[922],7:[924,931],
           8:[924,918],9:[918,925],10:[922],11:[931],12:[931,928],13:[922,920],
           14:[914,925],15:[929,914],16:[922],17:[919,923],18:[930]}
    table=[];trace=[]
    dispatch=eu.va(0x4988e0,19)
    for key,(mini,name,mod) in sorted(names.items()):
        original={915} if mini not in (7,17) else set()
        effect={0x2f7cac:934,0x2f7cc8:933,0x2f7ce4:935}.get(dispatch[mini])
        if effect:original.add(effect)
        file='src/iw_mini_botones.cpp'
        detail='BSK/AMM de actores; efectos de combo933; atlas localizado4/0 y AMA4/1 leidos por el port; carga original de ese atlas aun sin argumento recuperado.'
        if mini in ground:
            pack,field=ground[mini];original.update((pack,field,field+1,field+2,938,954))
            file='src/iw_minijuegos.cpp';detail=f'Objetos{pack}; campo{field}; sobrevuelo{field+1}; efectos{field+2}; HUD6/0; avisos5/0,1. Ramas originales296E50/3028C0/323D30.'
        elif mini in (7,17):
            original.update((891,937));file='src/iw_mini_vuelo.cpp'
            detail='Original33E540: paquete891 confirmado por GP49C3A4; campo elegido en4830F0=881/882/883 (883 variante0,881 variante1); HUD7. Port891/23,24 MSF;25,26 AMC;28 efectos;29..33 voces.'
        elif mini in (3,8,9,15):
            file='src/iw_mini_disparo.cpp'
            if mini==3:original.update((897,890,954))
            if mini==15:original.add(895)
            detail='Port: 3=890 ladrillo/MSS/AMC/efectos;8=873+851 Kienzan;9=874+892 manos+842 rayo;15=878+895 manos+935 Kikoho; HUD9/1; AMC915/2. Solo los bancos con prueba en ORIGINAL_IW_BANCOS se consideran confirmados.'
        port_banks=original|set(actor[mini])
        unresolved=set()
        if mini in (8,9,15):
            port_banks.update({8:{873,851},9:{874,892,842},15:{878,895}}[mini])
            unresolved.update({8:{873,851},9:{874,892,842},15:{878}}[mini])
        proven=sorted(e for e in original if ('DATA_CMN.AFS',e) in banks)
        missing=sorted(original-set(proven))
        refs=[banks[('DATA_CMN.AFS',e)][0] for e in proven]
        p,s=ref(file,'Entrada(')
        # Las trazas son historicas y separadas de las cargas AFS. No abrir VM.
        tpath=Path('D:/tmp/solIWN')/f'original_{key:03X}.json'
        obs=[]
        if tpath.is_file():
            raw=tpath.read_bytes();data=json.loads(raw)
            poses=Counter((str(a.get('c')),str(a.get('clip'))) for f in data.get('filas',[]) for a in f.get('actores',[]))
            for (c,clip),n in poses.items():
                trace.append(dict(clave=hex(key),fuente=str(tpath),sha256=hashlib.sha256(raw).hexdigest(),
                                  codigo=c,clip=clip,muestras=n,alcance='PINE poses; NO lectura AFS ni homologacion automatica de clips'))
            obs=[c for c,_ in poses]
        table.append(dict(clave=hex(key),indice=mini,minijuego=name.strip(),modulo=mod.strip(),
            bancos_original_cmn_confirmados=','.join(map(str,proven)),
            bancos_original_pendientes=','.join(map(str,sorted(set(missing)|unresolved))),
            bancos_actor_port=','.join(map(str,actor[mini])),
            bancos_port_cmn=','.join(map(str,sorted(port_banks))),
            original_funcion_linea='; '.join(sorted(set(refs))),port_fichero_linea=p,
            detalle_original_port=detail,traza_poses=str(tpath) if obs else 'SIN TRAZA UTIL',
            control_guia='Contrastar intro/juego/resultado; HUD; voz/SE; modelos B3HD+animIW. Guia audiovisual G no equivale a ID AFS.',
            estado='PARCIAL: carga estatica comprobada; bancos de actor son seleccion del port, no actor observado por AFS en original',
            siguiente='Recuperar selectores variables (camaras por modulo, voces/dialogos y atlas QTE) antes de aprobar fidelidad.'))
    assert len(table)==18
    write_csv(out/'IW_POR_MINIJUEGO.csv',table,list(table[0]))
    write_csv(out/'TRAZAS_IW_POSES.csv',trace,list(trace[0]))
    # B2: enlaces adicionales por literal o tabla del MISMO ELF PAL.
    rows=[]
    def b2(entry,archive,fn,needle,path,anchor,table=None):
        f=dec['b2'][f'{fn:08x}'];hits=[(n,s.strip()) for n,s in f['lines'] if needle in s]
        if not hits:return
        proof=' | '.join(f'{n}: {s}' for n,s in hits)
        if table:
            words=games['b2'].va(*table)
            if (entry|(0 if archive=='DATA_CMN.AFS' else 0x10000)) not in words:return
            proof+=f'; ELF{table[0]:X}={words}'
        p,s=ref(path,anchor)
        if not p:return
        for item in inv:
            if item['juego']!='b2' or Path(item['archivo']).name.upper()!=archive or item['entrada']!=entry:continue
            old=modes['B2_NAVE'].get(item['id'],{})
            modes['B2_NAVE'][item['id']]={**item,**old,'modo':'B2_NAVE','estado':'bien','cruzado':'si',
                'original_funcion_linea':f'E:/DBZ3HD_trabajo/ghidra/salida/b2.c:{f["line"]} {fn:X}',
                'original_prueba':proof,'port_fichero_linea':p,'port_prueba':s,'prioridad':'P3',
                'limite':'Carga/lector confirmados; arte interno, movimiento y sonido no aprobados visualmente.'}
            rows.append(modes['B2_NAVE'][item['id']])
    for entry,archive,fn,literal,anchor in [
        (901,'DATA_CMN.AFS',0x2ad940,'0x385','g_rueda.a = LeerAma2(cmn.Entrada(901))'),
        (1127,'DATA_UK.AFS',0x2ad940,'0x10467','texs(loc.Entrada(1127), g_rueda.tex)'),
        (897,'DATA_CMN.AFS',0x2b1150,'0x381','g_burb.a = LeerAma2(cmn.Entrada(897))'),
        (898,'DATA_CMN.AFS',0x2b1150,'0x382','const auto burb_amt = cmn.Entrada(898)'),
        (541,'DATA_UK.AFS',0x292850,'0x1021d','leer(541, g_ficha)')]:
        b2(entry,archive,fn,'FUN_0012d100('+literal,'src/pasarela_nave.cpp',anchor)
    for e,anchor in [(907,'const auto amm = cmn.Entrada(907)'),(908,'g_principal_avion = ps2::CargarSuelto'),(909,'g_principal_avion = ps2::CargarSuelto')]:
        b2(e,'DATA_CMN.AFS',0x23d7d0,'DAT_00377d90','src/pasarela_nave.cpp',anchor,(0x377d90,3))
    prizes=games['b2'].va(0x3782a0,43)
    for word in prizes:
        ident=word&65535
        b2(542+ident,'DATA_UK.AFS',0x292850,'param_2 + 0x21eU','src/pasarela_nave.cpp',
           'const Imagen im = TexturaAmt(loc.Entrada(size_t(542 + id_b2)), 0)')
    # Dos AMA de Satan cargadas por el original, omitidas por el lector del port.
    for entry,fn in [(665,0x22d5f0),(667,0x232fb0)]:
        f=dec['b1'][f'{fn:08x}'];needle=f'FUN_00138240({hex(entry)},'
        hits=[(n,s.strip()) for n,s in f['lines'] if needle in s]
        assert hits
        for item in inv:
            if item['juego']!='b1' or item['entrada']!=entry or Path(item['archivo']).name.upper()!='DATA_SP.AFS':continue
            p,s=ref('src/pasarela_b1.cpp','const std::vector<uint8_t> pantalla = afs.Entry(size_t(banco - 3))')
            old=modes['SATAN_B1'][item['id']]
            old.update(estado='FALTA',cruzado='no',original_funcion_linea=f'E:/DBZ3HD_trabajo/ghidra/salida/b1.c:{f["line"]} {fn:X}',
                original_prueba=' | '.join(f'{n}: {s}' for n,s in hits),port_fichero_linea=p,port_prueba=s,
                prioridad='P2',impacto='El port lee el atlas pero no la AMA de esta pantalla; sus movimientos/tiempos originales no quedan conectados.',
                arreglo='Cargar AMA665 (portada) y667 (resultado), asociar AMT666/668 y ejecutar sus objetos/tiempos en el motor AMA.',
                limite='Ausencia de lectura AMA en LeerDatosB1 revisada; diferencia visual concreta por objeto sin medir.')
    ctx['summary']['b2_premios_tabla']=len(prizes)
    ctx['summary']['minijuegos_con_ficha']=len(table)
