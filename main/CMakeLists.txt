idf_component_register(SRCS "wifi_connectivity_watchdog.c" "aws_task.c"
                            "thread_coap_task.c"
                            "Thread_BR.c"
                            "border_router_launch.c"
                            "wifi_onboarding/wifi_onboarding.c"
                            "wifi_onboarding/dns_server.c"
                    INCLUDE_DIRS "." "wifi_onboarding"
                    REQUIRES coreMQTT backoffAlgorithm aws_helpers esp_http_server espressif__esp_ot_cli_extension
                    EMBED_TXTFILES ${PROJECT_DIR}/certs/aws-root-ca.pem
                                   ${PROJECT_DIR}/certs/device.crt
                                   ${PROJECT_DIR}/certs/device.key
                                   ${PROJECT_DIR}/main/wifi_onboarding/portal.html)
