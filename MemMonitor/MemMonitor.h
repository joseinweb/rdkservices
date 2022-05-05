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

namespace WPEFramework
{
    namespace Plugin
    {
        enum JOBNUMBER
        {
            HOTKEY,
            LAUNCHED,
            DESTROYED,
            LOWMEMORY
        };

        class MemMonitor : public AbstractPlugin
        {

            class EXTERNAL Job : public Core::IDispatch
            {
            public:
                Job(MemMonitor *monitor, JOBNUMBER keyCode)
                    : _monitor(monitor), keycode(keyCode)
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
                static Core::ProxyType<Core::IDispatch> Create(MemMonitor *mon, JOBNUMBER keycode)
                {
                    return (Core::proxy_cast<Core::IDispatch>(Core::ProxyType<Job>::Create(mon, keycode)));
                }
                virtual void Dispatch()
                {
                    _monitor->Dispatch(keycode);
                }

            private:
                MemMonitor *_monitor;
                JOBNUMBER keycode;
            };

            MemMonitor(const MemMonitor &) = delete;
            MemMonitor &operator=(const MemMonitor &) = delete;

            void Dispatch(JOBNUMBER keycode);

            void SubscribeToEvents();
            void pluginEventHandler(const JsonObject &parameters);
            void onSuspended(const JsonObject &parameters);
            void onDestroyed(const JsonObject &parameters);
            bool m_subscribedToEvents;
            void onTimer();
            void launchResidentApp();

            TpTimer m_timer;
            mutable Core::CriticalSection m_callMutex;
            WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement> *m_remoteObject;
            volatile bool m_isResAppRunning, m_launchInitiated,m_suspendInitiated;
            string activeCallsign;

        public:
            MemMonitor();
            virtual ~MemMonitor();
            virtual const string Initialize(PluginHost::IShell *service) override;
            virtual void Deinitialize(PluginHost::IShell *service) override;
            virtual string Information() const override;
        };
    }
}