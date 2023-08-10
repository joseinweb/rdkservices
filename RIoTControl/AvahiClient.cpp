/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2023 RDK Management
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

#include <thread>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/timeval.h>

#include <avahi-common/thread-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

#include "AvahiClient.h"

namespace avahi
{

    const std::string RDK_SERVICE_NAME = "_rdk._tcp";
    // threaded poll instance
    static AvahiThreadedPoll *thread_poll = nullptr;
    // avahi client instance
    AvahiClient *client = nullptr;
    // avahi service browser instance
    AvahiServiceBrowser *sb = nullptr;

    bool m_initialized(false);
    bool m_scanInProgress(false);
    std::mutex m_stateMutex;
    std::condition_variable m_stateCond;
    std::list<std::shared_ptr<RDKDevice> > deviceList;

    void onDeviceStatusChanged(AvahiClient *ac, AvahiClientState acs, void *udata)
    {
        std::cout << "[onDeviceStatusChanged] Device status changed  -- " << std::this_thread::get_id() << std::endl;
        if (AVAHI_CLIENT_FAILURE == acs)
        {
            // Need to stop polling. Some issues with the client connection.
            std::cout << "[onDeviceStatusChanged] Client connection failure. exiting discovery" << avahi_strerror(avahi_client_errno(ac)) << std::endl;
            stopDiscovery();
        }
        std::cout << "[onDeviceStatusChanged] Exit -- " << std::this_thread::get_id() << std::endl;
    }
    void onServiceResolved(AvahiServiceResolver *r,
                           AvahiIfIndex interface,
                           AvahiProtocol protocol,
                           AvahiResolverEvent event,
                           const char *name,
                           const char *type,
                           const char *domain,
                           const char *host_name,
                           const AvahiAddress *address,
                           uint16_t port,
                           AvahiStringList *txt,
                           AvahiLookupResultFlags flags,
                           void *userdata)
    {
        std::cout << "[onServiceResolved] -- " << std::this_thread::get_id() << std::endl;
        if (AVAHI_RESOLVER_FOUND == event)
        {
            char a[AVAHI_ADDRESS_STR_MAX];

            avahi_address_snprint(a, sizeof(a), address);

            std::shared_ptr<RDKDevice> device(new RDKDevice());
            device->port = port;
            device->deviceName = host_name;
            device->ipAddress = a;
            device->addrType = (address->proto == AVAHI_PROTO_INET ? IPV4 : IPV6);
            deviceList.push_back(device);
        }
        avahi_service_resolver_free(r);
        std::cout << "[onServiceResolved] Exit -- " << std::this_thread::get_id() << std::endl;
    }

    void onServiceDiscovery(AvahiServiceBrowser *asb,
                            AvahiIfIndex aifIndex,
                            AvahiProtocol aprotocol,
                            AvahiBrowserEvent abevent,
                            const char *name,
                            const char *type,
                            const char *domain,
                            AvahiLookupResultFlags flags, void *udata)
    {
        std::cout << "onServiceDiscovery -- " << std::this_thread::get_id() << std::endl;
        switch (abevent)
        {
        case AVAHI_BROWSER_FAILURE:
            std::cout << "onServiceDiscovery AVAHI_BROWSER_FAILURE" << std::endl;
            stopDiscovery();
            break;
        case AVAHI_BROWSER_NEW:
            std::cout << "onServiceDiscovery AVAHI_BROWSER_NEW" << std::endl;
            if (!(avahi_service_resolver_new(client, aifIndex, aprotocol, name, type, domain, AVAHI_PROTO_INET, (AvahiLookupFlags)0, onServiceResolved, nullptr)))
                std::cout << "Failed to register for service resolution of " << name << ", error: " << avahi_strerror(avahi_client_errno(client)) << std::endl;
            break;
        case AVAHI_BROWSER_ALL_FOR_NOW:
            std::cout << "onServiceDiscovery AVAHI_BROWSER_ALL_FOR_NOW" << std::endl;
            break;
        case AVAHI_BROWSER_REMOVE:
            std::cout << "onServiceDiscovery AVAHI_BROWSER_REMOVE called " << std::endl;
            break;
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
            std::cout << "onServiceDiscovery AVAHI_BROWSER_CACHE_EXHAUSTED called " << std::endl;
            break;
        }
        std::cout << "[onServiceDiscovery] Exit -- " << std::this_thread::get_id() << std::endl;
    }

    bool stopDiscovery()
    {
        std::cout << "[stopDiscovery] -- " << std::this_thread::get_id() << std::endl;
        std::lock_guard<std::mutex> lockguard(m_stateMutex);
        m_scanInProgress = false;
        m_stateCond.notify_all();
        std::cout << "[stopDiscovery] Exit -- " << std::this_thread::get_id() << std::endl;
        return true;
    }
    bool initialize(const std::string &serviceName)
    {

        std::cout << "initialize" << std::endl;
        int error = 0;
        /* Allocate main loop object */
        if (!(thread_poll = avahi_threaded_poll_new()))
        {
            std::cout << "Failed to create avahi poll object." << std::endl;
            return m_initialized;
        }
        struct timeval tv;
        const AvahiPoll *pollObj = avahi_threaded_poll_get(thread_poll);

        avahi_elapse_time(&tv, DD_TIMEOUT_MILLIS, 0);

        pollObj->timeout_new(
            pollObj, &tv, [](AvahiTimeout *timeout, void *userdata)
            {
            std::cout << "[initialize]  timeout reached -- " << std::this_thread::get_id() << std::endl;
            m_scanInProgress = false;
            avahi_threaded_poll_quit(thread_poll); 
            std::cout << "[initialize]  timeout thread exit -- " << std::this_thread::get_id() << std::endl;},
            nullptr);

        /* Allocate a new client */
        client = avahi_client_new(avahi_threaded_poll_get(thread_poll), (AvahiClientFlags)0,
                                  onDeviceStatusChanged, NULL, &error);

        /* Check wether creating the client object succeeded */
        if (!client)
        {
            std::cout << "Failed to create avahi client instance." << avahi_strerror(error) << std::endl;
            return unInitialize();
        }
        /* Create the service browser */
        if (!(sb = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, serviceName.c_str(), NULL, (AvahiLookupFlags)0, onServiceDiscovery, client)))
        {
            std::cout << "Failed to create service browser: " << avahi_strerror(avahi_client_errno(client)) << std::endl;
            return unInitialize();
        }
        m_initialized = true;
        std::cout << "[initialize] Exit -- " << std::this_thread::get_id() << std::endl;
        return m_initialized;
    }
    int discoverDevices(std::list<std::shared_ptr<RDKDevice> > &devices, int timeoutMillis)
    {

        std::cout << "[discoverDevices] discoverDevices" << std::endl;
        // Let us clear existing deviceList;
        if (m_scanInProgress)
            return DD_SCAN_ALREADY_IN_PROGRESS;

        deviceList.clear();
        // Let us put the scan in progress.
        m_scanInProgress = true;
        std::cout << "[discoverDevices] Starting scanning" << std::endl;

        avahi_threaded_poll_start(thread_poll);

        std::unique_lock<std::mutex> lock(m_stateMutex);
        auto startTime = std::chrono::steady_clock::now();
        auto endTime = startTime + std::chrono::milliseconds(timeoutMillis);

        while (std::chrono::steady_clock::now() < endTime)
        {

            // Got a device or error occured?
            if (!m_scanInProgress)
            {
                std::cout << "[discoverDevices] Got devices. breaking.." << std::this_thread::get_id() << std::endl;
                break;
            }

            auto remainingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - std::chrono::steady_clock::now());
            if (remainingTime.count() <= 0)
                break;

            m_stateCond.wait_for(lock, remainingTime);
        }
        std::cout << "[discoverDevices] Total devices found " << deviceList.size() << std::endl;
        for (std::list<std::shared_ptr<RDKDevice> >::iterator it = deviceList.begin(); it != deviceList.end(); ++it)
            devices.push_back(*it);
        std::cout << "[discoverDevices] Exit -- " << std::this_thread::get_id() << std::endl;    
        return deviceList.size();
    }
    bool unInitialize()
    {
        std::cout << "[unInitialize] " << std::endl;
        if (m_scanInProgress)
        {
            stopDiscovery();
        }
        // clear any created objects
        if (sb)
            avahi_service_browser_free(sb);
        if (client)
            avahi_client_free(client);
        if (thread_poll)
            avahi_threaded_poll_stop(thread_poll);

        m_initialized = false;
        sb = nullptr;
        client = nullptr;
        thread_poll = nullptr;
       std::cout << "[unInitialize] Exit -- " << std::this_thread::get_id() << std::endl;
        return m_initialized;
    }
}
