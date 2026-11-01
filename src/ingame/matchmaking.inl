// dbz3 - Matchmaking del online (incluido por online_panel.cpp dentro de su namespace anonimo). Lo mueve la pantalla
// ONLINE en cada cuadro (hilo de la interfaz, el dueno de Session); los hilos de fondo solo hacen peticiones HTTP.
struct MmReq {
  std::atomic<bool> done{false};
  bool ok = false;
  std::string body;
};
struct MmTopic {
  std::mutex mu;
  std::string v;
  std::string get() { std::lock_guard<std::mutex> l(mu); return v; }
  void set(std::string s) { std::lock_guard<std::mutex> l(mu); v = std::move(s); }
};
std::string g_build_digest;   // huella del programa (la fija la admision), parte del canal

// Mods que cambian la partida (como la huella del online, sin hashear): carpetas activas con algun fichero que no sea
// textura de pack ni manifest. Vacio = vanilla. Hilo de fondo.
std::string MmModsKey() {
  if (std::getenv("DBZ3_VANILLA_SESSION")) return {};
  namespace fs = std::filesystem;
  std::error_code ec;
  std::vector<std::string> names;
  for (auto it = fs::directory_iterator(dbz3::launcher::ModPipeline::ModsDir(), ec); !ec && it != fs::directory_iterator();
       it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    const std::string name = rex::path_to_utf8(it->path().filename());
    if (name.empty() || name[0] == '.' || name.ends_with(".disabled") || name == "_forma_unica" ||
        fs::exists(it->path() / ".disabled", ec))
      continue;
    bool counts = false;
    std::error_code e2;
    for (auto f = fs::recursive_directory_iterator(it->path(), e2); !e2 && f != fs::recursive_directory_iterator();
         f.increment(e2)) {
      if (!f->is_regular_file(e2)) continue;
      std::string ext = rex::path_to_utf8(f->path().extension());
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      if ((ext == ".png" || ext == ".dds") && dbz3::settings::IsTexturePackFileName(rex::path_to_utf8(f->path().stem())))
        continue;
      if (rex::path_to_utf8(f->path().filename()) == "manifest.txt") continue;
      counts = true;
      break;
    }
    if (counts) names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  std::string acc;
  for (const auto& n : names) acc += n + "\n";
  return acc.empty() ? std::string() : rex::hash_bytes(acc);
}

std::string UrlEnc(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.') o += char(c);
    else { char b[4]; std::snprintf(b, sizeof(b), "%%%02X", c); o += b; }
  }
  return o;
}

struct Mm {
  enum St { kOff, kFirstPoll, kHost, kJoin, kMatched, kFail } st = kOff;
  std::shared_ptr<MmTopic> topic = std::make_shared<MmTopic>();
  std::shared_ptr<MmReq> poll;
  std::string base, announced, join_code, fail_msg;
  std::set<std::string> bad;
  double next_poll = 0, next_renew = 0, join_at = 0;
  int tries = 0, poll_fails = 0;
  bool test_local = false;   // PRUEBAS (DBZ3_TEST_MM): anuncia tambien el codigo de red local

  static std::shared_ptr<MmReq> Fetch(std::function<std::string()> make_url) {
    auto r = std::make_shared<MmReq>();
    std::thread([r, make_url] {
      const std::string url = make_url();
      r->ok = !url.empty() && dbz3::launcher::HttpGetUrl(url, r->body);
      r->done = true;
    }).detach();
    return r;
  }
  void StartPoll(bool first) {
    auto tp = topic;
    const std::string b = base;
    poll = Fetch([tp, b, first] {
      if (first && tp->get().empty()) {
        std::string t = dbz3::mm::Topic(g_build_digest, MmModsKey());
        if (const char* x = std::getenv("DBZ3_TEST_MM_TOPIC")) t += std::string("-") + x;   // PRUEBAS: topic propio
        tp->set(t);
      }
      return b + "/" + tp->get() + "/json?poll=1&since=80s";
    });
  }
  void Publish(const char* kind, const std::string& code) {
    if (code.empty()) return;
    auto tp = topic;
    const std::string b = base, msg = dbz3::mm::Message(kind, code);
    Fetch([tp, b, msg] {
      const std::string t = tp->get();
      return t.empty() ? std::string() : b + "/" + t + "/publish?message=" + UrlEnc(msg);
    });
  }
  bool Active() const { return st == kFirstPoll || st == kHost || st == kJoin; }
  void Start() {
    base = REXCVAR_GET(dbz3_mm_url);
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (const char* u = std::getenv("DBZ3_TEST_MM_URL")) base = u;   // PRUEBAS: servidor falso local
    test_local = std::getenv("DBZ3_TEST_MM") != nullptr;
    topic->set("");
    bad.clear();
    announced.clear();
    join_code.clear();
    fail_msg.clear();
    tries = poll_fails = 0;
    st = kFirstPoll;
    StartPoll(true);
    REXLOG_INFO("dbz3 mm: buscando partida en {}", base);
  }
  void Cancel() {
    if (!announced.empty()) Publish("BYE", announced);
    announced.clear();
    if (st != kOff) REXLOG_INFO("dbz3 mm: busqueda cancelada");
    st = kOff;
  }
  void Fail(Session& s, const std::string& msg) {
    Cancel();
    s.Close();
    fail_msg = msg;
    st = kFail;
    REXLOG_WARN("dbz3 mm: fallo: {}", msg);
  }
  // Un paso por cuadro. `admit` = admision del panel (identidad del programa).
  void Tick(Session& s, double now, int port, int delay, const std::function<bool()>& admit) {
    if (!Active()) return;
    const Link l = s.link();
    if (l == Link::kConnected || l == Link::kStarting || l == Link::kPlaying) {
      if (!announced.empty()) Publish("BYE", announced);
      announced.clear();
      st = kMatched;
      REXLOG_INFO("dbz3 mm: emparejados ({})", join_code.empty() ? "anfitrion" : "invitado");
      return;
    }
    auto start_host = [&]() {
      std::string err;
      if (!admit()) return Fail(s, T::T("No se pudo preparar la conexion.", "Could not prepare the connection."));
      if (!s.Host(uint16_t(port), uint8_t(delay), &err)) return Fail(s, err);
      st = kHost;
      join_code.clear();
      next_poll = now + 7;
      poll_fails = 0;
    };
    auto start_join = [&](const std::string& code) {
      std::string err;
      if (!admit() || !s.Join(code, uint16_t(port), &err)) {
        bad.insert(code);
        if (++tries >= 4) return start_host();
        st = kFirstPoll;
        StartPoll(false);
        return;
      }
      join_code = code;
      join_at = now;
      st = kJoin;
      REXLOG_INFO("dbz3 mm: uniendose a {}", code);
    };
    if (st == kFirstPoll) {
      if (!poll->done) return;
      if (!poll->ok) {
        const std::string net = dbz3::launcher::LastNetworkError();
        poll.reset();
        return Fail(s, std::string(T::T("No se pudo contactar con el servicio de busqueda. Que hacer: comprueba tu internet, o usa CREAR SALA / UNIRME CON CODIGO.",
                                        "Could not reach the matchmaking service. What to do: check your internet, or use CREATE A ROOM / JOIN WITH A CODE.")) +
                            (net.empty() ? "" : " [" + net + "]"));
      }
      const std::string pick = dbz3::mm::PickJoin(dbz3::mm::LiveCodes(poll->body), "", bad);
      poll.reset();
      if (!pick.empty()) start_join(pick); else start_host();
      return;
    }
    if (st == kJoin) {
      if (l == Link::kError || l == Link::kLost || now - join_at > 20.0) {
        REXLOG_INFO("dbz3 mm: {} no responde", join_code);
        bad.insert(join_code);
        s.Close();
        if (++tries >= 4) return start_host();
        st = kFirstPoll;
        StartPoll(false);
      }
      return;
    }
    // kHost: anuncia el codigo cuando es publico (IP del router o STUN), lo renueva y mira si hay una sala menor
    if (l == Link::kError || l == Link::kLost) return Fail(s, T::T("Se corto la conexion.", "The connection was cut."));
    std::string how;
    const std::string code = s.RoomCode(&how);
    if ((how != "local" || test_local) && !code.empty()) {
      if (code != announced) {
        if (!announced.empty()) Publish("BYE", announced);
        announced = code;
        Publish("OPEN", code);
        next_renew = now + 30;
        REXLOG_INFO("dbz3 mm: anunciada la sala {}", code);
      } else if (now >= next_renew) {
        Publish("OPEN", code);
        next_renew = now + 30;
      }
    }
    if (poll && poll->done) {
      if (poll->ok) {
        poll_fails = 0;
        const std::string pick = dbz3::mm::PickJoin(dbz3::mm::LiveCodes(poll->body), announced, bad);
        poll.reset();
        if (!pick.empty()) {   // regla de la carrera: la sala mayor cede a la menor
          REXLOG_INFO("dbz3 mm: cedo mi sala a {}", pick);
          if (!announced.empty()) Publish("BYE", announced);
          announced.clear();
          s.Close();
          return start_join(pick);
        }
      } else {
        ++poll_fails;
        poll.reset();
      }
    }
    if (!poll && now >= next_poll) {
      next_poll = now + 7;
      StartPoll(false);
    }
  }
};
Mm g_mm;
