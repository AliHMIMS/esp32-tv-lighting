#include "hyperion_server.h"

#include <lwip/sockets.h>

#include "leds.h"

// Codec capture on "High" sends up to 1280x720 RGB (~2.7 MB); anything bigger
// isn't a frame we want.
static constexpr size_t MAX_MESSAGE = 3 * 1024 * 1024;

static uint8_t *msg_buf = nullptr;
static portMUX_TYPE client_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t client_addr = 0;
static char client_origin[32] = "";

// ---------------------------------------------------------------- FlatBuffers

// Just enough of the FlatBuffers wire format to read hyperion_request.fbs.
// Every access is bounds-checked: the input comes straight off the network.
struct Fb {
  const uint8_t *buf;
  size_t len;
  bool has(size_t pos, size_t n) const { return pos <= len && n <= len - pos; }
  template <class T> T get(size_t pos) const {
    T v;
    memcpy(&v, buf + pos, sizeof(T));  // FlatBuffers is little-endian, like the ESP32
    return v;
  }
};

struct Table {
  const Fb *fb = nullptr;
  size_t pos = 0, vt = 0;
  uint16_t vt_len = 0;

  bool valid() const { return fb != nullptr; }

  static Table at(const Fb &fb, size_t pos) {
    Table t;
    if (!fb.has(pos, 4)) return t;
    int64_t vt = (int64_t)pos - fb.get<int32_t>(pos);
    if (vt < 0 || !fb.has(vt, 4)) return t;
    uint16_t vt_len = fb.get<uint16_t>(vt);
    if (vt_len < 4 || !fb.has(vt, vt_len)) return t;
    t.fb = &fb;
    t.pos = pos;
    t.vt = vt;
    t.vt_len = vt_len;
    return t;
  }

  // Byte offset of a field inside the table, 0 if absent.
  uint16_t field(int slot) const {
    size_t o = 4 + 2 * slot;
    if (!valid() || o + 2 > vt_len) return 0;
    return fb->get<uint16_t>(vt + o);
  }

  template <class T> T scalar(int slot, T def) const {
    uint16_t o = field(slot);
    if (!o || !fb->has(pos + o, sizeof(T))) return def;
    return fb->get<T>(pos + o);
  }

  // Follows an offset field (table, vector, string) to its absolute position.
  size_t ref(int slot) const {
    uint16_t o = field(slot);
    if (!o || !fb->has(pos + o, 4)) return 0;
    uint32_t rel = fb->get<uint32_t>(pos + o);
    if (rel > fb->len) return 0;
    size_t target = pos + o + rel;
    return fb->has(target, 4) ? target : 0;
  }

  Table table(int slot) const {
    size_t t = ref(slot);
    return t ? Table::at(*fb, t) : Table();
  }

  const uint8_t *vec(int slot, uint32_t &n) const {
    size_t t = ref(slot);
    if (!t) return nullptr;
    n = fb->get<uint32_t>(t);
    return fb->has(t + 4, n) ? fb->buf + t + 4 : nullptr;
  }
};

// Union type ids, in schema declaration order (0 = NONE)
enum Command : uint8_t { CMD_COLOR = 1, CMD_IMAGE = 2, CMD_CLEAR = 3, CMD_REGISTER = 4 };
enum ImageType : uint8_t { IMG_RAW = 1, IMG_NV12 = 2 };

static void send_all(int fd, const uint8_t *data, size_t len) {
  while (len) {
    int n = send(fd, data, len, 0);
    if (n <= 0) return;
    data += n;
    len -= n;
  }
}

// Reply { error: absent, video: absent, registered: priority }, hand-built.
static void send_register_reply(int fd, int32_t priority) {
  uint8_t m[4 + 24] = {};
  m[3] = 24;  // big-endian size prefix
  uint8_t *b = m + 4;
  const uint32_t root = 16;
  const uint16_t vtable[5] = {10, 8, 0, 0, 4};  // vtable size, table size, slot offsets
  const int32_t soffset = 16 - 4;               // table at 16, vtable at 4
  memcpy(b, &root, 4);
  memcpy(b + 4, vtable, sizeof(vtable));
  memcpy(b + 16, &soffset, 4);
  memcpy(b + 20, &priority, 4);
  send_all(fd, m, sizeof(m));
}

static void handle_message(int fd, const uint8_t *data, size_t len) {
  Fb fb{data, len};
  if (!fb.has(0, 4)) return;
  Table req = Table::at(fb, fb.get<uint32_t>(0));
  uint8_t type = req.scalar<uint8_t>(0, 0);
  Table cmd = req.table(1);
  if (!cmd.valid()) return;

  switch (type) {
    case CMD_IMAGE: {
      if (cmd.scalar<uint8_t>(0, 0) != IMG_RAW) return;  // grabbers we target never send NV12
      Table raw = cmd.table(1);
      uint32_t n = 0;
      const uint8_t *px = raw.vec(0, n);
      int32_t w = raw.scalar<int32_t>(1, -1), h = raw.scalar<int32_t>(2, -1);
      if (px && w > 0 && h > 0 && (uint64_t)w * h * 3 <= n) leds::submit_frame(px, w, h);
      break;
    }
    case CMD_COLOR: {
      int32_t c = cmd.scalar<int32_t>(0, -1);
      if (c >= 0) leds::submit_color((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
      break;
    }
    case CMD_CLEAR:
      leds::clear();
      break;
    case CMD_REGISTER: {
      uint32_t n = 0;
      const uint8_t *origin = cmd.vec(0, n);
      portENTER_CRITICAL(&client_mux);
      size_t k = origin ? min<size_t>(n, sizeof(client_origin) - 1) : 0;
      memcpy(client_origin, origin, k);
      client_origin[k] = 0;
      portEXIT_CRITICAL(&client_mux);
      send_register_reply(fd, cmd.scalar<int32_t>(1, 0));
      Serial.printf("Grabber registered: %s\n", client_origin);
      break;
    }
  }
}

// ---------------------------------------------------------------- server

static void set_client(uint32_t addr) {
  portENTER_CRITICAL(&client_mux);
  client_addr = addr;
  client_origin[0] = 0;
  portEXIT_CRITICAL(&client_mux);
}

static void server_task(void *) {
  int lfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  int one = 1;
  setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(hyperion::PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(lfd, (sockaddr *)&addr, sizeof(addr)) != 0 || listen(lfd, 2) != 0) {
    Serial.println("Hyperion server: bind/listen failed");
    vTaskDelete(nullptr);
  }

  int cfd = -1;
  uint8_t hdr[4];
  size_t hdr_got = 0, body_len = 0, body_got = 0;
  auto drop = [&](const char *why) {
    Serial.printf("Grabber disconnected (%s)\n", why);
    close(cfd);
    cfd = -1;
    set_client(0);
  };

  while (true) {
    fd_set rs;
    FD_ZERO(&rs);
    FD_SET(lfd, &rs);
    int maxfd = lfd;
    if (cfd >= 0) {
      FD_SET(cfd, &rs);
      maxfd = max(maxfd, cfd);
    }
    timeval tv = {1, 0};
    if (select(maxfd + 1, &rs, nullptr, nullptr, &tv) <= 0) continue;

    if (FD_ISSET(lfd, &rs)) {
      sockaddr_in peer;
      socklen_t plen = sizeof(peer);
      int nfd = accept(lfd, (sockaddr *)&peer, &plen);
      if (nfd >= 0) {
        // Newest connection wins: the app reconnects after network hiccups,
        // and its old socket may linger half-open.
        if (cfd >= 0) drop("replaced by new connection");
        cfd = nfd;
        hdr_got = body_len = body_got = 0;
        int idle = 10, intvl = 5, cnt = 3;  // detect a TV that vanished (power off)
        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        setsockopt(cfd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
        setsockopt(cfd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
        setsockopt(cfd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
        setsockopt(cfd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
        set_client(peer.sin_addr.s_addr);
        Serial.printf("Grabber connected from %s\n", inet_ntoa(peer.sin_addr));
      }
    }

    if (cfd < 0 || !FD_ISSET(cfd, &rs)) continue;

    // Messages are a 4-byte big-endian length followed by a FlatBuffer.
    if (hdr_got < 4) {
      int n = recv(cfd, hdr + hdr_got, 4 - hdr_got, 0);
      if (n <= 0) {
        drop(n == 0 ? "closed" : "error");
        continue;
      }
      hdr_got += n;
      if (hdr_got == 4) {
        body_len = (size_t)hdr[0] << 24 | hdr[1] << 16 | hdr[2] << 8 | hdr[3];
        body_got = 0;
        if (body_len == 0 || body_len > MAX_MESSAGE) {
          drop("message too big, lower the grabber's capture size");
        }
      }
    } else {
      int n = recv(cfd, msg_buf + body_got, body_len - body_got, 0);
      if (n <= 0) {
        drop(n == 0 ? "closed" : "error");
        continue;
      }
      body_got += n;
      if (body_got == body_len) {
        handle_message(cfd, msg_buf, body_len);
        hdr_got = 0;
      }
    }
  }
}

bool hyperion::begin() {
  msg_buf = (uint8_t *)ps_malloc(MAX_MESSAGE);
  if (!msg_buf) {
    Serial.println("Hyperion server: no PSRAM for the frame buffer");
    return false;
  }
  xTaskCreatePinnedToCore(server_task, "hyperion", 6144, nullptr, 2, nullptr, 0);
  return true;
}

String hyperion::client() {
  char origin[sizeof(client_origin)];
  portENTER_CRITICAL(&client_mux);  // no heap allocation inside the critical section
  uint32_t a = client_addr;
  memcpy(origin, client_origin, sizeof(origin));
  portEXIT_CRITICAL(&client_mux);
  if (!a) return "";
  String s = IPAddress(a).toString();
  if (origin[0]) s += String(" (") + origin + ")";
  return s;
}
