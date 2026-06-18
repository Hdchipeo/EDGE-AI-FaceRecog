#include "sdkconfig.h"

#if CONFIG_ENABLE_CONSOLE

#include "app_console.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "app_face.h"
#include <string.h>

static const char *TAG = "app_console";

static struct {
    struct arg_str *name;
    struct arg_end *end;
} enroll_args;

static int cmd_enroll(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **) &enroll_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, enroll_args.end, argv[0]);
        return 1;
    }
    ESP_LOGI(TAG, "Enrolling face: %s", enroll_args.name->sval[0]);
    app_face_enroll(enroll_args.name->sval[0]);
    return 0;
}

static int cmd_delete_all(int argc, char **argv) {
    ESP_LOGI(TAG, "Deleting all faces...");
    app_face_delete_all();
    return 0;
}

void app_console_init(void) {
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "face_rec>";

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
#else
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
#endif

    enroll_args.name = arg_str1(NULL, NULL, "<name>", "Name of the person");
    enroll_args.end = arg_end(1);
    
    const esp_console_cmd_t enroll_cmd = {
        .command = "enroll",
        .help = "Enroll a new face",
        .hint = NULL,
        .func = &cmd_enroll,
        .argtable = &enroll_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&enroll_cmd));

    const esp_console_cmd_t del_cmd = {
        .command = "delete_all",
        .help = "Delete all faces",
        .hint = NULL,
        .func = &cmd_delete_all,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&del_cmd));

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGI(TAG, "Console initialized! Type 'help' for commands.");
}

#endif // CONFIG_ENABLE_CONSOLE
