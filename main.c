#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <json-c/json.h>
#include <json-c/json_tokener.h>
#include "dpdkInit/dpdk.h"
#include "private.h"

#define DEFAULT_CONFIG_FILE "config.json"
#define MAX_FILE_SIZE (10 * 1024 * 1024)

static bool read_file_into_buffer(const char* filename, char** buffer, size_t* size) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        return false;
    }

    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return false;
    }

    long file_size = ftell(file);
    if (file_size < 0 || file_size > MAX_FILE_SIZE) {
        fclose(file);
        fprintf(stderr, "Invalid file size: %ld\n", file_size);
        return false;
    }

    rewind(file);

    *buffer = malloc(file_size + 1);
    if (!*buffer) {
        fclose(file);
        fprintf(stderr, "Memory allocation failed\n");
        return false;
    }

    size_t bytes_read = fread(*buffer, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        free(*buffer);
        fprintf(stderr, "File read error: expected %ld, got %zu\n", file_size, bytes_read);
        return false;
    }

    (*buffer)[file_size] = '\0';
    *size = file_size;
    return true;
}

static bool parse_json_from_buffer(const char* buffer, struct json_object** json_obj) {
    *json_obj = json_tokener_parse(buffer);
    if (!*json_obj) {
        fprintf(stderr, "JSON parsing failed\n");
        return false;
    }
    return true;
}

static bool extract_eal_arguments(struct json_object* json_obj, struct readConf* conf) {
    struct json_object* eal_args;
    if (!json_object_object_get_ex(json_obj, "eal_args", &eal_args)) {
        fprintf(stderr, "Missing 'eal_args' in configuration\n");
        return false;
    }

    if (!json_object_is_type(eal_args, json_type_array)) {
        fprintf(stderr, "'eal_args' is not an array\n");
        return false;
    }

    conf->argc = json_object_array_length(eal_args);
    conf->argv = malloc((conf->argc + 1) * sizeof(*conf->argv));
    if (!conf->argv) {
        fprintf(stderr, "Memory allocation failed for EAL args\n");
        return false;
    }

    for (int i = 0; i < conf->argc; i++) {
        struct json_object* arg = json_object_array_get_idx(eal_args, i);
        if (!json_object_is_type(arg, json_type_string)) {
            fprintf(stderr, "Non-string value in eal_args at index %d\n", i);
            return false;
        }

        conf->argv[i] = strdup(json_object_get_string(arg));
        if (!conf->argv[i]) {
            fprintf(stderr, "Memory allocation failed for argument %d\n", i);
            return false;
        }
    }

    conf->argv[conf->argc] = NULL;
    return true;
}

static void free_readConf(struct readConf* conf) {
    if (conf->argv) {
        for (int i = 0; i < conf->argc; i++) {
            free(conf->argv[i]);
        }
        free(conf->argv);
        conf->argv = NULL;
    }
    conf->argc = 0;
}

bool load_configurations(const char* config_file, struct readConf* conf) {
    char* json_buffer = NULL;
    size_t buffer_size = 0;
    struct json_object* json_obj = NULL;
    bool status = false;

    if (!read_file_into_buffer(config_file, &json_buffer, &buffer_size)) {
        goto cleanup;
    }

    if (!parse_json_from_buffer(json_buffer, &json_obj)) {
        goto cleanup;
    }

    if (!extract_eal_arguments(json_obj, conf)) {
        goto cleanup;
    }

    status = true;

cleanup:
    free(json_buffer);
    if (json_obj) json_object_put(json_obj);
    if (!status) free_readConf(conf);
    return status;
}

int main(void) {
    struct readConf config = {0};

    if (!load_configurations(DEFAULT_CONFIG_FILE, &config)) {
        fprintf(stderr, "Failed to load configurations\n");
        return EXIT_FAILURE;
    }

    int ret = dpdkInit(config.argc, config.argv);
    free_readConf(&config);

    if (ret != 0) {
        fprintf(stderr, "DPDK initialization failed with error %d\n", ret);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}