/*
 * KeeLoq Keystore Encryptor -- re-encrypts a plaintext KeeLoq keystore for Flipper Zero.
 * Companion to, and derived from, the KeeLoq Keystore Decryptor
 * (https://github.com/Lechnio/KeeLoq-Keystore-Decryptor); it performs the exact inverse operation.
 *
 * Reads the PLAINTEXT keystore at /ext/subghz/assets/keeloq_mfcodes (Encryption: 0) and writes an
 * AES-256 encrypted keystore (Encryption: 1) to /ext/keystore_encrypted.txt, byte-compatible with
 * what lib/subghz/subghz_keystore.c's loader expects: per-line AES-256-CBC via secure-enclave key
 * slot 1, each KEY:TYPE:NAME line zero-padded to a multiple of 16 bytes then hex-encoded (uppercase),
 * one line per key. The enclave key only exists on real hardware, which is why this must run on the
 * Flipper rather than off-device.
 *
 * IV handling matches the firmware loader exactly: the chosen IV is written verbatim to the header,
 * and a working copy is transformed by the same "mess_with_iv" cascade the firmware applies before
 * loading the key -- result[i] = (iv[i] + iv[i-1]) mod 256 for i = 15..1, iv[0] left unchanged. The
 * fixed IV "ARF_The_Firmware" is reused (the value the original encrypted keystore used), so the
 * output round-trips with both the firmware and the decryptor app -- for this IV the decryptor's
 * saturating cascade and the firmware's modulo cascade produce the same transformed IV.
 */

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <storage/storage.h>
#include <toolbox/stream/file_stream.h>
#include <toolbox/stream/buffered_file_stream.h>

#define KEYSTORE_PATH "/ext/subghz/assets/keeloq_mfcodes"
#define OUTPUT_PATH "/ext/keystore_encrypted.txt"
#define KEY_ENCLAVE_ID 1
#define DECRYPTED_LINE_SIZE 512
#define ENCRYPTED_LINE_SIZE (DECRYPTED_LINE_SIZE * 2)

typedef struct
{
    Gui *gui;
    ViewPort *view_port;
    FuriMessageQueue *queue;
    Storage *storage;
    Stream *output_stream;
    uint32_t keys_encrypted;
    bool finished;
    char status[128];
} KeystoreEncryptApp;

// The 16-byte IV stored in the header, ASCII "ARF_The_Firmware" -- the same value the original ARF
// keystore used, so the encrypted output is verifiable with the existing decryptor app.
static const uint8_t KEYSTORE_IV[16] =
    {0x41, 0x52, 0x46, 0x5F, 0x54, 0x68, 0x65, 0x5F,
     0x46, 0x69, 0x72, 0x6D, 0x77, 0x61, 0x72, 0x65};

static void keystore_input_callback(InputEvent *event, void *context)
{
    KeystoreEncryptApp *app = context;
    if (event->type == InputTypeShort && event->key == InputKeyBack)
    {
        furi_message_queue_put(app->queue, event, 0);
    }
}

static void keystore_draw_callback(Canvas *canvas, void *context)
{
    KeystoreEncryptApp *app = context;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "KeeLoq Keystore Encryptor");

    canvas_set_font(canvas, FontSecondary);

    if (!app->finished)
    {
        canvas_draw_str(canvas, 0, 30, "Encrypting...");
        canvas_draw_str(canvas, 0, 40, app->status);
    }
    else
    {
        canvas_draw_str(canvas, 0, 30, "Done!");
        char buf[64];
        snprintf(buf, sizeof(buf), "Keys: %lu", app->keys_encrypted);
        canvas_draw_str(canvas, 0, 40, buf);
        canvas_draw_str(canvas, 0, 50, "Saved to:");
        canvas_draw_str(canvas, 0, 60, OUTPUT_PATH);
    }
}

static KeystoreEncryptApp *app_alloc(void)
{
    KeystoreEncryptApp *app = malloc(sizeof(KeystoreEncryptApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_port = view_port_alloc();
    app->queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    view_port_draw_callback_set(app->view_port, keystore_draw_callback, app);
    view_port_input_callback_set(app->view_port, keystore_input_callback, app);

    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->storage = furi_record_open(RECORD_STORAGE);
    app->keys_encrypted = 0;
    app->finished = false;
    app->output_stream = NULL;
    strncpy(app->status, "Starting...", sizeof(app->status));

    return app;
}

static void app_free(KeystoreEncryptApp *app)
{
    if (app->output_stream)
    {
        buffered_file_stream_close(app->output_stream);
        stream_free(app->output_stream);
    }

    furi_record_close(RECORD_STORAGE);

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);

    furi_message_queue_free(app->queue);
    free(app);
}

static bool encrypt_keystore(KeystoreEncryptApp *app)
{
    Stream *keystore_stream = buffered_file_stream_alloc(app->storage);
    if (!buffered_file_stream_open(keystore_stream, KEYSTORE_PATH, FSAM_READ, FSOM_OPEN_EXISTING))
    {
        strncpy(app->status, "Keystore not found", sizeof(app->status));
        stream_free(keystore_stream);
        return false;
    }

    // Phase 1: read the header and confirm the input is plaintext (Encryption: 0), then rewind to
    // the first key line. Refuse an already-encrypted file so it is never double-encrypted.
    char hline[256];
    size_t hpos = 0;
    bool header_parsed = false;
    int encryption = -1;

    while (!header_parsed)
    {
        uint8_t c;
        if (stream_read(keystore_stream, &c, 1) != 1)
            break;

        if (c == '\n')
        {
            hline[hpos] = '\0';

            bool is_header = (strncmp(hline, "Filetype:", 9) == 0) ||
                             (strncmp(hline, "Version:", 8) == 0) ||
                             (strncmp(hline, "Encryption:", 11) == 0) ||
                             (strncmp(hline, "IV:", 3) == 0) ||
                             (hpos > 0 && hline[0] == '#');

            if (!is_header)
            {
                header_parsed = true;
                if (hpos > 0)
                {
                    stream_seek(keystore_stream, -((int)hpos + 1), StreamOffsetFromCurrent);
                }
                break;
            }

            if (strncmp(hline, "Encryption:", 11) == 0)
            {
                const char *v = hline + 11;
                while (*v == ' ')
                    v++;
                encryption = (*v == '0') ? 0 : 1;
            }

            hpos = 0;
        }
        else if (c != '\r' && hpos < sizeof(hline) - 1)
        {
            hline[hpos++] = c;
        }
    }

    if (encryption == 1)
    {
        strncpy(app->status, "Input already encrypted", sizeof(app->status));
        buffered_file_stream_close(keystore_stream);
        stream_free(keystore_stream);
        return false;
    }

    app->output_stream = buffered_file_stream_alloc(app->storage);
    if (!buffered_file_stream_open(
            app->output_stream, OUTPUT_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS))
    {
        strncpy(app->status, "Output error", sizeof(app->status));
        buffered_file_stream_close(keystore_stream);
        stream_free(keystore_stream);
        return false;
    }

    const char *out_header =
        "Filetype: Flipper SubGhz Keystore File\nVersion: 0\nEncryption: 1\n";
    stream_write(app->output_stream, (uint8_t *)out_header, strlen(out_header));

    char iv_line[80];
    int iv_pos = snprintf(iv_line, sizeof(iv_line), "IV:");
    for (int i = 0; i < 16; i++)
    {
        iv_pos += snprintf(iv_line + iv_pos, sizeof(iv_line) - iv_pos, " %02X", KEYSTORE_IV[i]);
    }
    snprintf(iv_line + iv_pos, sizeof(iv_line) - iv_pos, "\n");
    stream_write(app->output_stream, (uint8_t *)iv_line, strlen(iv_line));

    // Mirror the firmware's mess_with_iv (modulo byte cascade) on a working copy before loading the key.
    uint8_t iv[16];
    memcpy(iv, KEYSTORE_IV, sizeof(iv));
    for (int i = 15; i >= 1; i--)
    {
        iv[i] = (uint8_t)(iv[i] + iv[i - 1]);
    }

    if (!furi_hal_crypto_enclave_load_key(KEY_ENCLAVE_ID, iv))
    {
        strncpy(app->status, "Crypto init failed", sizeof(app->status));
        buffered_file_stream_close(keystore_stream);
        stream_free(keystore_stream);
        return false;
    }

    static const char hexchars[] = "0123456789ABCDEF";
    char line[DECRYPTED_LINE_SIZE];
    uint8_t plain[DECRYPTED_LINE_SIZE];
    uint8_t cipher[DECRYPTED_LINE_SIZE];
    char hex[ENCRYPTED_LINE_SIZE];
    size_t line_pos = 0;
    uint8_t rbuf[64];
    bool done_reading = false;

    while (!done_reading)
    {
        size_t ret = stream_read(keystore_stream, rbuf, sizeof(rbuf));
        size_t n = ret;
        if (ret == 0)
        {
            done_reading = true;
            n = 1; // one synthetic newline to flush a final unterminated line
        }

        for (size_t i = 0; i < n; i++)
        {
            uint8_t c = (ret == 0) ? (uint8_t)'\n' : rbuf[i];

            if (c == '\n')
            {
                line[line_pos] = '\0';

                if (line_pos > 0 && line[0] != '#')
                {
                    size_t len = line_pos;
                    size_t enc_len = ((len + 15) / 16) * 16;
                    if (enc_len > DECRYPTED_LINE_SIZE)
                        enc_len = DECRYPTED_LINE_SIZE;

                    memset(plain, 0, sizeof(plain));
                    memcpy(plain, line, (len < enc_len) ? len : enc_len);

                    if (furi_hal_crypto_encrypt(plain, cipher, enc_len))
                    {
                        for (size_t j = 0; j < enc_len; j++)
                        {
                            hex[j * 2] = hexchars[(cipher[j] >> 4) & 0xF];
                            hex[j * 2 + 1] = hexchars[cipher[j] & 0xF];
                        }
                        stream_write(app->output_stream, (uint8_t *)hex, enc_len * 2);
                        stream_write(app->output_stream, (uint8_t *)"\n", 1);
                        app->keys_encrypted++;

                        if (app->keys_encrypted % 10 == 0)
                        {
                            snprintf(
                                app->status,
                                sizeof(app->status),
                                "Encrypted: %lu",
                                app->keys_encrypted);
                            view_port_update(app->view_port);
                        }
                    }
                }

                line_pos = 0;
            }
            else if (c != '\r' && line_pos < DECRYPTED_LINE_SIZE - 1)
            {
                line[line_pos++] = c;
            }
        }
    }

    furi_hal_crypto_enclave_unload_key(KEY_ENCLAVE_ID);
    buffered_file_stream_close(keystore_stream);
    stream_free(keystore_stream);

    app->finished = true;
    snprintf(app->status, sizeof(app->status), "Done! Keys: %lu", app->keys_encrypted);

    return true;
}

int32_t keystore_encrypt_app(void *p)
{
    UNUSED(p);

    KeystoreEncryptApp *app = app_alloc();

    encrypt_keystore(app);

    InputEvent event;
    while (true)
    {
        view_port_update(app->view_port);

        if (furi_message_queue_get(app->queue, &event, 100) == FuriStatusOk)
        {
            if (event.type == InputTypeShort && event.key == InputKeyBack)
            {
                break;
            }
        }
    }

    app_free(app);
    return 0;
}
