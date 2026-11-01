"""Cierre solAST2: identidad EU/USA por hoja completa, no por numero/cabecera.

El censo conserva IDs fisicos de la copia USA que consume el port. La evidencia
original procede exclusivamente del ELF y AFS EU; HOMOLOGACION_IW explicita el
puente. Cruce = carga del banco + lectura de esa hoja; NO fidelidad visual.
"""
from collections import defaultdict, Counter
import hashlib
from pathlib import Path
import struct

EU_DEFAULT = Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)')

def kind(name):
    n=Path(name).name.upper()
    return 'LOCAL_EN' if n in ('DATA_USA.AFS','DATA_ENG.AFS') else n

def hashes(game, rows):
    result={}
    for archive in sorted({r['archivo'] for r in rows}):
        with game.open(archive) as f:
            for r in rows:
                if r['archivo']!=archive: continue
                f.seek(int(r['offset_archivo']))
                left=int(r['bytes']); h=hashlib.sha256()
                while left:
                    b=f.read(min(left,1024*1024))
                    if not b:raise ValueError('Hoja truncada: '+r['id'])
                    h.update(b);left-=len(b)
                result[r['id']]=h.hexdigest()
    return result

def augment(games, inv, dec, out, root, ref, write_csv, inventory, Game, eu_path):
    eu=Game('iw_eu',eu_path)
    # Solo el ingles homologo y los bancos comunes/audio: no equiparar traducciones.
    eu.files={p:n for p,n in eu.files.items() if Path(p).name.upper() in
              ('DATA_CMN.AFS','DATA_ENG.AFS','ADX_CMN.AFS','ADX_JPN.AFS','ADX_USA.AFS')}
    er,issues=inventory(eu)
    ur=[r for r in inv if r['juego']=='iw']
    eh,uh=hashes(eu,er),hashes(games['iw'],ur)
    index=defaultdict(list)
    for r in ur:index[(kind(r['archivo']),uh[r['id']])].append(r)
    matches={};hom=[]
    for r in er:
        candidates=index.get((kind(r['archivo']),eh[r['id']]),[])
        exact=[c for c in candidates if c['entrada']==r['entrada'] and c['hijo']==r['hijo']]
        c=exact[0] if len(exact)==1 else candidates[0] if len(candidates)==1 else None
        if c:matches[r['id']]=c
        hom.append(dict(original_id=r['id'],tipo=r['tipo'],bytes=r['bytes'],sha256_completo=eh[r['id']],
                        port_id=c['id'] if c else '',candidatos=len(candidates),
                        estado='IDENTICO' if c else 'AMBIGUO' if candidates else 'SIN_EQUIVALENTE_EXACTO'))
    write_csv(out/'HOMOLOGACION_IW.csv',hom,list(hom[0]))
    from assets_censo_mips import recover
    mips=recover(eu,dec['iw'],{0x14c960,0x310560})
    write_csv(out/'ORIGINAL_IW_MIPS_CARGAS.csv',mips,list(mips[0]))
    evidence=[]
    # Bancos cuya llamada se ha revisado en los bytes MIPS (PC exacto).
    for fn,entries in [
        (0x3076b0,[903,904]),(0x31b350,[897,898]),(0x31f070,[884,885]),
        (0x31f100,[900,901]),(0x31f190,[906,907]),(0x31f220,[909,910]),
        (0x307670,[905]),(0x31b310,[899]),(0x31f030,[886]),
        (0x31f0c0,[902]),(0x31f150,[908]),(0x31f1e0,[911]),
        (0x2f7c70,[933,934,935]),(0x302e50,[938,954]),(0x310210,[0x10006]),
        (0x28a3e0,[0x10005,936]),(0x2ade30,[1127]),(0x300cc0,[1093,1092,0x1014e,0x1014d]),
        (0x366320,[0x10162,0x10161]),(0x381a30,[0x10008]),(0x1fc290,[813]),
        (0x33e540,[891,937,0x10007]),(0x3691a0,[897,890,915,954,0x10009])]:
        for entry in entries:
            hits=[r for r in mips if r['funcion'].split()[0]==f'0x{fn:08x}' and r['a0']==hex(entry)]
            if hits:
                evidence.append((entry,fn,'; '.join(f'ELF PC {h["pc"]} a0={h["a0"]} [{h["palabras_prueba"]}]' for h in hits)))
    # Ramas comprobadas en MgRes_OpenBundleFile. Los branch-likely no pueden
    # resolverse por barrido lineal: conservar la instruccion de cada rama.
    for mini,pc,entry in [(0,0x323d84,887),(1,0x323d7c,888),(2,0x323d70,889),
                          (10,0x323d64,893),(14,0x323d58,894),(16,0x323d4c,896)]:
        word=eu.va(pc,1)[0]
        assert word==0x24040000|entry,(hex(pc),hex(word))
        evidence.append((entry,0x323d30,f'ELF {pc:#x}={word:#010x}; rama indice={mini} de 296E50/323D30'))
    assert eu.va(0x474060,1)==[915]
    evidence.append((915,0x2f6540,'ELF 474060=915; lw a0,(a1) PC2F6594 -> JAL14C960 PC2F659C'))
    # RAMShootingMinigame combina a0<<16|a1; no es un ID a0 aislado.
    assert eu.va(0x30cefc,1)==[0x2405037f]
    evidence.append((895,0x30cef0,'ELF30CEFC addiu a1,zero,895; 30CF04 JAL310560; wrapper310564/568 combina a0<<16|a1'))
    # Titulos/descripcion: rama +14==1 de2FCBC0: 0x10000 | (0x84+indice).
    assert eu.va(0x2fcc1c,1)==[0x24a30084]
    for entry in range(132,228):
        evidence.append((0x10000|entry,0x2fcbc0,
                         f'ELF2FCC1C=24A30084; rama mision normal; indice{entry-132}; OR10000 y JAL14C960 en2FCC30'))
    # Tabla compartida de animaciones/sonidos/boca, 17 filas de 40 B.
    actors=eu.va(0x474080,170)
    assert actors[:2]==[922,0xffffffff] and actors[10]==921
    for i in range(17):
        for entry in actors[i*10:(i+1)*10]:
            if entry!=0xffffffff and entry:
                evidence.append((entry,0x2f54c0,f'ELF tabla474080 fila{i}, 8 CMN + SE + boca; consumidor2F52C0/2F54C0'))
    # Registro por banco original: prueba de carga, no atribucion a cada imagen.
    banks={}
    for entry,fn,proof in evidence:
        archive='DATA_ENG.AFS' if entry>>16==1 else 'DATA_CMN.AFS'
        eid=entry&65535
        f=dec['iw'][f'{fn:08x}']
        banks[(archive,eid)]=(f'E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c:{f["line"]} 0x{fn:08x} {f["name"]}',proof)
    write_csv(out/'ORIGINAL_IW_BANCOS.csv',[
        dict(archivo=a,entrada=e,original_funcion_linea=v[0],prueba=v[1],
             alcance='banco cargado; NO todos los hijos usados') for (a,e),v in sorted(banks.items())],
        ['archivo','entrada','original_funcion_linea','prueba','alcance'])
    result={'eu':eu,'eu_rows':er,'matches':matches,'banks':banks,'hashes':eh,
            'summary':{'fuente_eu':eu.source,'hojas_eu_comparadas':len(er),
                       'homologacion':dict(Counter(r['estado'] for r in hom)),
                       'bancos_original_con_id':len(banks),'incidencias':issues}}
    return result

def cross(modes, ctx, ref):
    """Promueve SOLO hojas con selector/lector concreto revisado en el port."""
    er=ctx['eu_rows'];matches=ctx['matches'];banks=ctx['banks']
    def connect(mode, usa_entry, children, path, needle, note='',archive='DATA_CMN.AFS'):
        p,s=ref(path,needle)
        if not p:return
        for e in er:
            u=matches.get(e['id'])
            if not u or Path(u['archivo']).name.upper()!=archive or u['entrada']!=usa_entry:continue
            if children is not None and u['hijo'] not in children:continue
            k=(Path(e['archivo']).name.upper(),e['entrada'])
            if k not in banks:continue
            original,proof=banks[k]
            dest=modes.setdefault(mode,{})
            old=dest.get(u['id'],{})
            row={**u,**old,'modo':mode,'original_funcion_linea':original,
                 'original_prueba':proof+f'; {e["id"]}; SHA256 completo={ctx["hashes"][e["id"]]} = hoja del port',
                 'port_fichero_linea':p,'port_prueba':s,'cruzado':'si',
                 'limite':'Carga original del banco + lectura de esta hoja; no aprueba imagen/clip/evento ni aspecto. '+note}
            if row.get('estado') not in ('FALTA','mal entendido','el port usa un sustituto'):row['estado']='bien'
            row.setdefault('prioridad','P3');dest[u['id']]=row
    for e in (887,889,893,894):
        connect('IW_MINIJUEGOS',e,['0','1','4','5','8','9'],'src/iw_minijuegos.cpp','d.m_moneda = CargarModelo(pack, 5, 4, 0)')
    connect('IW_MINIJUEGOS',888,['0','1','4','5','8','9'],'src/iw_minijuegos.cpp','d.m_platano = CargarModelo(pack, 9, 8, 0)')
    connect('IW_MINIJUEGOS',896,['0','1','15','16'],'src/iw_minijuegos.cpp','d.m_meta = CargarModelo(pack, 16, 15, 0)')
    connect('IW_MINIJUEGOS',893,['15','16'],'src/iw_minijuegos.cpp','d.m_roca[i] = CargarModelo(pack, 16, 15, 0)')
    connect('IW_MINIJUEGOS',893,['17','18'],'src/iw_minijuegos.cpp','d.m_d = CargarModelo(pack, 18, 17, vd >= 100 ? 2 : vd > 49 ? 1 : 0)')
    for e in (887,888,889,893,894,896):
        children=[r['hijo'] for r in er if r['entrada']==e and r['tipo']=='#ITM' and 'DATA_CMN' in r['archivo']]
        connect('IW_MINIJUEGOS',e,children,'src/iw_minijuegos.cpp','if (!LeerCurso(itm, d))')
    connect('IW_MINIJUEGOS',915,['0','3'],'src/anim_iw.cpp','const std::vector<uint8_t> campo = cmn.Entrada(kIwVuelo)')
    connect('IW_MINIJUEGOS',915,['2'],'src/iw_mini_botones.cpp','g_amc = P::HijoAmb(cmn.Entrada(915), 2)')
    connect('IW_MINIJUEGOS',933,['0','1','15','29'],'src/iw_mini_botones.cpp','const auto amb = cmn.Entrada(933)')
    connect('IW_MINIJUEGOS',895,['0','1'],'src/iw_mini_disparo.cpp','const std::vector<uint8_t> manos = cmn.Entrada(895)')
    connect('IW_MINIJUEGOS',890,['0','1','4','5','6/0']+['6/'+str(i) for i in range(3,25)],'src/iw_mini_disparo.cpp','const std::vector<uint8_t> amb = cmn.Entrada(890)')
    connect('IW_MINIJUEGOS',891,['0','1','2','3','6','7','10','11','13','14','16','17','19','20','23','24','25','26','28/0','28/1']+['28/'+str(i) for i in range(3,21)],'src/iw_mini_vuelo.cpp','const std::vector<uint8_t> amb = cmn.Entrada(891)')
    # Solo BSK/AMM buscados por el lector; no incluir AMK de boca que esta pendiente.
    for e in set(ctx['eu'].va(0x474080,170))&set(range(912,933)):
        ch=[r['hijo'] for r in er if r['entrada']==e and r['tipo'] in ('#AMM','#BSK') and 'DATA_CMN' in r['archivo']]
        connect('IW_MINIJUEGOS',e,ch,'src/anim_iw.cpp','const std::vector<uint8_t> amb = e.entrada == kIwFid ? pack : cmn.Entrada(e.entrada)')
    connect('IW_MINIJUEGOS',934,['0','1','5','6','7','9','13','14','26','29'],'src/iw_minijuegos.cpp','const std::vector<uint8_t> pq = cmn.Entrada(934)')
    connect('IW_MINIJUEGOS',935,['0']+[str(i) for i in range(3,25)],'src/iw_mini_disparo.cpp','d->efectos[h] = ame::Leer(P::HijoAmb(pq, h))')
    for e in (886,899,902,905,908,911):
        connect('IW_MINIJUEGOS',e,['0','3','4','9','10'],'src/iw_minijuegos.cpp','constexpr int hijos[]={3,4,9,10}')
    connect('IW_MINIJUEGOS',902,['1','18','19','20'],'src/iw_minijuegos.cpp','d.kami_fx[i]=dbz3::iwame::ame::Leer')
    connect('IW_MINIJUEGOS',813,['0','1','6'],'src/iw_minijuegos.cpp','const auto hit=cmn.Entrada(813)')
    for e in (938,954):connect('IW_MINIJUEGOS',e,None,'src/anim_iw.cpp','archivo.Entrada(size_t(se.banco))')
    connect('IW_MINIJUEGOS',6,['0'],'src/iw_minijuegos.cpp','const std::vector<uint8_t> ui = P::HijoAmb(usa.Entrada(6), 0)',archive='DATA_USA.AFS')
    connect('IW_MINIJUEGOS',5,['0','1'],'src/iw_mini_vuelo.cpp','const auto paquete=usa.Entrada(5)',archive='DATA_USA.AFS')
    connect('IW_MINIJUEGOS',7,['0'],'src/iw_minijuegos.cpp','ui7 = P::HijoAmb(usa.Entrada(7), 0)',archive='DATA_USA.AFS')
    connect('IW_MINIJUEGOS',9,['1'],'src/iw_mini_disparo.cpp','const std::vector<uint8_t> hud = P::HijoAmb(usa.Entrada(9), 1)',archive='DATA_USA.AFS')
    connect('IW_CAMPANA',1127,None,'src/pasarela_iw.cpp','CargarHabilidadesIW(cmn.Entrada(1127))')
    connect('IW_CAMPANA',1093,['11'],'src/pasarela_iw.cpp','const std::vector<uint8_t> e = cmn.Entrada(1093)')
    connect('IW_CAMPANA',1093,['0','1','2'],'src/pasarela_iw.cpp','const std::vector<uint8_t> e = cmn.Entrada(1093)')
    connect('IW_CAMPANA',334,['0','1','2','3','4','5','6'],'src/pasarela_iw.cpp','const std::vector<uint8_t> b334 = usa.Entrada(334)',archive='DATA_USA.AFS')
    connect('IW_CAMPANA',333,['0'],'src/pasarela_iw.cpp','out.road = todas(HijoAmb(b333, 0))',archive='DATA_USA.AFS')
    for e in range(132,228):
        connect('IW_CAMPANA',e,['0','1'],'src/pasarela_b1.cpp','const std::vector<uint8_t> desc = hijo(0), tit = hijo(1)',archive='DATA_USA.AFS')
    return connect
