set(requires "")
idf_build_get_property(target IDF_TARGET)

if(${target} STREQUAL "linux")
    list(APPEND requires esp_stubs esp-tls esp_http_server protocol_examples_common nvs_flash cJSON)
endif()



idf_component_register(SRCS "sync_time.c" "main.c"
                    INCLUDE_DIRS "." "../esp_lwip_wg"
                    REQUIRES ${requires})
