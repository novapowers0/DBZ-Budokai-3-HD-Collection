// dbz3 - Launcher UI localization (ES/IT/DE/FR; everything else -> EN).
// GENERATED from src/launcher/launcher_state.cpp i18n::T() call sites.
// Spanish keys are matched by EXACT runtime string (adjacent literal
// concatenation). Add a new language by extending kTable.

#include "i18n.h"

#include <cstring>

namespace dbz3::i18n {

namespace {
// Xbox XGetLanguage id: 1=EN, 3=DE, 4=FR, 5=ES, 6=IT; anything else -> EN.
int g_ui_lang = 1;

struct Entry {
  const char* es;  // exact Spanish runtime string (the lookup key)
  const char* it;  // Italian (language id 6)
  const char* de;  // German (language id 3)
  const char* fr;  // French (language id 4)
};

static const Entry kTable[] = {
    {"[OK] Datos del juego en: %s", "[OK] Dati del gioco in: %s", "[OK] Spieldaten in: %s", "[OK] Données du jeu dans : %s"},
    {"region", "regione", "Region", "région"},
    {"Inicio: %s - %s - %dx - %s - %s", "Avvio: %s - %s - %dx - %s - %s", "Start: %s - %s - %dx - %s - %s", "Lancement : %s - %s - %dx - %s - %s"},
    {"Traduce el launcher y el texto del juego a este idioma. Se aplica en el proximo arranque.", "Traduce il launcher e il testo del gioco in questa lingua. Si applica al prossimo avvio.", "Übersetzt den Launcher und den Spieltext in diese Sprache. Gilt beim nächsten Start.", "Traduit le launcher et le texte du jeu dans cette langue. S'applique au prochain lancement."},
    {"No se encontraron los datos del juego (default.xex / us / eu).", "Dati del gioco non trovati (default.xex / us / eu).", "Spieldaten nicht gefunden (default.xex / us / eu).", "Données du jeu introuvables (default.xex / us / eu)."},
    {"Nota: default.xex no es el ejecutable US/NA estandar (version modificada o de otra region). Si el juego se cierra al inicio, sustituyelo por el default.xex de tu copia US/NA (yae3_xenon.xex).", "Nota: default.xex non è l'eseguibile US/NA standard (versione modificata o di un'altra regione). Se il gioco si chiude all'avvio, sostituiscilo con il default.xex della tua copia US/NA (yae3_xenon.xex).", "Hinweis: default.xex ist nicht die Standard-US/NA-Ausführungsdatei (modifiziert oder andere Region). Wenn das Spiel beim Start schließt, ersetzen Sie es durch das default.xex Ihrer US/NA-Kopie (yae3_xenon.xex).", "Remarque : default.xex n'est pas l'exécutable US/NA standard (version modifiée ou autre région). Si le jeu se ferme au démarrage, remplacez-le par le default.xex de votre copie US/NA (yae3_xenon.xex)."},
    {"default.xex es el ejecutable EU/PAL. Este nucleo esta recompilado SOLO desde el ejecutable US/NA (yae3_xenon.xex): el EU no puede arrancar aqui (el juego se cierra al inicio). Sustituye default.xex por el US/NA, o usa el launcher principal (que elige el nucleo EU/PAL por si solo). La region EU/PAL y el idioma se eligen aqui.", "default.xex è l'eseguibile EU/PAL. Questo nucleo è ricompilato SOLO dall'eseguibile US/NA (yae3_xenon.xex): l'EU non può avviarsi qui (il gioco si chiude all'avvio). Sostituisci default.xex con quello US/NA, oppure usa il launcher principale (che sceglie automaticamente il nucleo EU/PAL). La regione EU/PAL e la lingua si scelgono qui.", "default.xex ist die EU/PAL-Ausführungsdatei. Dieser Kern ist NUR aus der US/NA-Ausführungsdatei (yae3_xenon.xex) neu kompiliert: die EU-Version kann hier nicht starten (das Spiel schließt beim Start). Ersetzen Sie default.xex durch die US/NA-Datei oder verwenden Sie den Haupt-Launcher (der den EU/PAL-Kern automatisch wählt). Die EU/PAL-Region und die Sprache werden hier gewählt.", "default.xex est l'exécutable EU/PAL. Ce cœur est recompilé UNIQUEMENT à partir de l'exécutable US/NA (yae3_xenon.xex) : l'EU ne peut pas démarrer ici (le jeu se ferme au lancement). Remplacez default.xex par celui US/NA, ou utilisez le lanceur principal (qui choisit automatiquement le cœur EU/PAL). La région EU/PAL et la langue se choisissent ici."},
    {"Este es el nucleo EU/PAL y default.xex es el ejecutable US/NA. Cada nucleo es una recompilacion de UN ejecutable: este solo arranca el EU/PAL (yae3_xenon_eu.xex). Sustituye default.xex por el EU/PAL, o usa el launcher principal (que elige el nucleo correcto por si solo).", "Questo è il nucleo EU/PAL e default.xex è l'eseguibile US/NA. Ogni nucleo è una ricompilazione di UN eseguibile: questo avvia solo l'EU/PAL (yae3_xenon_eu.xex). Sostituisci default.xex con quello EU/PAL, oppure usa il launcher principale (che sceglie automaticamente il nucleo corretto).", "Dies ist der EU/PAL-Kern und default.xex ist die US/NA-Ausführungsdatei. Jeder Kern ist eine Neu-Kompilierung EINER Ausführungsdatei: dieser startet nur die EU/PAL-Datei (yae3_xenon_eu.xex). Ersetzen Sie default.xex durch die EU/PAL-Datei oder verwenden Sie den Haupt-Launcher (der den richtigen Kern automatisch wählt).", "Ceci est le cœur EU/PAL et default.xex est l'exécutable US/NA. Chaque cœur est une recompilation d'UN exécutable : celui-ci ne démarre que l'EU/PAL (yae3_xenon_eu.xex). Remplacez default.xex par celui EU/PAL, ou utilisez le lanceur principal (qui choisit automatiquement le bon cœur)."},
    {"La carpeta de datos no se localizo automaticamente.", "La cartella dei dati non è stata individuata automaticamente.", "Der Datenordner wurde nicht automatisch gefunden.", "Le dossier de données n'a pas été localisé automatiquement."},
    {"Falta default.xex. ", "Manca default.xex. ", "default.xex fehlt. ", "default.xex manquant. "},
    {"Faltan las carpetas us/ o eu/.", "Mancano le cartelle us/ o eu/.", "Die Ordner us/ oder eu/ fehlen.", "Dossiers us/ ou eu/ manquants."},
    {"Seleccionar carpeta de datos...", "Seleziona cartella dati...", "Datenordner auswählen...", "Sélectionner le dossier de données..."},
    {"No se pudo montar la carpeta elegida.", "Impossibile montare la cartella scelta.", "Der gewählte Ordner konnte nicht gemountet werden.", "Impossible de monter le dossier choisi."},
    {"La carpeta elegida no contiene us/ o eu/ (ni default.xex). Reintenta.", "La cartella scelta non contiene us/ o eu/ (né default.xex). Riprova.", "Der gewählte Ordner enthält kein us/ oder eu/ (und kein default.xex). Versuchen Sie es erneut.", "Le dossier choisi ne contient pas us/ ou eu/ (ni default.xex). Réessayez."},
    {"Elige la carpeta que contiene las carpetas us/ y eu/ (o assets/).", "Scegli la cartella che contiene le cartelle us/ ed eu/ (o assets/).", "Wählen Sie den Ordner, der die Ordner us/ und eu/ (oder assets/) enthält.", "Choisissez le dossier contenant les dossiers us/ et eu/ (ou assets/)."},
    {"Video", "Video", "Video", "Vidéo"},
    {"Escalado", "Upscaling", "Skalierung", "Mise à l'échelle"},
    {"Audio", "Audio", "Audio", "Audio"},
    {"Controles", "Controlli", "Steuerung", "Contrôles"},
    {"Mods", "Mods", "Mods", "Mods"},
    {"Cambio de modelo", "Cambio modello", "Modelltausch", "Échange de modèle"},
    {"Texturas", "Texture", "Texturen", "Textures"},
    {"Desarrollo", "Sviluppo", "Entwicklung", "Développement"},
    {"Restablecer valores", "Ripristina valori", "Standard wiederherstellen", "Réinitialiser"},
    {"Guardar ajustes", "Salva impostazioni", "Einstellungen speichern", "Enregistrer les réglages"},
    {"Calidad de imagen", "Qualità immagine", "Bildqualität", "Qualité d'image"},
    {"GPU: %s", "GPU: %s", "GPU: %s", "GPU : %s"},
    {"nivel detectado", "livello rilevato", "erkannte Stufe", "niveau détecté"},
    {"GPU: no detectado", "GPU: non rilevata", "GPU: nicht erkannt", "GPU : non détectée"},
    {"Auto (recomendado)", "Auto (consigliato)", "Auto (empfohlen)", "Auto (recommandé)"},
    {"Baja", "Bassa", "Niedrig", "Basse"},
    {"Media", "Media", "Mittel", "Moyenne"},
    {"Alta", "Alta", "Hoch", "Haute"},
    {"Ultra", "Ultra", "Ultra", "Ultra"},
    {"Manual", "Manuale", "Manuell", "Manuel"},
    {"Perfil de calidad", "Profilo di qualità", "Qualitätsprofil", "Profil de qualité"},
    {"Activo: %s -> %dx, MSAA %s, aniso %d, %s", "Attivo: %s -> %dx, MSAA %s, aniso %d, %s", "Aktiv: %s -> %dx, MSAA %s, aniso %d, %s", "Actif : %s -> %dx, MSAA %s, aniso %d, %s"},
    {"Perfil que ajusta escala interna, MSAA, filtrado anisotropico y upscaler. Auto detecta la GPU en cada arranque; elige un perfil fijo para bloquear los valores. Afecta a la proxima partida.", "Profilo che regola scala interna, MSAA, filtro anisotropico e upscaler. Auto rileva la GPU a ogni avvio; scegli un profilo fisso per bloccare i valori. Vale per la prossima partita.", "Profil, das interne Skalierung, MSAA, anisotropes Filtern und Upscaler einstellt. Auto erkennt die GPU bei jedem Start; ein festes Profil sperrt die Werte. Gilt für die nächste Sitzung.", "Profil qui règle l'échelle interne, la MSAA, le filtrage anisotrope et l'upscaler. Auto détecte la GPU à chaque lancement ; choisissez un profil fixe pour verrouiller les valeurs. S'applique à la prochaine session."},
    {"1x (nativa 720p)", "1x (720p nativa)", "1x (nativ 720p)", "1x (720p natif)"},
    {"2x (interna 1440p)", "2x (1440p interno)", "2x (1440p intern)", "2x (1440p interne)"},
    {"3x (interna 2160p)", "3x (2160p interno)", "3x (2160p intern)", "3x (2160p interne)"},
    {"4x (interna 2880p)", "4x (2880p interno)", "4x (2880p intern)", "4x (2880p interne)"},
    {"Escala de render interna", "Scala di rendering interna", "Interne Render-Skalierung", "Échelle de rendu interne"},
    {"Supersampling del framebuffer de 720p. Reduce el aliasing. Requiere reinicio.", "Supersampling del framebuffer 720p. Riduce l'aliasing. Richiede riavvio.", "Supersampling des 720p-Framebuffers. Reduziert Kantenflimmern. Neustart erforderlich.", "Suréchantillonnage du framebuffer 720p. Réduit l'aliasing. Redémarrage requis."},
    {"MSAA 2x nativo", "MSAA 2x nativa", "Nativer 2x-MSAA", "MSAA 2x native"},
    {"MSAA 2x del host para superficies MSAA 2x del guest.", "MSAA 2x host per superfici MSAA 2x guest.", "Host-2x-MSAA für Guest-2x-MSAA-Flächen.", "MSAA 2x hôte pour les surfaces MSAA 2x guest."},
    {"Filtrado anisotropico", "Filtro anisotropico", "Anisotrope Filterung", "Filtrage anisotrope"},
    {"Idioma", "Lingua", "Sprache", "Langue"},
    {"Idioma del launcher y del juego", "Lingua del launcher e del gioco", "Sprache von Launcher und Spiel", "Langue du launcher et du jeu"},
    {"Cambia el idioma de todo el launcher y del texto del juego. Requiere reinicio.", "Cambia la lingua di tutto il launcher e del testo del gioco. Richiede riavvio.", "Wechselt die Sprache des gesamten Launchers und des Spieltexts. Neustart erforderlich.", "Change la langue de tout le launcher et du texte du jeu. Redémarrage requis."},
    {"Pantalla", "Schermo", "Anzeige", "Affichage"},
    {"Ventana", "Finestra", "Fenster", "Fenêtré"},
    {"Sin bordes", "Senza bordi", "Randlos", "Sans bordures"},
    {"Pantalla completa exclusiva", "Schermo intero esclusivo", "Exklusiver Vollbildmodus", "Plein écran exclusif"},
    {"Modo de pantalla", "Modalità schermo", "Bildschirmmodus", "Mode d'affichage"},
    {"Velocidad del juego: fija a 60 FPS (sincronizada)", "Velocità del gioco: fissa a 60 FPS (sincronizzata)", "Spielgeschwindigkeit: fest 60 FPS (synchronisiert)", "Vitesse du jeu : fixée à 60 FPS (synchronisée)"},
    {"El ritmo del juego se sincroniza a 60 Hz (vblank del guest). No es configurable: sin esta sincronizacion el juego corre acelerado.", "Il ritmo del gioco è sincronizzato a 60 Hz (vblank del guest). Non configurabile: senza questa sincronizzazione il gioco corre troppo veloce.", "Der Spielrhythmus wird mit 60 Hz (Guest-Vblank) synchronisiert. Nicht konfigurierbar: ohne diese Synchronisation läuft das Spiel zu schnell.", "Le rythme du jeu est synchronisé à 60 Hz (vblank guest). Non configurable : sans cette synchronisation, le jeu tourne trop vite."},
    {"Limite de fotogramas (FPS)", "Limite fotogrammi (FPS)", "Bildratenlimit (FPS)", "Limite de FPS"},
    {"Sin limite", "Senza limite", "Unbegrenzt", "Sans limite"},
    {"Limita la velocidad de presentacion en pantalla, NO la velocidad del juego (esa es siempre 60). 60 = fluido; 30 = menos carga en GPUs integradas; 0 = sin limite.", "Limita la velocità di presentazione a schermo, NON la velocità del gioco (sempre 60). 60 = fluido; 30 = meno carico su GPU integrate; 0 = senza limite.", "Begrenzt die Bildwiederholrate auf dem Bildschirm, NICHT die Spielgeschwindigkeit (immer 60). 60 = flüssig; 30 = weniger Last auf integrierten GPUs; 0 = unbegrenzt.", "Limite la fréquence d'affichage, PAS la vitesse du jeu (toujours 60). 60 = fluide ; 30 = moins de charge sur GPU intégrées ; 0 = sans limite."},
    {"Frecuencia variable (G-Sync/FreeSync)", "Frequenza variabile (G-Sync/FreeSync)", "Variable Bildwiederholrate (G-Sync/FreeSync)", "Fréquence variable (G-Sync/FreeSync)"},
    {"Sincroniza el monitor con cada fotograma para evitar saltos en pantallas de alta frecuencia. Si esta desactivado, el juego ajusta el ritmo solo para que se vea fluido a 60 Hz.", "Sincronizza il monitor a ogni fotogramma per evitare strappi sui pannelli ad alta frequenza. Se disattivata, il gioco regola da solo il ritmo per apparire fluido a 60 Hz.", "Synchronisiert den Monitor mit jedem Frame für einen gleichmäßigen Ablauf auf Hochfrequenz-Panels. Ist sie deaktiviert, passt das Spiel das Tempo selbst an, um bei 60 Hz flüssig auszusehen.", "Synchronise le moniteur à chaque image pour un rendu régulier sur les écrans haute fréquence. Si désactivé, le jeu ajuste son rythme pour rester fluide à 60 Hz."},
    {"Monitor: %d Hz", "Monitor: %d Hz", "Monitor: %d Hz", "Moniteur : %d Hz"},
    {"El launcher no depende del refresco del monitor. El juego se ve fluido a cualquier Hz.", "Il launcher non dipende dalla frequenza del monitor. Il gioco è fluido a qualsiasi Hz.", "Der Launcher hängt nicht von der Monitorfrequenz ab. Das Spiel läuft bei jeder Hz flüssig.", "Le launcher ne dépend pas de la fréquence du moniteur. Le jeu reste fluide à toute fréquence."},
    {"Monitor: no detectado (se asume 60 Hz).", "Monitor: non rilevato (presunti 60 Hz).", "Monitor: nicht erkannt (60 Hz angenommen).", "Moniteur : non détecté (60 Hz supposés)."},
    {"Motor grafico", "Motore grafico", "Grafik-API", "Moteur graphique"},
    {"Vulkan es experimental: el combate 3D corre notablemente mas lento que D3D12 en hardware NVIDIA. Usa D3D12 salvo que necesites Vulkan por compatibilidad.", "Vulkan è sperimentale: il combattimento 3D è notevolmente più lento di D3D12 su hardware NVIDIA. Usa D3D12 a meno che ti serva Vulkan per compatibilità.", "Vulkan ist experimentell: 3D-Kämpfe laufen auf NVIDIA-Hardware deutlich langsamer als D3D12. Verwenden Sie D3D12, außer Sie brauchen Vulkan aus Kompatibilitätsgründen.", "Vulkan est expérimental : les combats 3D sont nettement plus lents que D3D12 sur le matériel NVIDIA. Utilisez D3D12 sauf si vous avez besoin de Vulkan pour la compatibilité."},
    {"API de render del host. Requiere reinicio.", "API di rendering host. Richiede riavvio.", "Host-Render-API. Neustart erforderlich.", "API de rendu hôte. Redémarrage requis."},
    {"CAS (nitidez)", "CAS (nitidezza)", "CAS (Schärfe)", "CAS (netteté)"},
    {"Efecto", "Effetto", "Effekt", "Effet"},
    {"FSR escala el render interno al tamano de la pantalla.\nIdeal con una escala interna baja (1x) en pantallas 1080p o superiores.", "FSR scala il render interno alla dimensione dello schermo.\nIdeale con una scala interna bassa (1x) su schermi 1080p o superiori.", "FSR skaliert das interne Rendering auf die Bildschirmgröße.\nAm besten mit niedriger interner Skalierung (1x) auf 1080p- oder größeren Displays.", "FSR met à l'échelle le rendu interne à la taille de l'écran.\nIdéal avec une échelle interne faible (1x) sur écrans 1080p ou plus."},
    {"CAS aplica nitidez adaptativa al contraste despues del escalado.\nMuy bueno con una escala interna alta (2x-3x).", "CAS applica nitidezza adattiva al contrasto dopo lo scaling.\nOttimo con una scala interna alta (2x-3x).", "CAS wendet nach der Skalierung kontrastadaptive Schärfe an.\nSehr gut mit hoher interner Skalierung (2x-3x).", "CAS applique une netteté adaptative au contraste après la mise à l'échelle.\nExcellent avec une échelle interne élevée (2x-3x)."},
    {"Escalado bilineal, el camino mas simple. Sin nitidez.", "Scaling bilineare, il percorso più semplice. Nessuna nitidezza.", "Bilineare Skalierung, der einfachste Weg. Keine Schärfung.", "Mise à l'échelle bilinéaire, le plus simple. Sans netteté."},
    {"El efecto elegido se aplica en el proximo arranque (requiere reinicio).", "L'effetto scelto si applica al prossimo avvio (richiede riavvio).", "Der gewählte Effekt wird beim nächsten Start angewendet (Neustart erforderlich).", "L'effet choisi s'applique au prochain lancement (redémarrage requis)."},
    {"Volumen", "Volume", "Lautstärke", "Volume"},
    {"Volumen general", "Volume generale", "Gesamtlautstärke", "Volume général"},
    {"Musica", "Musica", "Musik", "Musique"},
    {"Efectos (SFX)", "Effetti (SFX)", "Effekte (SFX)", "Effets (SFX)"},
    {"Voces", "Voci", "Stimmen", "Voix"},
    {"Las pistas de idioma/voz se eligen dentro del juego (japones/ingles).", "Le tracce lingua/voci si scelgono nel gioco (giapponese/inglese).", "Sprach-/Stimmspuren werden im Spiel gewählt (Japanisch/Englisch).", "Les pistes langue/voix se choisissent dans le jeu (japonais/anglais)."},
    {"Mando", "Controller", "Controller", "Manette"},
    {"XInput (nativo)", "XInput (nativo)", "XInput (nativ)", "XInput (natif)"},
    {"SDL (mandos genericos)", "SDL (controller generici)", "SDL (generische Pads)", "SDL (manettes génériques)"},
    {"Backend del mando", "Backend controller", "Controller-Backend", "Backend de manette"},
    {"XInput = mandos de PC estandar, sin init extra. SDL = mandos genericos, pero puede colgar con RTSS/OBS. Requiere reinicio.", "XInput = controller PC standard, senza init extra. SDL = controller generici, ma può bloccarsi con RTSS/OBS. Richiede riavvio.", "XInput = Standard-PC-Pads, keine zusätzliche Init. SDL = generische Pads, kann aber mit RTSS/OBS hängen. Neustart erforderlich.", "XInput = manettes PC standard, sans init supplémentaire. SDL = manettes génériques, mais peut se bloquer avec RTSS/OBS. Redémarrage requis."},
    {"Zona muerta de los sticks", "Zona morta degli stick", "Stick-Totzone", "Zone morte des sticks"},
    {"Activar vibracion", "Attiva vibrazione", "Vibration aktivieren", "Activer la vibration"},
    {"Teclado / Raton", "Tastiera / Mouse", "Tastatur / Maus", "Clavier / Souris"},
    {"Emular mando con teclado/raton", "Emula il controller con tastiera/mouse", "Controller mit Tastatur/Maus emulieren", "Émuler la manette avec clavier/souris"},
    {"Emula un mando con el teclado. Necesario para jugar sin pad. Usa las teclas de abajo.", "Emula un controller con la tastiera. Necessario per giocare senza pad. Usa i tasti qui sotto.", "Emuliert einen Controller mit der Tastatur. Nötig zum Spielen ohne Pad. Verwenden Sie die Tasten unten.", "Émule une manette avec le clavier. Nécessaire pour jouer sans pad. Utilisez les touches ci-dessous."},
    {"Usar el raton para el stick derecho", "Usa il mouse per lo stick destro", "Maus für den rechten Stick verwenden", "Utiliser la souris pour le stick droit"},
    {"Mueve el stick derecho con el raton (ademas de las teclas rstick_*).", "Muove lo stick destro con il mouse (oltre ai tasti rstick_*).", "Bewegt den rechten Stick mit der Maus (zusätzlich zu den rstick_*-Tasten).", "Déplace le stick droit avec la souris (en plus des touches rstick_*)."},
    {"Mapeo de teclas (MnK)", "Mappatura tasti (MnK)", "Tastenbelegung (MnK)", "Mappage des touches (MnK)"},
    {"Los nombres de tecla siguen VirtualKey (p.ej. Space, W, Up, LMB, RMB, MMB). Coma = alternativas, Shift+/Ctrl+/Alt+ = modificadores. Vacio = sin asignar.", "I nomi dei tasti seguono VirtualKey (es. Space, W, Up, LMB, RMB, MMB). Virgola = alternative, Shift+/Ctrl+/Alt+ = modificatori. Vuoto = non assegnato.", "Tastennamen folgen VirtualKey (z.B. Space, W, Up, LMB, RMB, MMB). Komma = Alternativen, Shift+/Ctrl+/Alt+ = Modifikatoren. Leer = nicht belegt.", "Les noms de touches suivent VirtualKey (ex. Space, W, Up, LMB, RMB, MMB). Virgule = alternatives, Shift+/Ctrl+/Alt+ = modificateurs. Vide = non assigné."},
    {"El remapeo completo de botones tambien esta disponible en el menu de ajustes en juego (F4).", "La rimappatura completa dei pulsanti è disponibile anche nel menu impostazioni di gioco (F4).", "Die vollständige Tastenbelegung ist auch im Spielmenü Einstellungen (F4) verfügbar.", "Le remappage complet des boutons est aussi disponible dans le menu des réglages en jeu (F4)."},
    {"Los mods sobrescriben entradas dentro de los contenedores .afs del juego (modelos, movesets, texturas) sin reempaquetar. Un mod es una carpeta aqui:", "I mod sovrascrivono voci nei contenitori .afs del gioco (modelli, moveset, texture) senza reimpacchettare. Un mod è una cartella qui:", "Mods überschreiben Einträge in den .afs-Containern des Spiels (Modelle, Movesets, Texturen), ohne neu zu packen. Ein Mod ist ein Ordner hier:", "Les mods remplacent des entrées dans les conteneurs .afs du jeu (modèles, movesets, textures) sans reconditionner. Un mod est un dossier ici :"},
    {"Region de assets", "Regione asset", "Asset-Region", "Région des assets"},
    {"USA (NTSC)", "USA (NTSC)", "USA (NTSC)", "USA (NTSC)"},
    {"Europa (PAL)", "Europa (PAL)", "Europa (PAL)", "Europe (PAL)"},
    {"Paquete de texto/audio/video. Requiere reinicio.", "Pacchetto testo/audio/video. Richiede riavvio.", "Text-/Audio-/Videopaket. Neustart erforderlich.", "Pack texte/audio/vidéo. Redémarrage requis."},
    {"No hay mods instalados. Los mods se colocan en la carpeta 'mods' junto al ejecutable, cada uno en su propia subcarpeta con un manifest.txt.", "Nessun mod installato. I mod vanno nella cartella 'mods' accanto all'eseguibile, ognuno nella propria sottocartella con un manifest.txt.", "Keine Mods installiert. Mods gehören in den Ordner 'mods' neben der ausführbaren Datei, jeder in einem eigenen Unterordner mit manifest.txt.", "Aucun mod installé. Les mods se placent dans le dossier 'mods' à côté de l'exécutable, chacun dans son sous-dossier avec un manifest.txt."},
    {"Abrir carpeta de mods", "Apri cartella mod", "Mods-Ordner öffnen", "Ouvrir le dossier des mods"},
    {"Crea 'mods/' si no existe y la abre en el Explorador. Copia aqui tu mod descargado y se listara y activara automaticamente.", "Crea 'mods/' se non esiste e lo apre in Esplora risorse. Metti qui il tuo mod scaricato e verrà elencato e attivato automaticamente.", "Erstellt 'mods/', falls nicht vorhanden, und öffnet es im Explorer. Legen Sie Ihren heruntergeladenen Mod hier ab; er wird automatisch gelistet und aktiviert.", "Crée 'mods/' s'il n'existe pas et l'ouvre dans l'Explorateur. Déposez votre mod téléchargé ici ; il sera listé et activé automatiquement."},
    {"%d mods (%d activados)", "%d mod (%d attivi)", "%d Mods (%d aktiviert)", "%d mods (%d activés)"},
    {"Mod", "Mod", "Mod", "Mod"},
    {"Tipo", "Tipo", "Typ", "Type"},
    {"%d archivo%s", "%d file%s", "%d Datei%s", "%d fichier%s"},
    {"s", "s", "e", "s"},
    {"%s\n%s\nAutor: %s\nVersion: %s\nTipo: %s\nOrigen: %s\nDestino: %s", "%s\n%s\nAutore: %s\nVersione: %s\nTipo: %s\nOrigine: %s\nDestinazione: %s", "%s\n%s\nAutor: %s\nVersion: %s\nTyp: %s\nQuelle: %s\nZiel: %s", "%s\n%s\nAuteur : %s\nVersion : %s\nType : %s\nSource : %s\nCible : %s"},
    {"(sin descripcion)", "(senza descrizione)", "(keine Beschreibung)", "(sans description)"},
    {"Editar descripcion / autor / version (manifest.txt)", "Modifica descrizione / autore / versione (manifest.txt)", "Beschreibung / Autor / Version bearbeiten (manifest.txt)", "Modifier description / auteur / version (manifest.txt)"},
    {"Editar mod: %s", "Modifica mod: %s", "Mod bearbeiten: %s", "Modifier le mod : %s"},
    {"Titulo", "Titolo", "Titel", "Titre"},
    {"Descripcion", "Descrizione", "Beschreibung", "Description"},
    {"Autor", "Autore", "Autor", "Auteur"},
    {"Version", "Versione", "Version", "Version"},
    {"Guardar", "Salva", "Speichern", "Enregistrer"},
    {"Cancelar", "Annulla", "Abbrechen", "Annuler"},
    {"El texto se guarda en %s/manifest.txt", "Il testo viene salvato in %s/manifest.txt", "Der Text wird in %s/manifest.txt gespeichert", "Le texte est enregistré dans %s/manifest.txt"},
    {"Cambio de modelo B3 HD -> B3 HD", "Cambio modello B3 HD -> B3 HD", "Modelltausch B3 HD -> B3 HD", "Échange de modèle B3 HD -> B3 HD"},
    {"Intercambia el bin #AMB completo de un personaje HD del B3 por el de otro (swap nativo). Genera el mod y lo activa.", "Scambia il bin #AMB completo di un personaggio HD del B3 con quello di un altro (swap nativo). Genera il mod e lo attiva.", "Tauscht das komplette #AMB-Bin eines HD-Charakters von B3 gegen das eines anderen (nativer Swap). Erstellt den Mod und aktiviert ihn.", "Échange le bin #AMB complet d'un personnage HD de B3 contre celui d'un autre (swap natif). Génère le mod et l'active."},
    {"El catalogo de personajes (catalog_b3.cat) no se encontro o esta vacio.", "Il catalogo dei personaggi (catalog_b3.cat) non è stato trovato o è vuoto.", "Der Charakterkatalog (catalog_b3.cat) wurde nicht gefunden oder ist leer.", "Le catalogue des personnages (catalog_b3.cat) est introuvable ou vide."},
    {"El cambio de modelo necesita la carpeta 'mod center hd' junto al ejecutable, con catalog_b3.cat y swap_b3.py. No viene incluida en el ZIP de release: descargala del repositorio (carpeta 'mod center hd') o desde un release completo, y colocala al lado de dbz3.exe.", "Il cambio modello richiede la cartella 'mod center hd' accanto all'eseguibile, con catalog_b3.cat e swap_b3.py. Non è inclusa nello ZIP di release: scaricala dal repository (cartella 'mod center hd') o da una release completa, e mettila accanto a dbz3.exe.", "Der Modelltausch benötigt den Ordner 'mod center hd' neben der ausführbaren Datei, mit catalog_b3.cat und swap_b3.py. Er ist nicht im Release-ZIP enthalten: laden Sie ihn aus dem Repository (Ordner 'mod center hd') oder aus einer vollständigen Version herunter und legen Sie ihn neben dbz3.exe.", "L'échange de modèle nécessite le dossier 'mod center hd' à côté de l'exécutable, avec catalog_b3.cat et swap_b3.py. Il n'est pas inclus dans le ZIP de release : téléchargez-le depuis le dépôt (dossier 'mod center hd') ou depuis une release complète, et placez-le à côté de dbz3.exe."},
    {"Esperado en: %s", "Previsto in: %s", "Erwartet in: %s", "Attendu à : %s"},
    {"Archivo de modelos (data_cmn.afs)", "File modelli (data_cmn.afs)", "Modelldatei (data_cmn.afs)", "Fichier de modèles (data_cmn.afs)"},
    {"Usar la ruta automatica (us/data_cmn.afs)", "Usa il percorso automatico (us/data_cmn.afs)", "Automatischen Pfad verwenden (us/data_cmn.afs)", "Utiliser le chemin automatique (us/data_cmn.afs)"},
    {"Buscar...", "Sfoglia...", "Durchsuchen...", "Parcourir..."},
    {"Ruta completa al data_cmn.afs del juego", "Percorso completo al data_cmn.afs del gioco", "Vollständiger Pfad zum data_cmn.afs des Spiels", "Chemin complet vers le data_cmn.afs du jeu"},
    {"Pega la ruta completa, por ejemplo:\nC:\\...\\us\\data_cmn.afs\no la que corresponda a tu instalacion.", "Incolla il percorso completo, ad esempio:\nC:\\...\\us\\data_cmn.afs\no quello corrispondente alla tua installazione.", "Fügen Sie den vollständigen Pfad ein, zum Beispiel:\nC:\\...\\us\\data_cmn.afs\noder den Pfad, der zu Ihrer Installation passt.", "Collez le chemin complet, par exemple :\nC:\\...\\us\\data_cmn.afs\nou celui qui correspond à votre installation."},
    {"Personaje HD (origen)", "Personaggio HD (origine)", "HD-Charakter (Quelle)", "Personnage HD (source)"},
    {"Selecciona...", "Seleziona...", "Auswählen...", "Sélectionner..."},
    {"personajes", "personaggi", "Charaktere", "personnages"},
    {"Slot destino", "Slot di destinazione", "Ziel-Slot", "Slot de destination"},
    {"Cambiar B3 -> B3", "Cambia B3 -> B3", "Tausche B3 -> B3", "Échanger B3 -> B3"},
    {"  [NO JUGABLE]", "  [NON GIOCABILE]", "  [NICHT SPIELBAR]", "  [NON JOUABLE]"},
    {"Trabajando...", "Lavoro in corso...", "Arbeitet...", "Travail en cours..."},
    {"Hecho.", "Fatto.", "Fertig.", "Terminé."},
    {"El mod generado se activa solo y se lista en la pestana Mods.", "Il mod generato si attiva da solo e compare nella scheda Mod.", "Der erzeugte Mod aktiviert sich selbst und erscheint im Reiter Mods.", "Le mod généré s'active tout seul et apparaît dans l'onglet Mods."},
    {"Mod de texturas (B3 HD)", "Mod texture (B3 HD)", "Textur-Mod (B3 HD)", "Mod de textures (B3 HD)"},
    {"Extrae las texturas de un personaje como imagenes PNG editables, y al reconstruir reinserta tus ediciones.", "Estrae le texture di un personaggio come immagini PNG modificabili e, alla ricostruzione, reinserisce le tue modifiche.", "Extrahiert die Texturen eines Charakters als bearbeitbare PNG-Bilder und fügt beim Neuaufbau deine Änderungen wieder ein.", "Extrait les textures d'un personnage en images PNG modifiables, puis réinsère vos modifications lors de la reconstruction."},
    {"El mod de texturas necesita la carpeta 'mod center hd' junto al ejecutable, con catalog_b3.cat y texture_b3.py. No viene incluida en el ZIP de release: descargala del repositorio (carpeta 'mod center hd') o desde un release completo, y colocala al lado de dbz3.exe.", "Il mod texture richiede la cartella 'mod center hd' accanto all'eseguibile, con catalog_b3.cat e texture_b3.py. Non è inclusa nello ZIP di release: scaricala dal repository (cartella 'mod center hd') o da una release completa, e mettila accanto a dbz3.exe.", "Der Textur-Mod benötigt den Ordner 'mod center hd' neben der ausführbaren Datei, mit catalog_b3.cat und texture_b3.py. Er ist nicht im Release-ZIP enthalten: laden Sie ihn aus dem Repository (Ordner 'mod center hd') oder aus einer vollständigen Version herunter und legen Sie ihn neben dbz3.exe.", "Le mod de textures nécessite le dossier 'mod center hd' à côté de l'exécutable, avec catalog_b3.cat et texture_b3.py. Il n'est pas inclus dans le ZIP de release : téléchargez-le depuis le dépôt (dossier 'mod center hd') ou depuis une release complète, et placez-le à côté de dbz3.exe."},
    {"Personaje (origen de las texturas)", "Personaggio (origine delle texture)", "Charakter (Texturquelle)", "Personnage (source des textures)"},
    {"Slot destino (donde se aplican las texturas)", "Slot di destinazione (dove si applicano le texture)", "Ziel-Slot (wo die Texturen angewendet werden)", "Slot de destination (où les textures sont appliquées)"},
    {"El mismo personaje (sin swap)", "Lo stesso personaggio (senza swap)", "Derselbe Charakter (kein Swap)", "Le même personnage (sans swap)"},
    {"Si eliges otro personaje, el bin del origen con sus texturas editadas se coloca en el slot de ese personaje (para combinar con un swap de modelo).", "Se scegli un altro personaggio, il bin dell'origine con le sue texture modificate viene inserito nello slot di quel personaggio (per combinarlo con uno swap di modello).", "Wenn Sie einen anderen Charakter wählen, wird das Bin der Quelle mit seinen bearbeiteten Texturen in den Slot dieses Charakters gelegt (zum Kombinieren mit einem Modelltausch).", "Si vous choisissez un autre personnage, le bin de la source avec ses textures modifiées est placé dans le slot de ce personnage (pour le combiner avec un échange de modèle)."},
    {"Nombre del mod", "Nome del mod", "Mod-Name", "Nom du mod"},
    {"Carpeta de texturas (PNG)", "Cartella texture (PNG)", "Texturordner (PNG)", "Dossier de textures (PNG)"},
    {"Examinar...", "Sfoglia...", "Durchsuchen...", "Parcourir..."},
    {"por defecto", "predefinito", "Standard", "par défaut"},
    {"Extraer texturas a PNG", "Estrai texture in PNG", "Texturen als PNG extrahieren", "Extraire les textures en PNG"},
    {"Abrir carpeta de texturas", "Apri cartella texture", "Texturordner öffnen", "Ouvrir le dossier de textures"},
    {"Edita los PNG en: %s", "Modifica i PNG in: %s", "Bearbeiten Sie die PNGs in: %s", "Modifiez les PNG dans : %s"},
    {"Extrae primero las texturas para editar los PNG.", "Estrai prima le texture per modificare i PNG.", "Extrahieren Sie zuerst die Texturen, um die PNGs zu bearbeiten.", "Extrayez d'abord les textures pour modifier les PNG."},
    {"Reconstruir mod con texturas editadas", "Ricostruisci il mod con le texture modificate", "Mod mit bearbeiteten Texturen neu erstellen", "Reconstruire le mod avec les textures modifiées"},
    {"Reinsere los PNG editados de la carpeta, recompila el bin y genera el mod activo (combinable con un swap de modelo).", "Reinserisce i PNG modificati dalla cartella, ricompila il bin e genera il mod attivo (combinabile con uno swap di modello).", "Setzt die bearbeiteten PNGs aus dem Ordner wieder ein, kompiliert das Bin neu und erzeugt den aktiven Mod (kombinierbar mit einem Modelltausch).", "Réinsère les PNG modifiés depuis le dossier, recompile le bin et génère le mod actif (combinable avec un échange de modèle)."},
    {"%zu texturas (PNG):", "%zu texture (PNG):", "%zu Texturen (PNG):", "%zu textures (PNG) :"},
    {"haz clic en 'Abrir carpeta' para verlas", "fai clic su 'Apri cartella' per vederle", "klicken Sie auf 'Texturordner öffnen', um sie zu sehen", "cliquez sur 'Ouvrir le dossier' pour les voir"},
    {"Diagnostico", "Diagnostica", "Diagnose", "Diagnostic"},
    {"Activar modo Dev (overlay F10)", "Attiva modalità Dev (overlay F10)", "Dev-Modus aktivieren (F10-Overlay)", "Activer le mode Dev (overlay F10)"},
    {"Anade un overlay en juego (F10) con diagnostico y opciones de prueba.", "Aggiunge un overlay di gioco (F10) con diagnostica e opzioni di test.", "Fügt ein Spiel-Overlay (F10) mit Diagnose und Testoptionen hinzu.", "Ajoute un overlay en jeu (F10) avec diagnostics et options de test."},
    {"Mostrar contador de FPS en juego (debug 60fps)", "Mostra contatore FPS in gioco (debug 60fps)", "FPS-Zähler im Spiel anzeigen (60fps-Debug)", "Afficher le compteur de FPS en jeu (debug 60fps)"},
    {"Muestra una ventana pequeña con los FPS actuales mientras juegas. Util para verificar el limite de fotogramas / el modo 60fps.", "Mostra una piccola finestra con gli FPS attuali mentre giochi. Utile per verificare il limite di fotogrammi / la modalità 60fps.", "Zeigt während des Spiels ein kleines Fenster mit den aktuellen FPS. Nützlich, um das Bildratenlimit / den 60fps-Modus zu prüfen.", "Affiche une petite fenêtre avec les FPS actuels pendant le jeu. Utile pour vérifier la limite de FPS / le mode 60fps."},
    {"Registro de diagnostico GPU (logs + .bmp)", "Registrazione diagnostica GPU (log + .bmp)", "GPU-Diagnoseprotokoll (Logs + .bmp)", "Journal de diagnostic GPU (logs + .bmp)"},
    {"Escribe diagnostico GPU por fotograma (logs, readbacks y dumps .bmp). Genera archivos grandes. Mantener apagado normalmente.", "Scrive diagnostica GPU per fotogramma (log, readback e dump .bmp). Genera file grandi. Tenerlo spento normalmente.", "Schreibt pro Frame GPU-Diagnose (Logs, Readbacks und .bmp-Dumps). Erzeugt große Dateien. Normalerweise deaktiviert lassen.", "Écrit des diagnostics GPU par image (logs, readbacks et dumps .bmp). Génère de gros fichiers. À laisser désactivé normalement."},
    {"Guardar minidump de crash (crash_*.dmp)", "Salva minidump di crash (crash_*.dmp)", "Crash-Minidump speichern (crash_*.dmp)", "Enregistrer un minidump de crash (crash_*.dmp)"},
    {"Escribe un minidump cuando el juego falla. Mantener apagado normalmente.", "Scrive un minidump quando il gioco va in crash. Tenerlo spento normalmente.", "Schreibt einen Minidump, wenn das Spiel abstürzt. Normalerweise deaktiviert lassen.", "Écrit un minidump lorsque le jeu plante. À laisser désactivé normalement."},
};

const Entry* FindEntry(const char* es) {
  for (const Entry& e : kTable) {
    if (std::strcmp(e.es, es) == 0) return &e;
  }
  return nullptr;
}

}  // namespace

void SetLanguage(int32_t xbox_language_id) {
  g_ui_lang = xbox_language_id;
}

const char* T(const char* es, const char* en) {
  switch (g_ui_lang) {
    case 5:  // Spanish
      return es;
    case 6:  // Italian
      if (const Entry* e = FindEntry(es)) return e->it;
      break;
    case 3:  // German
      if (const Entry* e = FindEntry(es)) return e->de;
      break;
    case 4:  // French
      if (const Entry* e = FindEntry(es)) return e->fr;
      break;
    default:
      break;
  }
  return en;  // English (and unsupported languages such as Japanese)
}

}  // namespace dbz3::i18n
