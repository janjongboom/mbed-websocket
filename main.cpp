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

typedef enum {
    WS_PARSING_NONE = 0,
    WS_PARSING_OPCODE = 1,
    WS_PARSING_LEN = 2,
    WS_PARSING_LEN126_1 = 3,
    WS_PARSING_LEN126_2 = 4,
    WS_PARSING_LEN127_1 = 5,
    WS_PARSING_LEN127_2 = 6,
    WS_PARSING_LEN127_3 = 7,
    WS_PARSING_LEN127_4 = 8,
    WS_PARSING_LEN127_5 = 9,
    WS_PARSING_LEN127_6 = 10,
    WS_PARSING_LEN127_7 = 11,
    WS_PARSING_LEN127_8 = 12,
    WB_PARSING_MASK_CHECK = 13,
    WB_PARSING_MASK_1 = 14,
    WB_PARSING_MASK_2 = 15,
    WB_PARSING_MASK_3 = 16,
    WB_PARSING_MASK_4 = 17,
    WS_PARSING_PAYLOAD = 18,
    WS_PARSING_DONE = 19
} WS_PARSING_STATE;

typedef struct {
    WS_PARSING_STATE state;
    bool fin;
    WS_OPCODE opcode;
    bool is_masked;
    char mask[4];
    uint32_t payload_len;
    uint32_t payload_cur_pos;
    uint8_t *payload;
} rx_ws_message_t;

int send(WS_OPCODE op_code, const uint8_t *data, size_t data_size);

WS_PARSING_STATE handle_rx_msg(rx_ws_message_t *msg, const uint8_t c) {
    printf("handle_rx_msg state=%d\n", msg->state);

    int i;

    switch (msg->state) {
        case WS_PARSING_NONE:
        case WS_PARSING_OPCODE:
            memset(msg->mask, 0, 4);    // empty mask
            msg->fin = c >> 7 & 0x1;    // first bit indicates the fin flag
            msg->opcode = (WS_OPCODE)(c & 0b1111);   // last four bits indicate the opcode
            msg->state = WS_PARSING_LEN;
            break;

        case WS_PARSING_LEN:
            msg->payload_len = c & 0x7f;
            msg->is_masked = c & 0x80;
            msg->payload_cur_pos = 0;
            if (msg->payload_len == 126) {
                msg->state = WS_PARSING_LEN126_1;
            }
            else if (msg->payload_len == 127) {
                msg->payload_len = 0;
                msg->state = WS_PARSING_LEN127_1;
            }
            else {
                msg->payload = (uint8_t*)malloc(msg->payload_len);
                msg->state = WB_PARSING_MASK_CHECK;
            }
            break;

        case WS_PARSING_LEN126_1:
            msg->payload_len = c << 8;
            msg->state = WS_PARSING_LEN126_2;
            break;

        case WS_PARSING_LEN126_2:
            msg->payload_len += c;
            msg->state = WB_PARSING_MASK_CHECK;
            break;

        case WS_PARSING_LEN127_1:
        case WS_PARSING_LEN127_2:
        case WS_PARSING_LEN127_3:
        case WS_PARSING_LEN127_4:
        case WS_PARSING_LEN127_5:
        case WS_PARSING_LEN127_6:
        case WS_PARSING_LEN127_7:
        case WS_PARSING_LEN127_8:
            i = msg->state - WS_PARSING_LEN127_1;
            msg->payload_len += (c << (7 - i) * 8);
            msg->state = (WS_PARSING_STATE)((int)msg->state + 1);
            break;

        case WB_PARSING_MASK_CHECK:
            if (!msg->is_masked) {
                msg->payload_cur_pos = 0;
                msg->state = WS_PARSING_PAYLOAD;
            }
            else {
                msg->state = WB_PARSING_MASK_1;
            }
            return handle_rx_msg(msg, c);

        case WB_PARSING_MASK_1:
            msg->mask[0] = c;
            msg->state = WB_PARSING_MASK_2;
            break;

        case WB_PARSING_MASK_2:
            msg->mask[1] = c;
            msg->state = WB_PARSING_MASK_3;
            break;

        case WB_PARSING_MASK_3:
            msg->mask[2] = c;
            msg->state = WB_PARSING_MASK_4;
            break;

        case WB_PARSING_MASK_4:
            msg->mask[3] = c;
            msg->payload_cur_pos = 0;
            msg->state = WS_PARSING_PAYLOAD;
            break;

        case WS_PARSING_PAYLOAD:
            msg->payload[msg->payload_cur_pos] = c ^ msg->mask[msg->payload_cur_pos % 4];
            if (msg->payload_cur_pos + 1 == msg->payload_len) {
                msg->state = WS_PARSING_DONE;
            }
            msg->payload_cur_pos++;
            break;

        case WS_PARSING_DONE:
            break;
    }

    printf("handle_rx_msg now state=%d\n", msg->state);
    return msg->state;
}

static rx_ws_message_t curr_msg;

void handle_socket_sigio() {
    static uint8_t rx_buffer[1024];

    printf("handle_socket_sigio\n");

    nsapi_size_or_error_t r = socket.recv(rx_buffer, sizeof(rx_buffer));
    printf("socket.recv returned %d\n", r); // 0 would be fine, would block would be fine too
    if (r > 0) {
        for (int ix = 0; ix < r; ix++) {
            printf("Handling char, c=%02x, state=%d\n", rx_buffer[ix], curr_msg.state);
            WS_PARSING_STATE s = handle_rx_msg(&curr_msg, rx_buffer[ix]);
            printf("Handling char done, c=%02x, state=%d\n", rx_buffer[ix], curr_msg.state);
            if (s == WS_PARSING_DONE) {
                printf("Websocket msg, opcode=%u, len=%lu: ", curr_msg.opcode, curr_msg.payload_len);
                for (size_t jx = 0; jx < curr_msg.payload_len; jx++) {
                    printf("%c", curr_msg.payload[jx]);
                }
                printf("\n");
            }
            curr_msg.state = WS_PARSING_NONE;
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

    return 0;
}


void fall() {
    const char *msg = "hello world";
    send(WS_TEXT_FRAME, (const uint8_t*)msg, strlen(msg));
}

int main() {
    curr_msg.state = WS_PARSING_NONE;

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

    queue.call_every(10000, &send, WS_PING_FRAME, nullptr, 0);

    queue.dispatch_forever();
}
