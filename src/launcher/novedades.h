// dbz3 - "Novedades" del launcher: lo nuevo de cada version, en palabras de jugador (ES/EN).
//
// Al publicar una version con cambios visibles, anade un bloque ARRIBA con su numero ("1.4.6"): el
// launcher muestra el bloque mas nuevo que no supere la version instalada y lo abre solo una vez tras
// actualizar (dbz3_news_seen). Pocas lineas, sin jerga; Tag Team y Online siempre como beta.
#pragma once

namespace dbz3::launcher {

struct NewsItem {
  const char* section_es;   // nullptr = misma seccion que la linea anterior
  const char* section_en;
  const char* es;
  const char* en;
};

struct NewsBlock {
  const char* version;
  const NewsItem* items;
  int count;
};

inline constexpr NewsItem kNews1555[] = {
    {"Infinite World (Dragon Mission)", "Infinite World (Dragon Mission)",
     "La historia ya enlaza sin cortes el mapa, el vídeo, el minijuego, el resultado y la vuelta al mapa.",
     "The story now flows smoothly from the map to the video, the minigame, the result and back to the map."},
    {nullptr, nullptr,
     "Los minijuegos de tierra de Raditz y Bubbles se pueden completar; la barra de energía sale sola y amarilla.",
     "Raditz and Bubbles' ground minigames can be completed; the energy bar is a single yellow bar."},
    {nullptr, nullptr,
     "Al terminar un minijuego de tierra, Goku queda en cuadro y la letra del rango se ve grande.",
     "When a ground minigame ends, Goku stays in frame and the rank letter is shown large."},
    {nullptr, nullptr,
     "En los vuelos, el luchador se ve de espaldas; la cámara ya está colocada al READY y la flecha de los aros va bajo los pies.",
     "In the flights, the fighter is seen from behind; the camera is already in place at READY and the ring arrow sits under the feet."},
    {nullptr, nullptr,
     "Las nubes y la niebla amarilla vuelven a verse, y el vuelo de Gohan tras Goten sigue la ruta y los aros del original.",
     "The clouds and the yellow fog are back, and Gohan's flight after Goten follows the original route and rings."},
    {nullptr, nullptr,
     "Al empezar una misión, el combate usa la música de batalla del original.",
     "When a mission starts, the battle uses the original battle music."},
    {"Dragon World (Budokai 2)", "Dragon World (Budokai 2)",
     "Las nueve fases del tablero se completan sin atascarse.",
     "All nine board phases can be completed without getting stuck."},
    {nullptr, nullptr,
     "Atacar a un rival en el tablero empieza el combate directamente, sin pasar por el cursor de destino.",
     "Attacking a rival on the board starts the battle directly, without going through the destination cursor."},
    {"Nave de Babidi (beta)", "Babidi's ship (beta)",
     "Se entra y se cambia de planta, sale la ventana «Get skill!» y la prueba de combate empieza con su tarjeta VS.",
     "You can enter and change floors, the “Get skill!” window appears and the battle trial starts with its VS card."},
    {"Leyenda de Mr. Satán", "Mr. Satan legend",
     "Ronda 1: la escena de los Juegos de Cell se ve entera con sus subtítulos, el coche de Satán llega por la carretera, la cámara lo sigue como en el original y hay menos tiempo en negro entre escenas.",
     "Round 1: the Cell Games scene plays in full with its subtitles, Satan's car arrives along the road, the camera follows him as in the original and there is less black screen between scenes."},
    {nullptr, nullptr,
     "La Leyenda avanza por sus rondas de principio a fin.",
     "The legend now moves through its rounds from start to finish."},
    {"Modo Héroe (beta)", "Hero Mode (beta)",
     "38 de los 52 guantes ya encajan en las poses de reposo, golpe, golpe recibido y caída.",
     "38 of the 52 gloves now fit in the idle, punch, hit and fall poses."},
    {nullptr, nullptr,
     "La lista de estilos de lucha muestra los 23 estilos.",
     "The fighting style list shows all 23 styles."},
    {"Interfaz y HUD", "Interface and HUD",
     "Los paneles de resultado de los minijuegos de disparo y de botones muestran el rango y los premios sin solaparse.",
     "The shooting and button minigame result panels show the rank and the rewards without overlapping."},
    {nullptr, nullptr,
     "El panel de Bubbles ya muestra el rótulo «ITEM COLLECTION %» como el original.",
     "Bubbles' panel now shows the “ITEM COLLECTION %” label like the original."},
    {"Cámara libre (beta)", "Free camera (beta)",
     "Nueva cámara libre en el menú F1 para mirar la escena desde otro ángulo. Viene apagada y no se guarda.",
     "New free camera in the F1 menu to look at the scene from another angle. It starts off and is not saved."},
};

inline constexpr NewsItem kNews1554[] = {
    {"Leyenda de Mr. Satán", "Mr. Satan legend",
     "Las presentaciones de cada ronda usan ahora las cámaras de Budokai 1: los luchadores salen enteros, sin bandas negras y sin recortes.",
     "Each round's introduction now uses Budokai 1's cameras: fighters appear whole, with no black bars and no cut-off edges."},
    {nullptr, nullptr,
     "Las poses se parecen más a las del original. Trunks sale con armadura, Ten Shin Han sin camiseta y Vegeta con armadura, como en Budokai 1. Sin rótulo de Siguiente o Saltar en las tomas.",
     "Poses are closer to the original. Trunks wears his armour, Ten Shin Han has no shirt and Vegeta wears his armour, as in Budokai 1. No Next or Skip prompt during the shots."},
    {"Infinite World (Dragon Mission)", "Infinite World (Dragon Mission)",
     "Las cámaras de los minijuegos vuelven a ser como en el original: botones, disparo Kienzan, rayos, disparo de Ten contra Cell, el campo y el vuelo.",
     "Minigame cameras are back to the original: buttons, Kienzan shot, energy beams, Ten's shot against Cell, the field and the flight."},
    {"Tag Team (beta)", "Tag Team (beta)",
     "El compañero que controla el ordenador ya cambia de luchador cuando el suyo tiene poca vida.",
     "The partner controlled by the computer now swaps in when its fighter is low on health."},
    {"Dragon World (Budokai 2)", "Dragon World (Budokai 2)",
     "Arreglado que en el tablero sonara la música de fondo del juego por debajo de la de Budokai 2.",
     "Fixed the game's background music playing underneath the Budokai 2 music on the board."},
    {"Online (beta)", "Online (beta)",
     "La revancha ya empieza sincronizada como un combate nuevo en los dos jugadores.",
     "Rematches now start in sync as a new battle for both players."},
    {"Más estable", "More stable",
     "Si el juego se cerraba a mitad de preparar los personajes, ya no se queda bloqueado en los siguientes arranques. Un mod con una cápsula mal escrita ya no cierra el juego.",
     "If the game closed while preparing the characters, it no longer stays stuck on the next launches. A mod with a badly written capsule no longer crashes the game."},
    {"Aviso", "Notice",
     "La primera vez que abras el juego tras actualizar, los escenarios de PS2 se preparan de nuevo. Tarda un poco; es normal.",
     "The first time you open the game after updating, the PS2 stages are prepared again. It takes a little while; this is normal."},
};

inline constexpr NewsItem kNews1553[] = {
    {"Combate", "Battle",
     "Arreglado: al empezar el combate la cámara se quedaba mirando al suelo (solo se veían las barras de vida) y se volvía loca al agarrar. Vuelve a ser la cámara original del juego.",
     "Fixed: at the start of a battle the camera got stuck looking at the ground (only the health bars were visible) and went crazy on grabs. It is the game's original camera again."},
    {"Pack HD de texturas", "HD texture pack",
     "Nueva V2 del pack de SSGPrinceVegeta en Nova's Mods, junto a la V1 (cada una con lo que pesa). Arreglado el cierre en combate con pack HD y la bajada a unos 25 FPS.",
     "New V2 of SSGPrinceVegeta's pack in Nova's Mods, next to V1 (each showing its download size). Fixed the in-battle crash with an HD pack and the drop to about 25 FPS."},
};

inline constexpr NewsItem kNews1552[] = {
    {"Modo Héroe (beta)", "Hero Mode (beta)",
     "Saga 4 jugable. Las sagas 2, 3 y 4 ya se juegan en el mapa de la Tierra, no en Namek.",
     "Saga 4 is playable. Sagas 2, 3 and 4 now take place on the Earth map, not Namek."},
    {nullptr, nullptr,
     "Peinados de Budokai 3 bien ajustados a la cabeza: Goku, Vegeta, Gohan (Cell) y Goku niño.",
     "Budokai 3 hairstyles that fit the head properly: Goku, Vegeta, Gohan (Cell) and Kid Goku."},
    {nullptr, nullptr,
     "Arreglado: al elegir el moveset de otro luchador en el apartado, el combate seguía usando los golpes del cuerpo propio.",
     "Fixed: choosing another fighter's moveset in the creator kept the body's own moves in battle."},
    {"Leyenda de Mr. Satán", "Mr. Satan legend",
     "Arreglado: en la escena final del ring de Cell no se veían Satán ni el suelo del ring.",
     "Fixed: Mr. Satan and the ring floor were invisible in the final Cell ring scene."},
    {nullptr, nullptr,
     "En la escena final Satán entrecierra los ojos como en el original.",
     "In the final scene Mr. Satan squints like in the original."},
    {"Online (beta)", "Online (beta)",
     "El informe de desincronía ya se guarda; aviso claro si el anfitrión no contesta; revancha y vuelta al menú probadas; si se corta en pleno combate puedes pausar y salir.",
     "The desync report is now saved; clear message if the host doesn't answer; rematch and return to menu tested; if the connection drops mid-fight you can pause and quit."},
};

inline constexpr NewsItem kNews1551[] = {
    {"Online (beta)", "Online (beta)",
     "Pantalla online más clara: 4 pasos visibles, estado de conexión con color y ping, copiar o pegar el código con X y errores explicados con qué hacer.",
     "Clearer online screen: 4 visible steps, connection status with colour and ping, copy or paste the code with X, and errors that say what to do."},
    {nullptr, nullptr,
     "Si hay una desincronía, el juego guarda un informe en la carpeta informes_online para que nos lo envíes.",
     "If a desync happens, the game saves a report in the informes_online folder so you can send it to us."},
    {"Modo Héroe (beta)", "Hero Mode (beta)",
     "Arreglado: la 1.5.5 no instalaba los datos de las sagas (escenarios de la Tierra y catálogo de piezas).",
     "Fixed: 1.5.5 did not install the saga data (Earth stages and parts catalogue)."},
};

inline constexpr NewsItem kNews155[] = {
    {"Infinite World (experimental)", "Infinite World (experimental)",
     "96/96 misiones disponibles en USA y Europa.",
     "All 96 missions are playable in the US and European versions."},
    {"Nave de Babidi (beta)", "Babidi's ship (beta)",
     "Funcionan las cuatro plantas; puedes volver a la nave tras editar habilidades.",
     "All four floors work, and you can return to the ship after editing skills."},
    {"Budokai 2 (experimental)", "Budokai 2 (experimental)",
     "El tablero arranca sin cierres y los combates con cambio de zona funcionan.",
     "The board starts without crashes, and battles with area changes work."},
    {"Leyenda de Mr. Satán", "Mr. Satan legend",
     "La presentación de la ronda 3 ya funciona.",
     "The round 3 introduction now works."},
    {"Modo Héroe (beta)", "Hero Mode (beta)",
     "Sagas 1-3 jugables; 4-8 en desarrollo. Piezas de Budokai 3 por defecto; piezas de XV2 opcionales (experimental).",
     "Sagas 1-3 are playable; 4-8 are in development. Budokai 3 parts by default; optional XV2 parts (experimental)."},
    {"Online (beta)", "Online (beta)",
     "Las peleas se mantienen sincronizadas en las pruebas; el modo Online sigue en beta.",
     "Fights stay in sync in testing; Online is still in beta."},
    {"Rendimiento", "Performance",
     "Nueva opción de resolución interna 480p (640x480) para PCs y portátiles modestos.",
     "New 480p (640x480) internal resolution option for modest PCs and laptops."},
};

inline constexpr NewsItem kNews1514[] = {
    {"Budokai 2 (experimental)", "Budokai 2 (experimental)",
     "El tablero se ve con el mismo encuadre que en PS2, con Gotenks, Vegito y Shenron, piezas animadas y diálogos con la fuente y los retratos originales. Nave de Babidi con su cielo y nubes originales.",
     "The board uses the same framing as on PS2, with Gotenks, Vegito and Shenron, animated pieces and dialogue with the original font and portraits. Babidi's ship with its original sky and clouds."},
    {"Infinite World (experimental)", "Infinite World (experimental)",
     "Vuelos con la ISO europea, minijuego de la barra roja con su cámara y fondo como el original, y el mapa bien colocado en 4:3, 16:10 y 21:9.",
     "Flight minigames with the European ISO, red bar minigame with its original camera and background, and the map correctly placed in 4:3, 16:10 and 21:9."},
    {"Modo Héroe (beta)", "Hero Mode (beta)",
     "Lista de personajes con duplicar y archivar, vista 3D, guardado nuevo (se convierte solo) y cambio de moveset dentro del juego sin reiniciar.",
     "Character list with duplicate and archive, 3D preview, new save format (converted automatically) and moveset changes in game without restarting."},
    {"Leyenda de Mr. Satán", "Legend of Mr. Satan",
     "Ronda 1 con los tiempos y subtítulos del original.",
     "Round 1 with the original timing and subtitles."},
    {"Rendimiento y arreglos", "Performance and fixes",
     "El pack HD ya no cierra el juego al activarlo y desactivarlo, nuevo modo rendimiento para gráficas modestas y aviso en el launcher si no se encuentran los escenarios de PS2.",
     "The HD pack no longer crashes the game when toggled, new performance mode for modest GPUs and a launcher notice when PS2 stages are not found."},
};

inline constexpr NewsItem kNews1513[] = {
    {"Budokai 2 (experimental)", "Budokai 2 (experimental)",
     "Vuelta del combate con el iris de la Bola de Dragón, ficha de cápsula como el original y arreglado un cierre al empezar el tablero.",
     "Return from battle with the Dragon Ball iris, capsule card like the original and a crash when the board starts fixed."},
    {"Leyenda de Mr. Satán", "Legend of Mr. Satan",
     "Escena previa de los Juegos de Cell y el ring de Cell entero de Budokai 1.",
     "Cell Games intro scene and the full Cell ring from Budokai 1."},
    {"Infinite World (experimental)", "Infinite World (experimental)",
     "Vale también la ISO europea. Título de cada minijuego en el panel de resultados.",
     "The European ISO works too. Each minigame's title on the results panel."},
    {"Arreglos", "Fixes",
     "Escenarios de PS2 en equipos sin Office, ranking de luchadores, un cierre con ports y cápsulas propias, y el pack HD reintenta y sigue la descarga si se corta.",
     "PS2 stages on PCs without Office, fighter ranking, a crash with ports and custom capsules, and the HD pack retries and resumes if the download breaks."},
};

inline constexpr NewsItem kNews1512[] = {
    {"Escenarios", "Stages",
     "Los escenarios de Budokai 2, Budokai 1 e Infinite World se crean desde tu ISO sin instalar nada más (Templo de Kami, Montañas nevadas...).",
     "Budokai 2, Budokai 1 and Infinite World stages are created from your ISO with nothing else to install (Kami's Lookout, Snowy Mountains...)."},
    {"Budokai 2 (experimental)", "Budokai 2 (experimental)",
     "Ya se puede salir del tablero en cualquier momento.",
     "You can now leave the board at any time."},
    {"Modo Héroe (beta)", "Hero Mode (beta)",
     "Tu cara en las escenas de saga, peinado también en cuerpos calvos, scouter y nombres con acentos.",
     "Your face in saga scenes, hairstyles on bald bodies too, scouter and names with accents."},
};

inline constexpr NewsItem kNews1511[] = {
    {"Infinite World (experimental)", "Infinite World (experimental)",
     "Minijuegos de botones como el original: los luchadores se miran y se golpean, cámara de IW y su panel azul con siluetas.",
     "Button minigames like the original: fighters face and hit each other, IW camera and its blue panel with silhouettes."},
    {nullptr, nullptr,
     "Pantalla de resultados de IW al acabar cada minijuego y tabla de tiempos en la nave de Goku.",
     "IW results screen after each minigame and the time table in Goku's ship."},
    {nullptr, nullptr,
     "Arreglado un cierre contra Paikuhan en Dragon Mission.",
     "Fixed a crash against Pikkon in Dragon Mission."},
    {"Modo Héroe (beta)", "Hero Mode (beta)",
     "Tu héroe es un personaje propio: su nombre, su cara y su peinado, colores y logo también en combate (reinicia tras crearlo).",
     "Your hero is its own character: its name, face, hair, colours and logo also in battle (restart after creating it)."},
    {nullptr, nullptr,
     "Los maestros y los movesets se desbloquean al avanzar por las sagas.",
     "Masters and movesets unlock as you progress through the sagas."},
    {"Leyenda de Mr. Satán", "Legend of Mr. Satan",
     "Título, prólogo y resultados como en el Budokai original.",
     "Title, prologue and results like the original Budokai."},
    {"Budokai 2 (experimental)", "Budokai 2 (experimental)",
     "Premios de cápsula, subidas de ataque y defensa y sonidos del tablero como el original.",
     "Capsule prizes, attack/defence boosts and board sounds like the original."},
};

inline constexpr NewsItem kNews151[] = {
    {"Budokai 2 (experimental)", "Budokai 2 (experimental)",
     "El Mundo Dragón arranca con su intro y las charlas del original; los Saibaman brotan del suelo y suena su música.",
     "Dragon World starts with its intro and the original dialogues; Saibamen sprout from the ground and its music plays."},
    {nullptr, nullptr, "Saibaman de colores: cada combate del tablero contra uno sale de un color.",
     "Coloured Saibamen: each board battle against one has a different colour."},
    {nullptr, nullptr,
     "Tablero más fiel: flecha roja, columna de vida, retratos, sonidos, música y escenarios de Budokai 2 en cada casilla.",
     "More faithful board: red arrow, life column, portraits, sounds, music and Budokai 2 stages on every tile."},
    {nullptr, nullptr, "Nave de Babidi (beta): pantallas y pruebas como en el original.",
     "Babidi's ship (beta): screens and trials as in the original."},
    {"Infinite World (experimental)", "Infinite World (experimental)",
     "El entrenamiento de Goku y Gohan ya no se descoloca, y las misiones dicen qué personaje falta.",
     "The Goku and Gohan training no longer glitches, and missions tell you which character is missing."},
    {nullptr, nullptr, "Pack Infinite World en Nova's Mods: marca los personajes y se crean con tu copia del juego.",
     "Infinite World pack in Nova's Mods: tick the characters and they are built from your game copy."},
    {nullptr, nullptr, "Mapa con bordes, música por zona y animaciones más suaves.",
     "Map with borders, music per area and smoother animations."},
    {"Modo Héroe (beta)", "Hero mode (beta)", "Cambia el moveset de tu héroe dentro del juego.",
     "Change your hero's moveset inside the game."},
    {"Mods", "Mods",
     "Janemba y las Fuerzas Ginyu en Nova's Mods con sus propias voces, y huecos de personaje que se conservan entre versiones.",
     "Janemba and the Ginyu Force in Nova's Mods with their own voices, and character slots kept between versions."},
    {"Launcher", "Launcher",
     "Arreglado: en algunos equipos no cargaba Nova's Mods ni la comprobación de versión.",
     "Fixed: on some PCs Nova's Mods and the update check did not load."},
    {nullptr, nullptr, "Nuevos idiomas: portugués (Brasil) y japonés.",
     "New languages: Portuguese (Brazil) and Japanese."},
    {nullptr, nullptr, "Arranca antes tras actualizar: los retratos de los personajes ya no se vuelven a dibujar.",
     "Starts faster after updating: character portraits are no longer redrawn."},
};

inline constexpr NewsItem kNews150[] = {
    {"Dragon HUB", "Dragon HUB",
     "Placas de rango, fondos y anfitriones nuevos, y marcas BETA / EXPERIMENTAL en lo que está en construcción.",
     "Rank plates, new backgrounds and hosts, and BETA / EXPERIMENTAL labels on what is still being built."},
    {"Tag Team (beta)", "Tag Team (beta)", "Relevos con teletransporte, también entre luchadores del ordenador.",
     "Teleport relays, also between computer fighters."},
    {"Mods", "Mods", "Los mods ya funcionan jugando directamente desde la ISO.",
     "Mods now work when playing straight from the ISO."},
    {"Modos nuevos", "New modes",
     "Misión 100, Modo Héroe (beta), Leyenda de Mr. Satán (beta) y las campañas de Infinite World y Budokai 2 "
     "(experimental): elige tu ISO de PS2 en la pestaña Juego.",
     "Mission 100, Hero mode (beta), Mr. Satan legend (beta) and the Infinite World and Budokai 2 campaigns "
     "(experimental): pick your PS2 ISO in the Game tab."},
    {"Launcher", "Launcher", "Lanzador y menú F1 renovados, y todo traducido a cinco idiomas.",
     "Renewed launcher and F1 menu, and everything translated into five languages."},
};

inline constexpr NewsItem kNews145[] = {
    {"Tag Team (beta)", "Tag Team (beta)",
     "Si te sacan del ring y te queda compañero, entra él y el combate sigue.",
     "Ring out with a partner still standing: they come in and the fight goes on."},
    {nullptr, nullptr, "El que entra llega volando y carga ki con la pose del propio juego.",
     "The incoming fighter flies in and powers up with the game's own ki pose."},
    {"Online (beta)", "Online (beta)",
     "Tener mods distintos ya no corta la conexión: el panel dice qué falta y lo instala desde Nova's Mods.",
     "Different mods no longer drop the connection: the panel shows what is missing and installs it from "
     "Nova's Mods."},
    {nullptr, nullptr, "Vuelve la apertura automática del puerto del router (UPnP).",
     "Automatic router port opening (UPnP) is back."},
    {"Texturas HD", "HD textures",
     "Los packs de texturas HD cargan sin tirones y se aplican nada más pulsar JUGAR.",
     "HD texture packs load without hitches and apply as soon as you press PLAY."},
    {nullptr, nullptr, "El pack HD de SSGPrinceVegeta se instala con un clic en Nova's Mods.",
     "SSGPrinceVegeta's HD pack installs in one click from Nova's Mods."},
    {"Launcher", "Launcher",
     "Nueva pestaña Juego: Tag Team, Online y los demás modos en un solo sitio, con arranque rápido.",
     "New Game tab: Tag Team, Online and the other modes in one place, plus a quick start option."},
    {nullptr, nullptr, "Textos en español con sus tildes y eñes. Pulsa un mod para ver su ficha.",
     "Spanish texts with proper accents. Click a mod to see its info card."},
    {"Kit de modding", "Modding kit",
     "Ports oficiales con un clic: Goku GT, Vegeta GT, Pan, Baby, Janemba, Trunks espada y Gohan del Futuro.",
     "Official ports in one click: Goku GT, Vegeta GT, Pan, Baby, Janemba, sword Trunks and Future Gohan."},
};

// Del mas nuevo al mas antiguo.
inline constexpr NewsBlock kNewsBlocks[] = {
    {"1.5.5.5", kNews1555, int(sizeof(kNews1555) / sizeof(kNews1555[0]))},
    {"1.5.5.4", kNews1554, int(sizeof(kNews1554) / sizeof(kNews1554[0]))},
    {"1.5.5.3", kNews1553, int(sizeof(kNews1553) / sizeof(kNews1553[0]))},
    {"1.5.5.2", kNews1552, int(sizeof(kNews1552) / sizeof(kNews1552[0]))},
    {"1.5.5.1", kNews1551, int(sizeof(kNews1551) / sizeof(kNews1551[0]))},
    {"1.5.5", kNews155, int(sizeof(kNews155) / sizeof(kNews155[0]))},
    {"1.5.1.4", kNews1514, int(sizeof(kNews1514) / sizeof(kNews1514[0]))},
    {"1.5.1.3", kNews1513, int(sizeof(kNews1513) / sizeof(kNews1513[0]))},
    {"1.5.1.2", kNews1512, int(sizeof(kNews1512) / sizeof(kNews1512[0]))},
    {"1.5.1.1", kNews1511, int(sizeof(kNews1511) / sizeof(kNews1511[0]))},
    {"1.5.1", kNews151, int(sizeof(kNews151) / sizeof(kNews151[0]))},
    {"1.5.0", kNews150, int(sizeof(kNews150) / sizeof(kNews150[0]))},
    {"1.4.5", kNews145, int(sizeof(kNews145) / sizeof(kNews145[0]))},
};

}  // namespace dbz3::launcher
