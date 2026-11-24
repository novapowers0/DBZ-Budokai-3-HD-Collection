# v1.5.7 — notas en IT, DE, FR y PT

Origen: `studies 5/ENTREGAS/NOTAS_157.md:52-99` (sección «v1.5.7 — ES»). Traducción compacta y fiel de esa sección. Los identificadores técnicos (cvars, nombres de archivo, rutas y URL) quedan sin tocar. No se traducen las marcas de nota ni la sección interna de trazabilidad del original (`NOTAS_157.md:120-144`), porque no forman parte del anuncio. El PT sigue la convención pt-BR del proyecto (`res/ingame_ui/i18n_modos.toml`); el launcher no tiene columna PT.

---

# v1.5.7 — IT

Nuove modalità opzionali, novità della modalità Eroe e correzioni. Include anche le modifiche annotate come 1.5.6.1 e 1.5.6.2, che non hanno avuto una pubblicazione separata.

## Download

| File | A cosa serve |
|---|---|
| `DBZ-Budokai-3-HD-Collection-v1.5.7.zip` | Pacchetto del gioco per Windows. |
| `DBZ3HD-1.5.7-Kit-Modding.zip` | Kit di modding opzionale. |
| `DBZ-Budokai-3-HD-Collection-v1.5.7-linux-amd64.tar.gz` | Linux / Vulkan beta, quando è allegato; non testato su un vero computer Linux. |
| [Pacchetto personaggi — in sviluppo](https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing) | Download opzionale separato; il `DBZ3HD-1.4.3-Personajes.zip` esistente. |

Usa i pacchetti allegati, non gli archivi «Source code» di GitHub.

## Installazione

1. Estrai lo ZIP di Windows. Se aggiorni, salva prima una copia di impostazioni, salvataggi e mod; sostituisci insieme i file del programma, comprese le DLL.
2. Aggiungi la tua ISO di **Budokai 3 HD Collection (Xbox 360)** oppure `default.xex` + `us/` o `eu/` estratti. Scegli cartella/ISO in `dbz3.exe` e premi **GIOCA**.
3. Opzionale: estrai il Kit nella cartella del gioco. Le nuove modalità sono in **Dragon HUB**, oltre l'ultima riga del menu principale. Se non c'è un interruttore visibile, chiudi il gioco e cambia l'impostazione esistente in `dbz3_user.toml`, senza duplicarla.

## Novità

- **Modalità Eroe · beta:** schermate rinnovate (`dbz3_heroe_ui_nueva`), percorsi alternativi (`dbz3_heroe_rutas`) e tecniche v2 (`dbz3_heroe_tecnicas_v2`). **Ogni impostazione è disattivata per impostazione predefinita; mettila su `true` per provarla.** I percorsi sono stati testati fino all'Atto 2; equipaggiare e lanciare il Kamehameha in Allenamento ha superato il test.
- **Creatore XV2 · sperimentale:** sei varianti razziali, con trasformazioni Saiyan, Freezer dorato e Majin testate. Richiede il tuo Xenoverse 2 installato; attiva `dbz3_cac_xv2 = true` (**disattivato per impostazione predefinita**). `dbz3_xv2_dir` sceglie la cartella; vuoto consente il rilevamento automatico. Il Namecciano non ha trasformazione del modello.
- **Dragon Survivors · beta:** sopravvivenza con attacchi automatici e miglioramenti salendo di livello. Attiva `dbz3_survivors = true`; i contenuti/audio nuovi usano `dbz3_survivors_contenido_v2` e `dbz3_survivors_audio_v2`, anch'essi su `true`. **Tutti e tre disattivati per impostazione predefinita.** Una vittoria abbreviata, l'audio e il ritorno al HUB hanno superato il test; giudizio visivo: «accettabile come BETA».
- **Dragon Rogue · beta:** percorsi come Kumite II, carte, ripresa, evoluzione/fusione e miglioramenti di Kami. `dbz3_rogue = true`, **disattivato per impostazione predefinita**. Sono stati approvati percorsi mirati, senza certificare tutti i piani e tutte le difficoltà.
- **Modalità Competitiva · sperimentale, solo US:** Aura Burst rivisto, inseguimento L1 e finali senza Hyper. `dbz3_modo_competitivo = true`, **disattivato per impostazione predefinita**. Una coppia con le stesse regole in **Online · beta** ha superato il test senza differenze di verifica.
- **Sfide e Kit:** regole più chiare, rivincita, premi alla ripetizione e nomi delle mosse nel Laboratorio, **attivi per impostazione predefinita** (impostazioni di rifinitura `dbz3_modos_*`). Il pulsante del Kit nella scheda Personaggi è **attivo per impostazione predefinita** (`dbz3_launcher_kit`); non lo apre automaticamente. La durata delle fusioni offline usa `dbz3_fusion_duracion`, inizialmente `"original"`.
- **Classifica online · beta:** client opzionale di classifica di Survivors/diario di Rogue, `dbz3_ranking_online = true`, **disattivato per impostazione predefinita**. Testato con server simulato; il servizio pubblico completo resta non confermato.

## Correzioni

- Le sale competitive non rifiutano più la loro regola; Vegito conserva Potara in Rogue; il doppio clic apre un solo Kit; un lottatore aggiunto senza posto valido viene rifiutato con avviso.

## Anche incluso — 1.5.6.1 / 1.5.6.2

- Budget automatico delle texture più contenuto su PC modesti, risparmio di CPU del ciclo principale, recupero del dispositivo audio, correzione del percorso di disegno Intel e ricerche delle mod più rapide. Controlli allo sblocco/ripristino dei salvataggi ed errori di connessione chiari; il rollback resta disattivato per impostazione predefinita.
- Correzioni delle forme donatrici/Dragon Rush, delle parti che scomparivano in Shin Budokai (**bisogna ricostruire le mod interessate**), delle introduzioni degli scenari convertiti, più logica originale di IW e miglioramenti del selettore di scenari. Non si considerano risolte tutte le differenze di scene/cinematiche.
- Le importazioni B1/B2 e le relative ricette Nova non richiedono più B3 PS2; rispettano Scegli ISO, spiegano gli errori e includono gli script ausiliari di musica, ricolorazione e arte Eroe che mancavano. L'importazione degli attacchi di energia di Shin Budokai richiede ancora B3 PS2.

## Limiti noti

- **Tag Team · beta / Online · beta:** resta aperta la chiusura/desincronizzazione nei combattimenti lunghi. Le coppie locali non certificano il gioco via internet tra due PC né la compatibilità Windows/Linux.
- Stanza/Evoluzione (`dbz3_heroe_habitacion`, `dbz3_heroe_evolucion`), premi/Collezione (`dbz3_heroe_recompensas`) e sfondi della storia (`dbz3_heroe_fondos_historia`) restano **disattivati per impostazione predefinita**, senza accettazione del percorso completo nei rapporti di base. Ultime modifiche a Shenron, finale dei percorsi e progressione completa delle tecniche: non confermati.
- Restano ritocchi alle parti XV2 e dettagli di caricamento, selettore e aspetto di Survivors. Chiudere e riaprire durante il precaricamento è un rischio di revisione del codice, senza chiusura riprodotta. Classifica pubblica completa e tutte le difficoltà di Rogue: non verificate. Il traduttore IW non è ancora una funzione visibile per il giocatore.

**Crediti del pacchetto opzionale di personaggi:** @ArielPVB — Guldo, Jeice, Burter, Zarbon, Dodoria e Android 19.

---

# v1.5.7 — DE

Neue optionale Modi, Neuerungen im Heldenmodus und Fehlerbehebungen. Enthält auch die als 1.5.6.1 und 1.5.6.2 vermerkten Änderungen, die nicht separat veröffentlicht wurden.

## Downloads

| Datei | Wofür |
|---|---|
| `DBZ-Budokai-3-HD-Collection-v1.5.7.zip` | Spielpaket für Windows. |
| `DBZ3HD-1.5.7-Kit-Modding.zip` | Optionales Modding-Kit. |
| `DBZ-Budokai-3-HD-Collection-v1.5.7-linux-amd64.tar.gz` | Linux / Vulkan Beta, wenn angehängt; nicht auf einem echten Linux-Rechner getestet. |
| [Charakter-Paket — in Arbeit](https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing) | Separater optionaler Download; das bestehende `DBZ3HD-1.4.3-Personajes.zip`. |

Verwende die angehängten Pakete, nicht die „Source code“-Archive von GitHub.

## Installation

1. Entpacke die Windows-ZIP. Beim Aktualisieren vorher eine Kopie von Einstellungen, Spielständen und Mods sichern; die Programmdateien zusammen ersetzen, einschließlich der DLLs.
2. Füge deine eigene ISO von **Budokai 3 HD Collection (Xbox 360)** oder entpackte `default.xex` + `us/` bzw. `eu/` hinzu. Wähle Ordner/ISO in `dbz3.exe` und drücke **SPIELEN**.
3. Optional: Entpacke das Kit in den Spielordner. Die neuen Modi sind im **Dragon HUB**, hinter der letzten Zeile des Hauptmenüs. Wenn kein Schalter sichtbar ist, schließe das Spiel und ändere die vorhandene Einstellung in `dbz3_user.toml`, ohne sie zu duplizieren.

## Neu

- **Heldenmodus · Beta:** erneuerte Bildschirme (`dbz3_heroe_ui_nueva`), alternative Routen (`dbz3_heroe_rutas`) und Techniken v2 (`dbz3_heroe_tecnicas_v2`). **Jede Einstellung ist standardmäßig aus; setze sie auf `true`, um sie zu testen.** Die Routen wurden bis Akt 2 getestet; Kamehameha ausrüsten und im Training abfeuern hat den Test bestanden.
- **XV2-Ersteller · experimentell:** sechs Rassenvarianten, mit getesteten Saiyajin-, Golden-Freezer- und Majin-Transformationen. Erfordert dein installiertes Xenoverse 2; aktiviere `dbz3_cac_xv2 = true` (**standardmäßig aus**). `dbz3_xv2_dir` wählt den Ordner; leer erlaubt die automatische Erkennung. Namekianer haben keine Modell-Transformation.
- **Dragon Survivors · Beta:** Überleben mit automatischen Angriffen und Verbesserungen beim Stufenaufstieg. Aktiviere `dbz3_survivors = true`; die neuen Inhalte/Audios nutzen `dbz3_survivors_contenido_v2` und `dbz3_survivors_audio_v2`, ebenfalls auf `true`. **Alle drei standardmäßig aus.** Ein abgekürzter Sieg, der Audio-Teil und die Rückkehr zum HUB wurden erfolgreich getestet; visuelles Urteil: „als BETA akzeptabel“.
- **Dragon Rogue · Beta:** Wege wie Kumite II, Karten, Fortsetzen, Evolution/Fusion und Kami-Verbesserungen. `dbz3_rogue = true`, **standardmäßig aus**. Es gibt abgenommene gezielte Durchläufe, ohne alle Ebenen und Schwierigkeitsgrade zu zertifizieren.
- **Kompetitiver Modus · experimentell, nur US:** überarbeiteter Aura Burst, L1-Verfolgung und Ultimates ohne Hyper. `dbz3_modo_competitivo = true`, **standardmäßig aus**. Ein Paar mit denselben Regeln in **Online · Beta** bestand ohne Unterschiede bei der Prüfsumme.
- **Herausforderungen und Kit:** klarere Regeln, Revanche, Belohnungen beim Wiederholen und Angriffsnamen im Labor, **standardmäßig an** (Feinschliff-Einstellungen `dbz3_modos_*`). Die Kit-Schaltfläche im Tab Charaktere ist **standardmäßig an** (`dbz3_launcher_kit`); sie öffnet es nicht automatisch. Die Dauer von Fusionen offline nutzt `dbz3_fusion_duracion`, anfangs `"original"`.
- **Online-Rangliste · Beta:** optionaler Client für die Survivors-Rangliste/das Rogue-Tagebuch, `dbz3_ranking_online = true`, **standardmäßig aus**. Mit simuliertem Server getestet; der vollständige öffentliche Dienst ist weiterhin unbestätigt.

## Fehlerbehebungen

- Kompetitive Räume lehnen ihre Regel nicht mehr ab; Vegito behält in Rogue Potara; Doppelklick öffnet nur ein Kit; ein hinzugefügter Kämpfer ohne gültigen Platz wird mit Hinweis abgelehnt.

## Ebenfalls enthalten — 1.5.6.1 / 1.5.6.2

- Kleineres automatisches Texturbudget auf bescheidenen PCs, CPU-Ersparnis der Hauptschleife, Wiederherstellung des Audiogeräts, Korrektur des Intel-Zeichenpfads und schnellere Mod-Suche. Prüfungen beim Entsperren/Wiederherstellen von Spielständen und klare Verbindungsfehler; Rollback bleibt standardmäßig aus.
- Korrekturen an Spenderformen/Dragon Rush, an verschwindenden Teilen in Shin Budokai (**betroffene Mods müssen neu gebaut werden**), an Intros konvertierter Stages, mehr originale IW-Logik und Verbesserungen des Stage-Auswahlers. Verbleibende Unterschiede bei Szenen/Zwischensequenzen gelten nicht als behoben.
- B1/B2-Importe und die zugehörigen Nova-Rezepte brauchen kein B3 PS2 mehr; sie respektieren ISO wählen, erklären die Fehler und enthalten die fehlenden Hilfsskripte für Musik, Umfärbung und Helden-Artwork. Der Import der Energieangriffe aus Shin Budokai braucht weiterhin B3 PS2.

## Bekannte Einschränkungen

- **Tag Team · Beta / Online · Beta:** das Beenden/die Desynchronisation bei langen Kämpfen ist weiterhin offen. Lokale Paare zertifizieren weder Spiel über das Internet zwischen zwei PCs noch Windows-/Linux-Kompatibilität.
- Raum/Evolution (`dbz3_heroe_habitacion`, `dbz3_heroe_evolucion`), Belohnungen/Sammlung (`dbz3_heroe_recompensas`) und Story-Hintergründe (`dbz3_heroe_fondos_historia`) bleiben **standardmäßig aus**, ohne Abnahme des vollständigen Durchlaufs in den Basisberichten. Letzte Shenron-Änderungen, Routenende und vollständige Technik-Progression: unbestätigt.
- Es bleibt Feinarbeit an den XV2-Teilen sowie Details bei Laden, Auswahl und Aussehen von Survivors. Schließen und Wiederöffnen während des Vorladens ist ein Code-Review-Risiko, ohne reproduzierten Absturz. Vollständige öffentliche Rangliste und alle Rogue-Schwierigkeiten: nicht verifiziert. Der IW-Übersetzer ist noch keine sichtbare Funktion für Spieler.

**Credits für das optionale Charakter-Paket:** @ArielPVB — Guldo, Jeice, Burter, Zarbon, Dodoria und Android 19.

---

# v1.5.7 — FR

Nouveaux modes optionnels, nouveautés du mode Héros et corrections. Inclut aussi les changements notés 1.5.6.1 et 1.5.6.2, qui n'ont pas eu de publication séparée.

## Téléchargements

| Fichier | À quoi ça sert |
|---|---|
| `DBZ-Budokai-3-HD-Collection-v1.5.7.zip` | Paquet du jeu pour Windows. |
| `DBZ3HD-1.5.7-Kit-Modding.zip` | Kit de modding optionnel. |
| `DBZ-Budokai-3-HD-Collection-v1.5.7-linux-amd64.tar.gz` | Linux / Vulkan bêta, lorsqu'il est joint ; non testé sur une vraie machine Linux. |
| [Pack de personnages — en cours](https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing) | Téléchargement optionnel séparé ; le `DBZ3HD-1.4.3-Personajes.zip` existant. |

Utilise les paquets joints, pas les archives « Source code » de GitHub.

## Installation

1. Décompresse le ZIP Windows. Si tu mets à jour, sauvegarde d'abord une copie des réglages, parties et mods ; remplace ensemble les fichiers du programme, DLL comprises.
2. Ajoute ta propre ISO de **Budokai 3 HD Collection (Xbox 360)** ou `default.xex` + `us/` ou `eu/` extraits. Choisis dossier/ISO dans `dbz3.exe` et appuie sur **JOUER**.
3. Facultatif : décompresse le Kit dans le dossier du jeu. Les nouveaux modes sont dans le **Dragon HUB**, au-delà de la dernière ligne du menu principal. Si aucun interrupteur n'est visible, ferme le jeu et modifie le réglage existant dans `dbz3_user.toml`, sans le dupliquer.

## Nouveautés

- **Mode Héros · bêta :** écrans renouvelés (`dbz3_heroe_ui_nueva`), parcours alternatifs (`dbz3_heroe_rutas`) et techniques v2 (`dbz3_heroe_tecnicas_v2`). **Chaque réglage est désactivé par défaut ; mets-le sur `true` pour l'essayer.** Les parcours ont été testés jusqu'à l'Acte 2 ; équiper et lancer le Kamehameha en Entraînement a passé le test.
- **Créateur XV2 · expérimental :** six variantes raciales, avec transformations Saiyan, Freezer doré et Majin testées. Nécessite ton Xenoverse 2 installé ; active `dbz3_cac_xv2 = true` (**désactivé par défaut**). `dbz3_xv2_dir` choisit le dossier ; vide permet la détection automatique. Le Namek n'a pas de transformation de modèle.
- **Dragon Survivors · bêta :** survie avec attaques automatiques et améliorations en montant de niveau. Active `dbz3_survivors = true` ; les contenus/audios nouveaux utilisent `dbz3_survivors_contenido_v2` et `dbz3_survivors_audio_v2`, aussi sur `true`. **Les trois désactivés par défaut.** Une victoire abrégée, l'audio et le retour au HUB ont été validés ; verdict visuel : « acceptable en BÊTA ».
- **Dragon Rogue · bêta :** parcours comme Kumite II, cartes, reprise, évolution/fusion et améliorations de Kami. `dbz3_rogue = true`, **désactivé par défaut**. Des parcours ciblés sont approuvés, sans certifier tous les étages ni toutes les difficultés.
- **Mode Compétitif · expérimental, US uniquement :** Aura Burst revu, poursuite L1 et ultimes sans Hyper. `dbz3_modo_competitivo = true`, **désactivé par défaut**. Une paire avec les mêmes règles en **Online · bêta** a réussi sans différence de contrôle.
- **Défis et Kit :** règles plus claires, revanche, récompenses en cas de répétition et noms des coups au Laboratoire, **activés par défaut** (réglages de finition `dbz3_modos_*`). Le bouton du Kit dans l'onglet Personnages est **activé par défaut** (`dbz3_launcher_kit`) ; il ne l'ouvre pas automatiquement. La durée des fusions hors ligne utilise `dbz3_fusion_duracion`, initialement `"original"`.
- **Classement en ligne · bêta :** client optionnel de classement Survivors/journal Rogue, `dbz3_ranking_online = true`, **désactivé par défaut**. Testé avec un serveur simulé ; le service public complet reste non confirmé.

## Corrections

- Les salles compétitives ne rejettent plus leur règle ; Vegito conserve Potara dans Rogue ; le double-clic n'ouvre qu'un seul Kit ; un combattant ajouté sans place valide est refusé avec un message.

## Également inclus — 1.5.6.1 / 1.5.6.2

- Budget automatique de textures plus petit sur PC modestes, économie CPU de la boucle principale, récupération du périphérique audio, correction du chemin de rendu Intel et recherches de mods plus rapides. Vérifications au déverrouillage/restauration des sauvegardes et erreurs de connexion claires ; le rollback reste désactivé par défaut.
- Corrections des formes donneuses/Dragon Rush, des parties qui disparaissaient dans Shin Budokai (**il faut reconstruire les mods concernés**), des introductions de stages convertis, plus de logique originale d'IW et améliorations du sélecteur de stages. Les différences restantes de scènes/cinématiques ne sont pas déclarées corrigées.
- Les imports B1/B2 et les recettes Nova correspondantes n'ont plus besoin de B3 PS2 ; ils respectent Choisir l'ISO, expliquent les erreurs et incluent les scripts auxiliaires de musique, recoloration et art de Héros qui manquaient. L'import des attaques d'énergie de Shin Budokai nécessite encore B3 PS2.

## Limites connues

- **Tag Team · bêta / Online · bêta :** la fermeture/la désynchronisation en combats longs reste ouverte. Les paires locales ne certifient pas le jeu par internet entre deux PC ni la compatibilité Windows/Linux.
- Chambre/Évolution (`dbz3_heroe_habitacion`, `dbz3_heroe_evolucion`), récompenses/Collection (`dbz3_heroe_recompensas`) et fonds d'histoire (`dbz3_heroe_fondos_historia`) restent **désactivés par défaut**, sans acceptation du parcours complet dans les rapports de base. Derniers changements de Shenron, fin des parcours et progression complète des techniques : non confirmés.
- Il reste des réglages des pièces XV2 et des détails de chargement, sélecteur et apparence de Survivors. Fermer et rouvrir pendant le préchargement est un risque de revue de code, sans fermeture reproduite. Classement public complet et toutes les difficultés de Rogue : non vérifiés. Le traducteur IW n'est pas encore une fonction visible pour le joueur.

**Crédits du pack optionnel de personnages :** @ArielPVB — Guldo, Jeice, Burter, Zarbon, Dodoria et Android 19.

---

# v1.5.7 — PT

Novos modos opcionais, novidades do Modo Herói e correções. Inclui também as mudanças anotadas como 1.5.6.1 e 1.5.6.2, que não tiveram publicação separada.

## Downloads

| Arquivo | Para que serve |
|---|---|
| `DBZ-Budokai-3-HD-Collection-v1.5.7.zip` | Pacote do jogo para Windows. |
| `DBZ3HD-1.5.7-Kit-Modding.zip` | Kit de modding opcional. |
| `DBZ-Budokai-3-HD-Collection-v1.5.7-linux-amd64.tar.gz` | Linux / Vulkan beta, quando anexado; sem testes em um computador Linux real. |
| [Pacote de personagens — em desenvolvimento](https://drive.google.com/file/d/1jDRwzd_idmeIxwAMN4q4bC89JKFiGXX7/view?usp=sharing) | Download opcional separado; o `DBZ3HD-1.4.3-Personajes.zip` existente. |

Use os pacotes anexados, não os arquivos «Source code» do GitHub.

## Instalação

1. Extraia o ZIP do Windows. Se for atualizar, salve antes uma cópia das configurações, dos jogos salvos e dos mods; substitua juntos os arquivos do programa, incluindo as DLL.
2. Adicione sua própria ISO de **Budokai 3 HD Collection (Xbox 360)** ou `default.xex` + `us/` ou `eu/` extraídos. Escolha pasta/ISO no `dbz3.exe` e pressione **JOGAR**.
3. Opcional: extraia o Kit na pasta do jogo. Os modos novos ficam no **Dragon HUB**, além da última linha do menu principal. Se não houver interruptor visível, feche o jogo e altere a configuração existente no `dbz3_user.toml`, sem duplicá-la.

## Novidades

- **Modo Herói · beta:** telas renovadas (`dbz3_heroe_ui_nueva`), rotas alternativas (`dbz3_heroe_rutas`) e técnicas v2 (`dbz3_heroe_tecnicas_v2`). **Cada opção vem desligada por padrão; coloque em `true` para testar.** As rotas foram testadas até o Ato 2; equipar e lançar o Kamehameha no Treinamento passou no teste.
- **Criador XV2 · experimental:** seis variantes raciais, com transformações Saiyajin, Freeza dourado e Majin testadas. Requer seu Xenoverse 2 instalado; ative `dbz3_cac_xv2 = true` (**desligado por padrão**). `dbz3_xv2_dir` escolhe a pasta; vazio permite detecção automática. O Namekuseijin não tem transformação de modelo.
- **Dragon Survivors · beta:** sobrevivência com ataques automáticos e melhorias ao subir de nível. Ative `dbz3_survivors = true`; os conteúdos e áudios novos usam `dbz3_survivors_contenido_v2` e `dbz3_survivors_audio_v2`, também em `true`. **Os três desligados por padrão.** Uma vitória abreviada, o áudio e o retorno ao HUB foram aprovados; veredito visual: «aceitável como BETA».
- **Dragon Rogue · beta:** caminhos como Kumite II, cartas, retomada, evolução/fusão e melhorias de Kami. `dbz3_rogue = true`, **desligado por padrão**. Há percursos direcionados aprovados, sem certificar todos os andares e todas as dificuldades.
- **Modo Competitivo · experimental, apenas US:** Aura Burst revisado, perseguição L1 e definitivas sem Hyper. `dbz3_modo_competitivo = true`, **desligado por padrão**. Um par com as mesmas regras em **Online · beta** passou sem diferenças de verificação.
- **Desafios e Kit:** regras mais claras, revanche, prêmios ao repetir e nomes de golpes no Laboratório, **ligados por padrão** (ajustes de polimento `dbz3_modos_*`). O botão do Kit na aba Personagens está **ligado por padrão** (`dbz3_launcher_kit`); ele não o abre automaticamente. A duração de fusões offline usa `dbz3_fusion_duracion`, inicialmente `"original"`.
- **Ranking online · beta:** cliente opcional de classificação de Survivors/diário de Rogue, `dbz3_ranking_online = true`, **desligado por padrão**. Testado com servidor simulado; o serviço público completo continua sem confirmação.

## Correções

- As salas competitivas não rejeitam mais a própria regra; o Vegito mantém Potara no Rogue; o clique duplo abre um só Kit; um lutador adicionado sem vaga válida é rejeitado com aviso.

## Também incluído — 1.5.6.1 / 1.5.6.2

- Orçamento automático de texturas menor em PCs modestos, economia de CPU do loop principal, recuperação do dispositivo de som, correção do caminho de desenho Intel e buscas de mods mais rápidas. Verificações ao desbloquear/restaurar jogos salvos e erros de conexão claros; o rollback continua desligado por padrão.
- Correções das formas doadoras/Dragon Rush, das partes que desapareciam no Shin Budokai (**é preciso reconstruir os mods afetados**), das introduções de cenários convertidos, mais lógica original do IW e melhorias no seletor de cenários. Não se consideram resolvidas todas as diferenças de cenas/cinemáticas.
- As importações B1/B2 e as receitas Nova correspondentes não precisam mais de B3 PS2; respeitam Escolher ISO, explicam os erros e incluem os scripts auxiliares de música, recolorização e arte de Herói que faltavam. A importação de ataques de energia do Shin Budokai ainda precisa de B3 PS2.

## Limites conhecidos

- **Tag Team · beta / Online · beta:** continua aberto o fechamento/dessincronização em combates longos. Os pares locais não certificam jogo pela internet entre dois PCs nem compatibilidade Windows/Linux.
- Sala/Evolução (`dbz3_heroe_habitacion`, `dbz3_heroe_evolucion`), recompensas/Coleção (`dbz3_heroe_recompensas`) e fundos de história (`dbz3_heroe_fondos_historia`) continuam **desligados por padrão**, sem aceitação do percurso completo nos relatórios base. Últimas mudanças do Shenron, final das rotas e progressão completa de técnicas: sem confirmação.
- Faltam ajustes das peças XV2 e detalhes de carregamento, seletor e aparência do Survivors. Fechar e reabrir durante o pré-carregamento é um risco de revisão de código, sem fechamento reproduzido. Ranking público completo e todas as dificuldades do Rogue: sem verificação. O tradutor IW ainda não é uma função visível para o jogador.

**Créditos do pacote opcional de personagens:** @ArielPVB — Guldo, Jeice, Burter, Zarbon, Dodoria e Android 19.
