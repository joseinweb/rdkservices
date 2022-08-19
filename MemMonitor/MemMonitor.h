/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
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
 */

#pragma once
#include <mutex>
#include "Module.h"
#include "utils.h"
#include "AbstractPlugin.h"
#include "tptimer.h"
#define HOME_KEY 36
namespace WPEFramework
{
    namespace Plugin
    {
        enum JOBTYPE
        {
            LAUNCH,
            OFFLOAD,
            KEEP_ACTIVE_APP,
            REMOVE_ACTIVE_APP,
            RESTORE_RES_APP
        };

        class MemMonitor : public AbstractPlugin
        {
            class Config : public Core::JSON::Container
            {
            private:
                Config(const Config &) = delete;
                Config &operator=(const Config &) = delete;

            public:
                Config() : Core::JSON::Container(), Homeurl(), Callsigns()
                {
                    Add(_T("homeurl"), &Homeurl);
                    Add(_T("callsigns"), &Callsigns);
                    Add(_T("lowmem"), &Lowmem);
                    Add(_T("criticalmem"),&Criticalmem);
                }
                ~Config()
                {
                }
                Core::JSON::String Homeurl;
                Core::JSON::DecUInt16 Lowmem, Criticalmem;

                Core::JSON::ArrayType<Core::JSON::String> Callsigns;
            } configurations;

            class EXTERNAL Job : public Core::IDispatch
            {
            public:
                Job(MemMonitor *monitor, JOBTYPE _jobType)
                    : _monitor(monitor), jobType(_jobType)
                {

                    ASSERT(_monitor != nulllptr);
                }
                virtual ~Job()
                {
                }

            private:
                Job() = delete;
                Job(const Job &) = delete;
                Job &operator=(const Job &) = delete;

            public:
                static Core::ProxyType<Core::IDispatch> Create(MemMonitor *mon, JOBTYPE jobType)
                {
                    return (Core::proxy_cast<Core::IDispatch>(Core::ProxyType<Job>::Create(mon, jobType)));
                }
                virtual void Dispatch()
                {
                    _monitor->Dispatch(jobType);
                }

            private:
                MemMonitor *_monitor;
                JOBTYPE jobType;
            };

            MemMonitor(const MemMonitor &) = delete;
            MemMonitor &operator=(const MemMonitor &) = delete;

            void Dispatch(JOBTYPE keycode);

            // Event handling...
            void SubscribeToEvents();
            void onLowMemoryEvent(const JsonObject &parameters);
            void onCriticalMemoryEvent(const JsonObject &parameters);
            void onSuspended(const JsonObject &parameters);
            void onDestroyed(const JsonObject &parameters);
            void onLaunched(const JsonObject &parameters);
            void onKeyEvent(const JsonObject &parameters);

            // Give some breathing space for apps
            void setMemoryLimits();

            void offloadApplication(const string callsign);
            void updateState(bool running, bool started);

            bool m_subscribedToEvents;
            void onTimer();
            void launchResidentApp();

            TpTimer m_timer;
            mutable Core::CriticalSection m_callMutex;
            WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement> *m_remoteObject;
            volatile bool m_isResAppRunning, m_launchInitiated, m_onHomeScreen;

            string activeCallsign;
            string m_homeURL;
            int m_lowMem, m_criticalMem;
            std::list<string> callsigns;

        public:
            MemMonitor();
            virtual ~MemMonitor();
            virtual const string Initialize(PluginHost::IShell *service) override;
            virtual void Deinitialize(PluginHost::IShell *service) override;
            virtual string Information() const override;
        };
    }
}