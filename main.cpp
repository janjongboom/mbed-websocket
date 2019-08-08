#include "mbed.h"
#include "http_request.h"

EventQueue queue;
TCPSocket socket;

typedef enum {
    WS_CONTINUATION_FRAME = 0,
    WS_TEXT_FRAME = 1,
    WS_BINARY_FRAME = 2,
    WS_CONNECTION_CLOSE_FRAME = 8,
    WS_PING_FRAME = 9,
    WS_PONG_FRAME = 10
} WS_OPCODE;

int send(const uint8_t *data, size_t data_size);

void handle_socket_sigio() {
    static uint8_t rx_buffer[1024];

    printf("handle_socket_sigio\n");

    nsapi_size_or_error_t r = socket.recv(rx_buffer, sizeof(rx_buffer));
    printf("socket.recv returned %d\n", r); // 0 would be fine, would block would be fine too
    if (r > 0) {
        bool fin = rx_buffer[0] >> 7 & 0x1;     // first bit indicates the fin flag
        uint8_t opcode = rx_buffer[0] & 0b1111; // last four bits indicate the opcode
        char mask[4] = {0, 0, 0, 0};

        printf("Raw: ");
        for (size_t ix = 0; ix < r; ix++) {
            printf("%02x ", rx_buffer[ix]);
        }
        printf("\n");

        int c = 1;

        uint32_t ws_length = rx_buffer[c] & 0x7f;
        bool is_masked = rx_buffer[c] & 0x80;
        if (ws_length == 126) {
            c++;
            ws_length = rx_buffer[c] << 8;
            c++;
            ws_length += rx_buffer[c];
        } else if (ws_length == 127) {
            ws_length = 0;
            for (int i = 0; i < 8; i++) {
                c++;
                ws_length += (rx_buffer[c] << (7-i)*8);
            }
        }

        printf("Parsed length: %lu\n", ws_length);

        printf("Payload: ");
        // mask stuff.. if needed?
        for (int i = c; i < c + ws_length; i++) {
            rx_buffer[i] = rx_buffer[i] ^ mask[i % 4];
            printf("%02x ", rx_buffer[i]);
        }
        printf("\n");


        // schedule pong frame
        if (opcode == WS_PING_FRAME) {
            queue.call(&send, WS_PONG_FRAME, nullptr, 0);
        }
    }
}

int setLength(uint32_t len, uint8_t *buffer) {

    if (len < 126) {
        buffer[0] = len | (1<<7);
        return 1;
    } else if (len < 65535) {
        buffer[0] = 126 | (1<<7);
        buffer[1] = (len >> 8) & 0xff;
        buffer[2] = len & 0xff;
        return 3;
    } else {
        buffer[0] = 127 | (1<<7);
        for (int i = 0; i < 8; i++) {
            buffer[i+1] = (len >> i*8) & 0xff;
        }
        return 9;
    }
}

int setOpcode(WS_OPCODE opcode, uint8_t *buffer) {
    buffer[0] = 0x80 | (opcode & 0x0f);
    return 1;
}

int setMask(uint8_t *buffer) {
    for (int i = 0; i < 4; i++) {
        buffer[i] = 0;
    }
    return 4;
}

uint8_t ws_buffer[1024];
int send(WS_OPCODE opcode, const uint8_t *data, size_t data_size) {
    if (data_size + 15 > sizeof(ws_buffer)) {
        return -10; // not enough space
    }

    memset(ws_buffer, 0, sizeof(ws_buffer));
    int idx = 0;
    idx = setOpcode(opcode, ws_buffer);
    idx += setLength(data_size, ws_buffer + idx);
    idx += setMask(ws_buffer + idx);
    memcpy(ws_buffer + idx, data, data_size);
    idx += data_size;

    nsapi_size_or_error_t r = socket.send((const uint8_t*)ws_buffer, idx);
    printf("send returned %d\n", r);
}


void fall() {
    const char *msg = "hello world";
    send(WS_TEXT_FRAME, (const uint8_t*)msg, strlen(msg));
}

int main() {
    WiFiInterface *network = WiFiInterface::get_default_instance();
    if (!network) {
        printf("No network\n");
    }

    nsapi_error_t r;

    r = network->connect("yolo 5GHz", "0624710192", NSAPI_SECURITY_WPA2);
    printf("Connect %d\n", r);

    r = socket.open(network);
    printf("Socket open %d\n", r);

    r = socket.connect("echo.websocket.org", 80);
    printf("Socket connect %d\n", r);

    // @todo: calculate new keys myself
    // var key = 'L159VM0TWUzyDxwJEIEzjw=='
    // var combined = 'L159VM0TWUzyDxwJEIEzjw==' + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
    // var h = require('crypto').createHash('sha1')
    // h.update(combined).digest('base64')
    HttpRequest* req = new HttpRequest(&socket, HTTP_GET, "ws://echo.websocket.org");
    req->set_header("Upgrade", "Websocket");
    req->set_header("Connection", "Upgrade");
    req->set_header("Sec-WebSocket-Key", "L159VM0TWUzyDxwJEIEzjw==");
    req->set_header("Sec-WebSocket-Version", "13");

    HttpResponse* res = req->send();

    printf("Response: %d - %s\n", res->get_status_code(), res->get_status_message().c_str());

    bool has_valid_upgrade = false;
    bool has_valid_websocket_accept = false;

    printf("Headers:\n");
    for (size_t ix = 0; ix < res->get_headers_length(); ix++) {
        const char *header_key = res->get_headers_fields()[ix]->c_str();
        const char *header_value = res->get_headers_values()[ix]->c_str();

        // @todo: do case-insensitive compare on the header key
        if (strcmp(header_key, "Upgrade") == 0 && strcmp(header_value, "websocket") == 0) {
            has_valid_upgrade = true;
        }
        if (strcmp(header_key, "Sec-WebSocket-Accept") == 0 && strcmp(header_value, "DdLWT/1JcX+nQFHebYP+rqEx5xI=") == 0) {
            has_valid_websocket_accept = true;
        }

        printf("\t%s: %s\n", header_key, header_value);
    }
    printf("\nBody (%lu bytes):\n\n%s\n", res->get_body_length(), res->get_body_as_string().c_str());

    if (res->get_status_code() != 101) {
        printf("No valid status code\n");
    }

    if (!has_valid_upgrade) {
        printf("No valid upgrade header found\n");
    }
    if (!has_valid_websocket_accept) {
        printf("No valid accept header found\n");
    }

    delete req;

    // now switch to non-blocking

    socket.set_blocking(false);
    socket.sigio(queue.event(&handle_socket_sigio));

    InterruptIn btn(BUTTON1);
    btn.fall(queue.event(&fall));

    printf("init done\n");

    queue.dispatch_forever();
}
