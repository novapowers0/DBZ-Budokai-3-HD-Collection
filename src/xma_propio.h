// dbz3 - Sonidos XMA de los bancos #AMB de B3 HD para codigo propio (SUR-07 via C): el decodificador "xmaframes" del FFmpeg que
// ya trae el SDK (el mismo que usa xma_context.cpp), sin pasar por el hardware XMA del juego ni por el gestor de efectos. Asi un modo
// propio suena golpes, explosiones y gritos del juego sin un combate oculto. Se lee de los AFS del jugador: nada se distribuye.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace dbz3::xma {

struct Sonido {
  std::vector<int16_t> pcm;   // intercalado si canales == 2
  int hz = 0, canales = 1;
};

// Paquetes XMA2 de 2048 B (un solo flujo) -> PCM de 16 bits. `muestras` > 0 recorta a esa longitud (PlayLength del banco).
std::vector<int16_t> Decodificar(const uint8_t* datos, size_t bytes, int canales, int muestras = 0, int hz = 48000);
// Sonido `i` de un banco de B3 HD (suelto: efectos de data_cmn; o hijo 0 de un #AMB: gritos de lang_*). Falso si no hay o no es XMA.
bool SonidoDeBanco(const std::vector<uint8_t>& amb, int i, Sonido& out);
// Volumen (0..1) de la secuencia del sonido `i` en un IECS "Sequ" (data_cmn 3985 para el banco 3983...; 1 si no esta).
float VolumenSecuencia(const std::vector<uint8_t>& sq, int i);
// Cuantos sonidos tiene el primer banco (0 si no es un banco).
int SonidosEnBanco(const std::vector<uint8_t>& amb);
// Reproduce el sonido `i` de la entrada `entrada` de `afs` (cache por entrada; decodifica la primera vez) en un hilo con waveOut.
// `tipo` 0..7: un sonido nuevo del mismo tipo corta al anterior (-1 = no corta). Devuelve falso si no se pudo cargar (no bloquea:
// la carga va en el hilo; el falso solo sale si la entrada ya fallo antes).
bool Oir(const std::filesystem::path& afs, int entrada, int i, float volumen, int tipo = -1);
// Carga (decodifica) el sonido `i` de la entrada en segundo plano para que el primer Oir no tarde.
void Precargar(const std::filesystem::path& afs, int entrada, int i = 0);
void CallarTodo();

}  // namespace dbz3::xma
