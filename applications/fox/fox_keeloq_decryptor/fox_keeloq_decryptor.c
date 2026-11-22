

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <storage/storage.h>
#include <toolbox/stream/file_stream.h>
#include <toolbox/stream/buffered_file_stream.h>
#include <flipper_format/flipper_format.h>
#include <lib/subghz/subghz_keystore.h>

#define KEYSTORE_PATH "/ext/subghz/assets/keeloq_mfcodes"
#define OUTPUT_PATH "/ext/keystore_decrypted.txt"

typedef struct
{
    Gui *gui;
    ViewPort *view_port;
    FuriMessageQueue *queue;
    Storage *storage;
    Stream *output_stream;
    uint32_t keys_decrypted;
    bool finished;
    char status[128];
} KeystoreDecryptApp;

static void keystore_input_callback(InputEvent *event, void *context)
{
    KeystoreDecryptApp *app = context;
    if (event->type == InputTypeShort && event->key == InputKeyBack)
    {
        furi_message_queue_put(app->queue, event, 0);
    }
}

static void keystore_draw_callback(Canvas *canvas, void *context)
{
    KeystoreDecryptApp *app = context;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "KeeLoq Keystore Decryptor");

    canvas_set_font(canvas, FontSecondary);

    if (!app->finished)
    {
        canvas_draw_str(canvas, 0, 30, "Decrypting...");
        canvas_draw_str(canvas, 0, 40, app->status);
    }
    else
    {
        canvas_draw_str(canvas, 0, 30, "Done!");
        char buf[64];
        snprintf(buf, sizeof(buf), "Keys: %lu", app->keys_decrypted);
        canvas_draw_str(canvas, 0, 40, buf);
        canvas_draw_str(canvas, 0, 50, "Saved to:");
        canvas_draw_str(canvas, 0, 60, OUTPUT_PATH);
    }
}

static KeystoreDecryptApp *app_alloc(void)
{
    KeystoreDecryptApp *app = malloc(sizeof(KeystoreDecryptApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_port = view_port_alloc();
    app->queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    view_port_draw_callback_set(app->view_port, keystore_draw_callback, app);
    view_port_input_callback_set(app->view_port, keystore_input_callback, app);

    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->storage = furi_record_open(RECORD_STORAGE);
    app->keys_decrypted = 0;
    app->finished = false;
    app->output_stream = NULL;
    strncpy(app->status, "Starting...", sizeof(app->status));

    return app;
}

static void app_free(KeystoreDecryptApp *app)
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

static bool decrypt_keystore(KeystoreDecryptApp *app)
{
    app->output_stream = buffered_file_stream_alloc(app->storage);
    if (!buffered_file_stream_open(
            app->output_stream, OUTPUT_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS))
    {
        strncpy(app->status, "Output error", sizeof(app->status));
        return false;
    }

    const char *header = "# Decrypted Keystore\n# Format: KEY:TYPE:NAME\n\n";
    stream_write(app->output_stream, (uint8_t *)header, strlen(header));

    strncpy(app->status, "Loading keystore...", sizeof(app->status));
    view_port_update(app->view_port);

    SubGhzKeystore *keystore = subghz_keystore_alloc();
    if (!subghz_keystore_load(keystore, KEYSTORE_PATH))
    {
        strncpy(app->status, "Keystore load failed", sizeof(app->status));
        subghz_keystore_free(keystore);
        app->finished = true;
        return false;
    }

    SubGhzKeyArray_t *data = subghz_keystore_get_data(keystore);
    size_t count = SubGhzKeyArray_size(*data);
    for (size_t i = 0; i < count; i++)
    {
        SubGhzKey *k = SubGhzKeyArray_get(*data, i);
        FuriString *out_line = furi_string_alloc_printf(
            "%016llX:%u:%s\n",
            (unsigned long long)k->key,
            k->type,
            (k->name) ? furi_string_get_cstr(k->name) : "");
        stream_write(
            app->output_stream,
            (uint8_t *)furi_string_get_cstr(out_line),
            furi_string_size(out_line));
        furi_string_free(out_line);

        app->keys_decrypted++;
        if (app->keys_decrypted % 25 == 0)
        {
            snprintf(
                app->status, sizeof(app->status),
                "Decrypted: %lu", app->keys_decrypted);
            view_port_update(app->view_port);
        }
    }

    subghz_keystore_free(keystore);

    app->finished = true;
    snprintf(app->status, sizeof(app->status), "Done! Keys: %lu", app->keys_decrypted);
    return true;
}

int32_t keystore_decrypt_app(void *p)
{
    UNUSED(p);

    KeystoreDecryptApp *app = app_alloc();

    decrypt_keystore(app);

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
