#include "select_demo.h"

#if DEMO == DEMO_WSS

#include "mbed.h"
#include "wss_client.h"

/* List of trusted root CA certificates
 * currently one: DST Root CA X3, the CA for websocket.org
 *
 * To add more root certificates, just concatenate them.
 */
const char SSL_CA_PEM[] =  "-----BEGIN CERTIFICATE-----\n"
    "MIIDSjCCAjKgAwIBAgIQRK+wgNajJ7qJMDmGLvhAazANBgkqhkiG9w0BAQUFADA/\n"
    "MSQwIgYDVQQKExtEaWdpdGFsIFNpZ25hdHVyZSBUcnVzdCBDby4xFzAVBgNVBAMT\n"
    "DkRTVCBSb290IENBIFgzMB4XDTAwMDkzMDIxMTIxOVoXDTIxMDkzMDE0MDExNVow\n"
    "PzEkMCIGA1UEChMbRGlnaXRhbCBTaWduYXR1cmUgVHJ1c3QgQ28uMRcwFQYDVQQD\n"
    "Ew5EU1QgUm9vdCBDQSBYMzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB\n"
    "AN+v6ZdQCINXtMxiZfaQguzH0yxrMMpb7NnDfcdAwRgUi+DoM3ZJKuM/IUmTrE4O\n"
    "rz5Iy2Xu/NMhD2XSKtkyj4zl93ewEnu1lcCJo6m67XMuegwGMoOifooUMM0RoOEq\n"
    "OLl5CjH9UL2AZd+3UWODyOKIYepLYYHsUmu5ouJLGiifSKOeDNoJjj4XLh7dIN9b\n"
    "xiqKqy69cK3FCxolkHRyxXtqqzTWMIn/5WgTe1QLyNau7Fqckh49ZLOMxt+/yUFw\n"
    "7BZy1SbsOFU5Q9D8/RhcQPGX69Wam40dutolucbY38EVAjqr2m7xPi71XAicPNaD\n"
    "aeQQmxkqtilX4+U9m5/wAl0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNV\n"
    "HQ8BAf8EBAMCAQYwHQYDVR0OBBYEFMSnsaR7LHH62+FLkHX/xBVghYkQMA0GCSqG\n"
    "SIb3DQEBBQUAA4IBAQCjGiybFwBcqR7uKGY3Or+Dxz9LwwmglSBd49lZRNI+DT69\n"
    "ikugdB/OEIKcdBodfpga3csTS7MgROSR6cz8faXbauX+5v3gTt23ADq1cEmv8uXr\n"
    "AvHRAosZy5Q6XkjEGB5YGV8eAlrwDPGxrancWYaLbumR9YbK+rlmM6pZW87ipxZz\n"
    "R8srzJmwN0jP41ZL9c8PDHIyh8bwRLtTcm1D9SZImlJnt1ir/md2cXjbDaJWFBM5\n"
    "JDGFoqgCWjBH4d1QB7wCCZAA62RjYJsWvIjJEubSfZGL+T0yjWW06XyxV3bqxbYo\n"
    "Ob8VZRzI9neWagqNdwvYkQsEjgfbKbYK7p2CNTUQ\n"
    "-----END CERTIFICATE-----\n";


// we need an event queue to pass events around
EventQueue queue;
WssClient *client;
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
    mbed_trace_init();

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

    client = new WssClient(&queue, WiFiInterface::get_default_instance(), SSL_CA_PEM, "wss://echo.websocket.org");
    connect();

    InterruptIn btn(BUTTON1);
    btn.fall(queue.event(&fall));

    printf("Press BUTTON1 to send a message to the server\n");

    queue.dispatch_forever();
}

#endif // DEMO == DEMO_WS
