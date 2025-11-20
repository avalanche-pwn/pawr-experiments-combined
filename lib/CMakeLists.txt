# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_INTERACTIVE interactive)
add_subdirectory_ifdef(CONFIG_DATA_GENERATOR data_generator)
add_subdirectory(crypto)
