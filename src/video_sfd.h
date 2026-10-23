// dbz3 - Reproductor de videos SFD de CRI Sofdec (frente R7, 2026-10-16): las escenas de la historia de Infinite World
// (USR/FD01A..FD20.SFD + ENDING.SFD de la copia del jugador, leidas en marcha; nunca se reparten).
//
// SFD = MPEG-1 PS: video MPEG-2 512x448 (stream 0xE0; FFmpeg 4.4 del SDK, objetivo propio de src/video_sfd.cmake),
// audio ADX mono 48 kHz en dos streams (0xC0 "_jp.sfa", 0xC1 "_us.sfa": los nombres van en la cabecera Sofdec del
// stream 0xBF) y nada mas. Suena el idioma de voces del juego (ingles o japones) por winmm (waveOut, aparte de la mezcla del juego) con el volumen y el
// silencio del juego; el video va al reloj del audio. Se dibuja a pantalla completa por encima de todo (ImGui, fondo
// negro, 4:3 del video) y A o START lo saltan.
#pragma once
#include <filesystem>
#include <vector>

#include "pasarela.h"   // pasarela::Imagen

namespace dbz3::video {

// Subtitulos: los tiempos van DENTRO del SFD (bloque CRITAG "USRINFS TYPE=SUBTITLE" de los paquetes 0xBF del
// principio: por canal CHDAT/CHNO, TUNIT=30 y TIMEDAT {INTIME, DURTIME, CONTENT = indice de la imagen}; lo lee el
// propio IW con Movie_DrawFrameAndSubtitle, EU 0x338C10). Canal 2 = inglés; EU usa 3=alemán, 4=francés,
// 5=español, 6=italiano según dbz3_language. `subtitulos` = imágenes de DATA_USA 77 + video (AfsPs2 resuelve idioma).
// Empieza a reproducir (corta el que hubiera). Devuelve false si el archivo no existe. Hilo cualquiera.
bool Reproducir(const std::filesystem::path& ruta, std::vector<pasarela::Imagen> subtitulos = {}, int canal_subtitulos = 2);
// true desde Reproducir hasta que acaba o se salta. Mientras, el juego lee un mando neutro y su sonido calla
// (audio_gain 0); las pantallas ImGui propias (GetStateForUI) no deben leer el mando y, al acabar, esperar a que se
// suelte la A que lo salto.
bool Activo();
void Saltar();

// Videos de IW por indice (el de las tablas de escenas del ELF, EscenaIW::video: 0 FD01A, 1 FD01B, 2..7 FD02..FD07,
// 8 FD08A, 9 FD08B, 10..21 FD09..FD20) o kFinalIW = ENDING.SFD (creditos del final: sin subtitulos, musica estereo
// SBGMF_02, 4 min 51 s). Busca la copia de IW (pasarela::BuscarIW) y carga los subtitulos de DATA_USA 77 + video.
constexpr int kFinalIW = 22;
bool ReproducirIW(int video);

}  // namespace dbz3::video
