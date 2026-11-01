"""Modos propios y carencias auditadas, separados de las adscripciones candidatas."""
import hashlib
from pathlib import Path
import tomllib


def modos_propios(modes, inventory, ref, root):
    native = {int(r['entrada']):r for r in inventory if r['juego']=='b3hd'}
    manifest = root/'mod center hd/tactics_manifest.toml'
    data = tomllib.loads(manifest.read_text(encoding='utf-8-sig'))
    selected = {}
    for unit in data.get('unidad',[]):
        for key in ('modelo','banco','anm_forma'):
            entry = unit.get(key)
            if isinstance(entry,int): selected.setdefault(entry,[]).append(f'{unit["id"]}.{key}')
    dest=modes.setdefault('DRAGON_TACTICS',{})
    for entry, users in selected.items():
        if entry not in native: continue
        p,s=ref('src/tactics/entrada.cpp','CargarCatalogo(datos + "/tactics_units.toml"')
        item=native[entry]
        dest[item['id']]={**item,'modo':'DRAGON_TACTICS','estado':'NO COMPROBADO',
             'port_fichero_linea':p,'port_prueba':s+' | manifest: '+', '.join(users),
             'original_funcion_linea':'NO APLICA: Dragon Tactics es modo propio',
             'original_prueba':'Reutiliza assets B3 HD; no hay modo Dragon Tactics en los decomps PS2 suministrados.',
             'cruzado':'no_aplica','prioridad':'P2','impacto':'Sin aprobacion visual de cada clip',
             'arreglo':'Resolver codigos de tecnica/dano/derrota/carga/transformacion del manifiesto y medir su consumidor.',
             'limite':'Manifest declara el ID; no se descomprime LZX ni se prueba que el dibujante use cada clip.'}
    # Cada selector declarado es un asset/configuracion del port con identidad propia.
    # Su existencia no equivale a una animacion extraida del juego.
    for unit in data.get('unidad',[]):
        for clip, values in unit.get('clips',{}).items():
            key=f'config:tactics:{unit["id"]}:clips.{clip}'
            p,s=ref('mod center hd/tactics_manifest.toml',f'id = "{unit["id"]}"')
            status=values.get('estado','no declarado')
            dest[key]=dict(modo='DRAGON_TACTICS',juego='port',archivo='mod center hd/tactics_manifest.toml',
                entrada=unit.get('banco',-1),hijo=f'{unit["id"]}/clips.{clip}',id=key,
                tipo='CONFIG selector de animacion (no es clip extraido)',bytes=0,offset_archivo=0,
                estado='mal entendido' if status in ('inferido','pendiente') else 'NO COMPROBADO',
                port_fichero_linea=p,port_prueba=str(values),cruzado='no_aplica',
                original_funcion_linea='NO APLICA: modo propio',original_prueba='Selector del manifiesto local',
                impacto='Codigo pendiente/inferido; efecto visible no comprobado' if status in ('inferido','pendiente') else 'Declarado medido; no repetido en este censo',
                arreglo='Vincular codigo y banco al lector real y comparar pose/tiempo; no convertir inferido en medido.',
                limite='Fila de configuracion; bytes=0 significa selector, no recurso vacio. La linea cita la unidad, no su clip.',
                prioridad='P2')
    # Heroe: configuracion original del proyecto y cuerpos base definidos por codigo.
    dest=modes.setdefault('HEROE',{})
    for folder in ('res/heroe','mod center hd/cac'):
        for path in sorted((root/folder).glob('*.toml')):
            rel=str(path.relative_to(root)).replace('\\','/')
            raw=path.read_bytes()
            if path.name=='sagas.toml':
                p,s=ref('src/heroe.cpp','dbz3::ModsRoot().parent_path() / "res" / "heroe" / "sagas.toml"')
            elif path.name=='desbloqueos.toml':
                p,s=ref('src/heroe.cpp','desbloqueos.toml')
            else:
                p,s=ref('src/heroe_ui.cpp','piezas_cac.toml')
            key=f'port:{rel}'
            dest[key]=dict(modo='HEROE',juego='port',archivo=rel,entrada=-1,hijo='',id=key,tipo='CONFIG TOML',
                 bytes=len(raw),offset_archivo=0,cabecera_sha256=hashlib.sha256(raw[:128]).hexdigest(),
                 estado='NO COMPROBADO',port_fichero_linea=p,port_prueba=s,
                 original_funcion_linea='NO APLICA: modo Heroe propio',original_prueba='Inspiracion UT/XV2 no acredita equivalencia de modo/asset.',
                 cruzado='no_aplica',impacto='Uso de este TOML individual no demostrado salvo sagas.toml',
                 arreglo='Seguir importador CAC hasta pieza/paquete real del jugador y registrar ID y lector.',
                 limite='Configuraciones existentes; la referencia generica CAC no prueba consumo de todas. No inventar IDs UT/XV2.',prioridad='P3')
    for key, typ, needle in [
            ('cuerpo','AWO/AZT del cuerpo B3','+2 ID cuyo modelo se carga'),
            ('moveset','8 bancos ANM del donante','los 8 fids ANM del moveset'),
            ('camara','CAM del donante','CAM (+0x20'),
            ('voz','Bancos de gritos del donante','su banco de gritos'),
            ('capsulas','Capsulas por defecto del donante','sus capsulas por defecto'),
            ('ut','Piezas externas Ultimate Tenkaichi','geometría sale del creador UT'),
            ('xv2','Piezas externas Xenoverse 2','cuerpos propios con piezas del creador')]:
        p,s=ref('src/heroe.cpp',needle)
        ident=f'dependencia_sin_id:HEROE:{key}'
        dest[ident]=dict(modo='HEROE',juego='port',archivo='src/heroe.cpp',entrada=-1,hijo=key,id=ident,
             tipo=typ,bytes=0,offset_archivo=0,estado='NO COMPROBADO',
             port_fichero_linea=p,port_prueba=s,cruzado='no_aplica',
             original_funcion_linea='NO APLICA al modo propio; donantes originales por resolver',
             original_prueba='Dependencia documentada en fuente; no es un asset fisico identificado.',
             impacto='Sin traza de ID donante y carga efectiva; no se aprueba ni se declara ausente.',
             arreglo='Registrar ID real al resolver cuerpo/estilo y seguir la carga de cada banco.',
             limite='DEPENDENCIA SIN ID: no sumar como hoja fisica; cero bytes no significa asset vacio.',prioridad='P3')


def hallazgos(ref, dec, decomp):
    result=[]
    def put(id,mode,priority,status,asset,path,needle,game,addr,impact,fix,limit):
        p,s=ref(path,needle)
        fn=dec.get(game,{}).get(addr.zfill(8))
        original=f'{Path(decomp)/{"iw":"iw_sles55347.c","b1":"b1.c","b2":"b2.c"}.get(game,"")}:{fn["line"]} {fn["name"]}' if fn else 'NO COMPROBADO / no aplica'
        result.append(dict(id=id,modo=mode,prioridad=priority,estado=status,asset_o_dependencia=asset,
             port_fichero_linea=p,port_prueba=s,original_funcion_linea=original,
             impacto=impact,arreglo=fix,limite=limit))
    put('AST01','IW_MINIJUEGOS','P1','mal entendido','Cell 0x7DD: identidad/forma y modelo HD',
        'src/iw_minijuegos.cpp','constexpr int kGoku = 0, kCell = 33', 'iw','298760',
        'Cell: la memoria pide primera forma y el codigo/comentarios antiguos dicen semiperfecto; aspecto actual sin comparar.',
        'Comprobar forma en la copia original y enlazar ESA forma de B3 HD con banco IW; corregir las tablas contradictorias.',
        'No afirma que el modelo PS2 siga activo: no hay llamadas al cuerpo PS2 en los minijuegos revisados. La funcion original no fija forma aqui.')
    if 'forma 0 = primera forma' in result[-1]['port_prueba']:
        result[-1].update(prioridad='P2',estado='NO COMPROBADO',
            impacto='Cell: una edicion concurrente ya declara primera forma HD; falta validar cuerpo y retrato en pantalla.',
            arreglo='Contrastar cuerpo, retrato y banco de animacion con el original; actualizar antecedentes que aun dicen semiperfecto.',
            limite='Correccion vista en disco durante el censo; no se reclama el arreglo ni se da por validado visualmente.')
    put('AST02','IW_MINIJUEGOS','P1','FALTA','AP8 → #AMK → poses #AMM de boca → matrices locales B3',
        'src/anim_iw.cpp','AP8 pendiente: AMK solo selecciona','iw','207180',
        'El consumidor de eventos deja pendiente la boca; la sincronia labial de esos eventos no queda conectada.',
        'Leer poses AMM reales, mapear nueve huesos por nombre y llamar al Motor/AP8 y CopiarBoca desde DispararEvento.',
        'Falta de conexion demostrada por rama ejecutable; no se midio frecuencia ni cada boca en pantalla.')
    put('AST03','IW_MINIJUEGOS','P3','NO COMPROBADO','rocas de 0x7D9: CMN893/16 frente a recursos del campo900',
        'src/iw_minijuegos.cpp','d.m_roca[i] = CargarModelo(pack, 16, 15, 0)', 'iw','2a9bf0',
        'Se repite variante0, pero el dictamen anterior citaba321350: esa funcion dibuja objetos D, no las rocas.',
        'Seguir Ki_UpdateProjectileFlight y la preparacion de slots del campo900 hasta malla/UV/variante; no aplicarles el umbral50/100 de los objetos D.',
        'Rectificacion solAST2: no hay evidencia suficiente para declarar incorrecta la variante de roca; sin prueba visual nueva.')
    put('AST04','SATAN_B1','P2','el port usa un sustituto','DATA_SP 669: simbolos de clave/contraseña',
        'src/pasarela.cpp','Los simbolos del original son sprites','b1','235310',
        'La clave se representa con caracteres y fuente anfitriona; el arte de los simbolos originales se sustituye.',
        'Vincular los 12 simbolos resultantes a sus sprites originales en vez de renderizar ASCII.',
        'La funcion original genera caracteres; el uso del atlas concreto sigue pendiente de cruce por sprite.')
    put('AST05','DRAGON_TACTICS','P2','mal entendido','tactics_manifest.toml: tecnica inferida; dano/derrota/carga/transformacion pendientes',
        'mod center hd/tactics_manifest.toml','nota = "accion 20 del motor, sin codigo BSK confirmado"','', '',
        'Hay selectores de animacion pendientes o inferidos; no acredita animaciones fieles para todas las acciones.',
        'Cerrar banco/codigo/tiempo por accion y unidad y contrastar el clip realmente solicitado por el dibujante.',
        'Modo propio: no es contenido ausente de un original Tactics; el censo separa selector de clip extraido.')
    put('AST06','HEROE','P3','NO COMPROBADO','piezas UT/XV2, modelos B3 y sagas locales',
        'src/heroe.cpp','dbz3::ModsRoot().parent_path() / "res" / "heroe" / "sagas.toml"','', '',
        'Falta traza por pieza externa hasta archivo e ID; no permite aprobar apariencia completa.',
        'Registrar pieza → paquete del jugador → conversor → modelo generado → consumidor y medir el resultado.',
        'Decomp entregado solo cubre B1/B2/IW; no UT ni XV2. No se inventan consumidores de esos juegos.')
    put('AST07','IW_MINIJUEGOS','P1','FALTA','Eventos AP7 tipo4; efectos de Super/Ult IW',
        'src/iw_ap_eventos.h','ejecutar recursos de tecnicas/capsulas','iw','1bf610',
        'Las animaciones pueden pedir tecnicas sin que su generador visual IW quede conectado; conservar el pedido no dibuja el efecto.',
        'Conectar el despacho tipo4 de DispararEvento con Preparar4 y los recursos IW de la ruta elegida, con anclaje y reloj del actor.',
        'Rama ejecutable sin consumidor tipo4 revisada; EVENTOS_IW_BSK separa presencia y selector. No se atribuye todo rayo manual de QTE a esta falta.')
    put('AST08','IW_MINIJUEGOS','P3','NO COMPROBADO','AP5: CMN813 atlas0 y AME1/6 solo en campo',
        'src/anim_iw.cpp','if(iwmini::EscenaApCampo()!=0)return false','iw','1fbe00',
        'La via de impactos de eventos solo acepta campo y subtipos0/1; los demas quedan pendientes aunque el BSK los contenga.',
        'Resolver subtipo y ancla en la tabla4941E0 y conectar su generador; conservar el filtro original y no tratar subtipo como indiceAME.',
        'EscenaApCampo devuelve-1 en modulos de botones/disparo/vuelo. El censo BSK actual no prueba eventos5 seleccionados: es un limite del consumidor, no un fallo visible demostrado.')
    put('AST09','IW_MINIJUEGOS','P2','FALTA','Audio: AP1/AP2 con handle y AP7 al cambiar forma',
        'src/iw_ap_eventos.h','requiere handle/control original','iw','22fc50',
        'Hay rutas sonoras pendientes: secuencias con handle y tablas de voz durante cambios de forma; no es ausencia de todos los sonidos.',
        'Conectar control de handle para valores>=1000 y la tabla ADX de transicion de forma; usar CMN954, bancoSE del actor y ADX segun la ruta.',
        'One-shot, voces y pasos ya tienen lectores reales. EVENTOS_IW_BSK identifica solicitudes; falta observar disparo/sonido por evento.')
    put('AST10','SATAN_B1','P2','FALTA','DATA_SP665/667 AMA; atlas666/668',
        'src/pasarela_b1.cpp','const std::vector<uint8_t> pantalla = afs.Entry(size_t(banco - 3))','b1','22d5f0',
        'Satan carga el arte de portada y resultado pero omite las dos AMA que organiza el original; sus movimientos y tiempos no quedan conectados.',
        'Leer AMA665/667 junto a AMT666/668 y reproducir los objetos/tiempos originales con el motor AMA.',
        'Literales originales22D5F0/232FB0; lectura del port revisada. No se inventa el movimiento de un sprite concreto.')
    put('AST11','DRAGON_TACTICS','P2','el port usa un sustituto','data_cmn504/AZT textura33;553/AZT textura36',
        'src/tactics/entrada.cpp','const S::Pic impacto = S::Img("fx_impacto")','', '',
        'El rayo de Tactics usa tiras del halo y color generico; fx_rayo504/33 y fx_brillo553/36 se extraen pero no se solicitan.',
        'Conectar esas hojas al tipo de tecnica; para Kamehameha fiel seguir ademas guion/modelo/animacion del banco del personaje, no solo tintar el halo.',
        'Siete hojas DDS verificadas en EFECTOS_B3HD_TACTICS.csv; no se afirma que504/33 sea por si sola el Kamehameha completo.')
    return result
