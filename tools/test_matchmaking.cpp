// Prueba de la logica del matchmaking: clang++ -std=c++20 -I src/ingame tools/test_matchmaking.cpp -o D:/tmp/online2/tm.exe
#include <cassert>
#include <cstdio>
#include "matchmaking.h"
using namespace dbz3::mm;
static std::string L(const char* msg) { return std::string("{\"id\":\"x\",\"time\":1,\"event\":\"message\",\"topic\":\"t\",\"message\":\"") + msg + "\"}\n"; }
int main() {
  std::string b = std::string("{\"event\":\"open\"}\n") + L("dbz3 1 OPEN AAAAA-11111") + L("dbz3 1 OPEN BBBBB-22222") + L("dbz3 1 BYE AAAAA-11111") +
                  L("hola mundo") + L("dbz3 1 OPEN CCCCC-33333") + L("dbz3 1 OPEN BBBBB-22222");
  auto live = LiveCodes(b);
  assert(live.size() == 2 && live[0] == "BBBBB-22222" && live[1] == "CCCCC-33333");
  assert(PickJoin(live, "", {}) == "BBBBB-22222");
  assert(PickJoin(live, "", {"BBBBB-22222"}) == "CCCCC-33333");
  assert(PickJoin(live, "CCCCC-33333", {}) == "BBBBB-22222");   // la mayor cede
  assert(PickJoin(live, "BBBBB-22222", {}).empty());            // la menor espera
  assert(LiveCodes("").empty());
  assert(Topic("ABCDEF0123456789", "") == "dbz3hd-mm-abcdef0123-v");
  assert(Topic("ABCDEF0123456789", "1234567890") == "dbz3hd-mm-abcdef0123-12345678");
  std::puts("matchmaking: OK");
}
