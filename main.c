#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <json-c/json.h>
#include <json-c/json_tokener.h>
#include "dpdkInit/dpdk.h"
#include "private.h"

#define DEFAULT_CONFIG_FILE "config.json"

struct readConf read;
const char *filename = "exData.xml.data";

/**
 * @brief Load configurations from a JSON file.
 *
 * @param config_file Path to the configuration file.
 */
void load_configurations(const char *config_file)
{
    FILE *file = fopen(config_file, "r");
    if (file == NULL)
    {
        fprintf(stderr, "Error opening file.\n");
        return;
    }

    // Get the file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read the JSON data from the file
    char *json_data = (char *)malloc(file_size + 1);
    fread(json_data, 1, file_size, file);
    fclose(file);
    json_data[file_size] = '\0';

    // Parse the JSON data
    struct json_object *json = json_tokener_parse(json_data);
    if (json == NULL)
    {
        fprintf(stderr, "Error parsing JSON.\n");
        free(json_data);
        return;
    }

    // Extract configuration values
    struct json_object *item;

    if (json_object_object_get_ex(json, "eal_args", &item) && json_object_is_type(item, json_type_array))
    {
        read.argc = json_object_array_length(item);
        read.argv = (char **)malloc((read.argc + 1) * sizeof(char *)); // +1 for the null terminator

        for (int i = 0; i < read.argc; ++i)
        {
            struct json_object *arg_item = json_object_array_get_idx(item, i);
            if (arg_item != NULL && json_object_is_type(arg_item, json_type_string))
            {
                read.argv[i] = strdup(json_object_get_string(arg_item));
            }
        }

        read.argv[read.argc] = NULL;
    }

    // Clean up
    free(json_data);
    json_object_put(json);
}

int main()
{
    // Load configurations
    load_configurations(DEFAULT_CONFIG_FILE);

    // Initialize DPDK
    dpdkInit(read.argc, read.argv);

    return 0;
}
