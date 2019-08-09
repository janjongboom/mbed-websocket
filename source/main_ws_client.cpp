#include "select_demo.h"

#if DEMO == DEMO_WS

#include "mbed.h"
#include "ws_client.h"

// we need an event queue to pass events around
EventQueue queue;
WsClient *client;
bool client_connected = false;

// When button is pressed send a message
void fall() {
    if (!client_connected) return;

    const char *msg = "hello world";
    nsapi_error_t r = client->send(WS_TEXT_FRAME, (const uint8_t*)msg, strlen(msg));

    if (r == NSAPI_ERROR_OK) {
        printf("Sent %d bytes over websocket\n", strlen(msg));
    }
    else {
        printf("Failed to send over websocket (%d)\n", r);
    }
}

// Message received from the ws server
void rx_callback(WS_OPCODE opcode, uint8_t *buffer, size_t buffer_size) {
    printf("Received message on websocket. opcode=%u, buffer_size=%lu, content: ", opcode, buffer_size);
    for (size_t ix = 0; ix < buffer_size; ix++) {
        printf("%c", buffer[ix]);
    }
    printf("\n");
}

// fwd declaration of connect, so we can reconnect when something goes awry
void connect();

// invoked when pong is not received
void disconnect_callback() {
    printf("Websocket disconnected, trying to reconnect in 5 seconds\n");
    client_connected = false;
    queue.call_in(5000, &connect);
}

// connect callbacks (global so this remains in scope)
ws_callbacks_t ws_callbacks = {
    rx_callback,
    disconnect_callback
};

// connect to the websocket server
void connect() {
    printf("Connecting to websocket server...\n");
    int r = client->connect(&ws_callbacks);
    if (r == 0) {
        printf("Connected to websocket server\n");
        client_connected = true;
    }
    else {
        printf("Failed to connect to websocket server (%d)\n", r);
    }
}

int main() {
    printf("Mbed Websocket demo (ws://echo.websocket.org)!\n");
    WiFiInterface *network = WiFiInterface::get_default_instance();
    if (!network) {
        printf("No default network interface found...\n");
        return 1;
    }

    printf("Connecting to network...\n");

    nsapi_error_t r = network->connect("yolo 5GHz", "0624710192", NSAPI_SECURITY_WPA2);
    if (r != 0) {
        printf("Network connection failed (%d)\n", r);
        return 1;
    }
    printf("Network connected\n");

    client = new WsClient(&queue, WiFiInterface::get_default_instance(), "ws://echo.websocket.org");
    connect();

    InterruptIn btn(BUTTON1);
    btn.fall(queue.event(&fall));

    printf("Press BUTTON1 to send a message to the server\n");

    queue.dispatch_forever();
}

#endif // DEMO == DEMO_WS
