// Web previewer: a tiny HTTP server that hands the live framebuffer to the
// bench page and takes clicks back, so the panel rides inside the browser —
// no SDL window needed to see or drive the UI.
//
//   GET  /frame.bmp            the glass, right now (32bpp BMP)
//   POST /touch?x=&y=&d=1|0    press/drag (d=1) or release (d=0), panel px
//
// One request per connection, no keep-alive: the bench polls ~12 fps over
// loopback, which is ~20 MB/s of localhost traffic — noise. The python bench
// server proxies these under /panel/*, so the page stays same-origin.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hw/display_hw.h"   // SCREEN_WIDTH/HEIGHT

extern void sim_frame_copy(uint32_t* dst);
extern void sim_web_touch(int x, int y, bool down);

static int s_port = 0;

static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}
static void put_le16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

// 54-byte header + bottom-up BGRA rows: the most boring BMP there is, which
// is exactly why every browser renders it.
static size_t frame_bmp(uint8_t* out) {
    const uint32_t W = SCREEN_WIDTH, H = SCREEN_HEIGHT;
    const uint32_t body = W * H * 4, total = 54 + body;
    memset(out, 0, 54);
    out[0] = 'B'; out[1] = 'M';
    put_le32(out + 2, total);
    put_le32(out + 10, 54);
    put_le32(out + 14, 40);
    put_le32(out + 18, W);
    put_le32(out + 22, H);
    put_le16(out + 26, 1);
    put_le16(out + 28, 32);
    put_le32(out + 34, body);

    static uint32_t* frame = nullptr;
    if (!frame) frame = (uint32_t*)malloc(W * H * sizeof(uint32_t));
    sim_frame_copy(frame);
    uint8_t* rows = out + 54;
    for (uint32_t y = 0; y < H; y++)
        memcpy(rows + (H - 1 - y) * W * 4, frame + y * W, W * 4);
    return total;
}

static void send_all(int fd, const void* buf, size_t n) {
    const char* p = (const char*)buf;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w <= 0) return;
        p += w; n -= (size_t)w;
    }
}

static void handle(int fd) {
    char req[512];
    ssize_t n = read(fd, req, sizeof(req) - 1);
    if (n <= 0) return;
    req[n] = 0;

    if (!strncmp(req, "GET /frame", 10)) {
        static uint8_t* bmp = nullptr;
        if (!bmp) bmp = (uint8_t*)malloc(54 + SCREEN_WIDTH * SCREEN_HEIGHT * 4);
        size_t len = frame_bmp(bmp);
        char hdr[160];
        int hl = snprintf(hdr, sizeof(hdr),
                          "HTTP/1.1 200 OK\r\nContent-Type: image/bmp\r\n"
                          "Content-Length: %zu\r\nCache-Control: no-store\r\n"
                          "Connection: close\r\n\r\n", len);
        send_all(fd, hdr, (size_t)hl);
        send_all(fd, bmp, len);
        return;
    }
    int x, y, d;
    if (sscanf(req, "POST /touch?x=%d&y=%d&d=%d", &x, &y, &d) == 3) {
        if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT)
            sim_web_touch(x, y, d != 0);
        send_all(fd, "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n", 47);
        return;
    }
    send_all(fd, "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n", 45);
}

static void* serve(void*) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)s_port);
    if (bind(srv, (sockaddr*)&a, sizeof(a)) != 0 || listen(srv, 8) != 0) {
        fprintf(stderr, "sim_web: cannot listen on %d\n", s_port);
        return nullptr;
    }
    for (;;) {
        int fd = accept(srv, nullptr, nullptr);
        if (fd < 0) continue;
        handle(fd);
        close(fd);
    }
}

void sim_web_start(int port) {
    s_port = port;
    pthread_t t;
    pthread_create(&t, nullptr, serve, nullptr);
    pthread_detach(t);
    printf("web previewer: http://127.0.0.1:%d/frame.bmp\n", port);
}
