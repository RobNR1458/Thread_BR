idf_component_register(SRCS "aws_task.c" "thread_coap_task.c" "Thread_BR.c"
                    INCLUDE_DIRS "."
                    REQUIRES coreMQTT backoffAlgorithm aws_helpers
                    EMBED_TXTFILES ${PROJECT_DIR}/certs/aws-root-ca.pem
                    ${PROJECT_DIR}/certs/device.crt
                    ${PROJECT_DIR}/certs/device.key)
