cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/examples/common_components/protocol_examples_common"
						 "$ENV{HOME}/esp/esp-aws-iot/libraries/backoffAlgorithm"
						 "$ENV{HOME}/esp/esp-aws-iot/libraries/coreMQTT"
						 "$ENV{HOME}/esp/esp-aws-iot/libraries/corePKCS11"
						 "$ENV{HOME}/esp/esp-aws-iot/libraries/Fleet-Provisioning-for-AWS-IoT-embedded-sdk"
						 "$ENV{HOME}/esp/esp-aws-iot/libraries/common/posix_compat"
						 )

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(Thread_BR)
