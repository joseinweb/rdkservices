# If not stated otherwise in this file or this component's license file the
# following copyright and licenses apply:
#
# Copyright 2020 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(PLUGIN_NAME MemMonitor)
set(MODULE_NAME ${NAMESPACE}${PLUGIN_NAME})

set(PLUGIN_MEMMONITOR_AUTOSTART "false" CACHE STRING "Automatically start Memory Monitor plugin")
set(PLUGIN_MEMMONITOR_MODE "Off" CACHE STRING "Controls if the plugin should run in its own process, in process or remote")

find_package(${NAMESPACE}Plugins REQUIRED)
find_package(${NAMESPACE}Definitions REQUIRED)
find_package(CompileSettingsDebug CONFIG REQUIRED)

find_package(IARMBus REQUIRED)

add_library(${MODULE_NAME} SHARED
    MemMonitor.cpp
    Module.cpp
    ../helpers/utils.cpp
    ../helpers/tptimer.cpp
)
target_include_directories(${MODULE_NAME} PRIVATE
        ../helpers
        ${IARMBUS_INCLUDE_DIRS})

set_target_properties(${MODULE_NAME} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        CompileSettingsDebug::CompileSettingsDebug
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${NAMESPACE}Definitions::${NAMESPACE}Definitions
        ${IARMBUS_LIBRARIES})

install(TARGETS ${MODULE_NAME}
    DESTINATION lib/${STORAGE_DIRECTORY}/plugins)

write_config(${PLUGIN_NAME})