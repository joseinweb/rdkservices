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
        class MemMonitor : public AbstractPlugin
        {
        public:
            MemMonitor();
            virtual ~MemMonitor();
            virtual const string Initialize(PluginHost::IShell *service) override;
            virtual void Deinitialize(PluginHost::IShell *service) override;
            virtual string Information() const override;

             static MemMonitor* _instance;

            

        private:
            MemMonitor(const MemMonitor &) = delete;
            MemMonitor &operator=(const MemMonitor &) = delete;
            
            void SubscribeForMemoryEvents();
            void pluginEventHandler(const JsonObject &parameters);
            bool SubscribedToEvents;
            bool interceptEnabled;
            void onTimer();

            TpTimer m_timer;
            std::mutex m_callMutex;            
        };
    }
}