idf_component_register(
    SRCS "network_transport.c" "clock_esp.c"
    INCLUDE_DIRS "."
    REQUIRES esp-tls mbedtls esp_timer coreMQTT
)
