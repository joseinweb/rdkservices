/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#include "RIoTControl.h"
#include "UtilsJsonRpc.h"
#include "AvahiClient.h"

#include <sstream>
#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework
{
    namespace
    {

        static Plugin::Metadata<Plugin::RIoTControl> metadata(
            // Version (Major, Minor, Patch)
            API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
            // Preconditions
            {},
            // Terminations
            {},
            // Controls
            {});
    }

    namespace Plugin
    {

        std::string getRemoteAddress()
        {
            if (avahi::initialize())
            {
                std::list<std::shared_ptr<avahi::RDKDevice> > devices;
                if (avahi::discoverDevices(devices) > 0)
                {
                    // Find the one with ipv4 address and return
                    for (std::list<std::shared_ptr<avahi::RDKDevice> >::iterator it = devices.begin(); it != devices.end(); ++it)
                    {
                        std::shared_ptr<avahi::RDKDevice> device = *it;
                        std::cout << "Found IOT Gateway device " << device->ipAddress << ":" << device->port << std::endl;
                        if (device->addrType == avahi::IP_ADDR_TYPE::IPV4)
                        {
                            std::cout << "Found ipv4 device " << device->ipAddress << ":" << device->port << std::endl;
                            return "tcp://" + device->ipAddress + ":" + std::to_string(device->port);
                        }
                    }
                }
                else
                {
                    std::cout << " Failed to identify RDK IoT Gateway." << std::endl;
                }
                avahi::unInitialize();
                std::cout << " Failed to identify IPV4 RDK IoT Gateway." << std::endl;
            }
            else
            {
                std::cout << " Failed to initialize avahi " << std::endl;
            }

            return "";
        }

        SERVICE_REGISTRATION(RIoTControl, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

        RIoTControl::RIoTControl()
            : PluginHost::JSONRPC(), m_apiVersionNumber(API_VERSION_NUMBER_MAJOR), riotConn(nullptr),
              _job(Core::ProxyType<Job>::Create(this)), connectedToRemote(false)
        {
            Register("getAvailableDevices", &RIoTControl::getAvailableDevices, this);
            Register("getDeviceProperties", &RIoTControl::getDeviceProperties, this);
            Register("getDeviceProperty", &RIoTControl::getDeviceProperty, this);
            Register("sendCommand", &RIoTControl::sendCommand, this);
        }

        RIoTControl::~RIoTControl()
        {
        }

        const string RIoTControl::Initialize(PluginHost::IShell * /* service */)
        {
            riotConn = new iotbridge::RIoTConnector();
            Core::IWorkerPool::Instance().Submit(Core::proxy_cast<Core::IDispatchType<void> >(_job));
            return "";
        }
        void RIoTControl::Deinitialize(PluginHost::IShell * /* service */)
        {
            riotConn->cleanupIPC();
            delete riotConn;
            riotConn = nullptr;

            connectedToRemote = false;
            remote_addr = "";
        }
        string RIoTControl::Information() const
        {
            return (string());
        }

        bool RIoTControl::connectToRemote()
        {
            std::cout << "[RIoTControl::connectToRemote] Starting worker thread..." << std::endl;

            if (remote_addr.empty())
            {
                remote_addr = getRemoteAddress();
            }

            if (!remote_addr.empty())
                connectedToRemote = riotConn->initializeIPC(remote_addr);
            std::cout << "[RIoTControl::connectToRemote] completed .Remote address: " << remote_addr.c_str() << std::endl;
            return connectedToRemote;
        }

        // Supported methods
        uint32_t RIoTControl::getAvailableDevices(const JsonObject &parameters, JsonObject &response)
        {
            bool success = false;
            if (connectedToRemote)
            {
                std::list<std::shared_ptr<WPEFramework::iotbridge::IOTDevice> > deviceList;
                JsonArray devArray;
                if (riotConn->getDeviceList(deviceList) > 0)
                {

                    for (const auto &device : deviceList)
                    {
                        JsonObject deviceObj;
                        deviceObj["name"] = device->deviceName;
                        deviceObj["uuid"] = device->deviceId;
                        deviceObj["type"] = device->devType;
                        devArray.Add(deviceObj);
                    }
                }
                response["devices"] = devArray;
                success = true;
            }
            else
            {
                response["message"] = "Failed to connect to IoT Gateway";
            }
            returnResponse(success);
        }

        uint32_t RIoTControl::getDeviceProperties(const JsonObject &parameters, JsonObject &response)
        {
            bool success = false;

            returnIfParamNotFound(parameters, "deviceId");

            if (connectedToRemote)
            {

                std::list<std::string> properties;
                std::string uuid = parameters["deviceId"].String();

                riotConn->getDeviceProperties(uuid, properties);
                std::cout << " Value returned is " << properties.size() << std::endl;

                JsonObject propObj;
                for (std::string const &property : properties)
                {

                    std::stringstream stream(property);
                    std::string key, value;
                    std::getline(stream, key, '=');
                    std::getline(stream, value, '=');
                    propObj[key.c_str()] = value;
                }

                response["properties"] = propObj;

                success = true;
            }
            else
            {
                response["message"] = "Failed to connect to IoT Gateway";
                std::cout << "Failed to connect to IoT Gateway" << std::endl;
            }

            returnResponse(success);
        }
        uint32_t RIoTControl::getDeviceProperty(const JsonObject &parameters, JsonObject &response)
        {
            bool success = false;
            returnIfParamNotFound(parameters, "deviceId");
            returnIfParamNotFound(parameters, "propName");

            if (connectedToRemote)
            {
                std::string uuid = parameters["deviceId"].String();
                std::string prop = parameters["propName"].String();
                std::string propVal = riotConn->getDeviceProperty(uuid, prop);
                response["value"] = propVal;
                success = true;
            }
            else
            {
                response["message"] = "Failed to connect to IoT Gateway";
            }

            returnResponse(success);
        }
        uint32_t RIoTControl::sendCommand(const JsonObject &parameters, JsonObject &response)
        {
            bool success = false;
            returnIfParamNotFound(parameters, "deviceId");
            returnIfParamNotFound(parameters, "command");

            if (connectedToRemote)
            {
                std::string uuid = parameters["deviceId"].String();
                std::string cmd = parameters["command"].String();
                if (0 == riotConn->sendCommand(uuid, cmd))
                    success = true;
            }
            else
            {
                response["message"] = "Failed to connect to IoT Gateway";
            }

            returnResponse(success);
        }

    } // namespace Plugin
} // namespace WPEFramework
