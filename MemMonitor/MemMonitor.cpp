#include "MemMonitor.h"
#include <iostream>
#define SERVER_DETAILS "127.0.0.1:9998"
#define SUBSCRIPTION_CALLSIGN "org.rdk.RDKShell"
#define SUBSCRIPTION_CALLSIGN_VER SUBSCRIPTION_CALLSIGN ".1"
#define SUBSCRIPTION_EVENT "onDeviceLowRamWarning"
#define RECONNECTION_TIME_IN_MILLISECONDS 5500

#define THUNDER_TIMEOUT 2000

namespace WPEFramework
{
    namespace Plugin
    {
        static std::string gThunderAccessValue = SERVER_DETAILS;
        MemMonitor *MemMonitor::_instance = nullptr;
        static std::string sThunderSecurityToken;
        std::shared_ptr<WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>> rdkshell_ptr;
        void MemMonitor::pluginEventHandler(const JsonObject &parameters)
        {
            string message;
            parameters.ToString(message);
            LOGINFO("[Jose] [%s event], %s : %s", SUBSCRIPTION_EVENT, __FUNCTION__, C_STR(message));            
            if (parameters.HasLabel("ram"))
            {
                long ram = std::stol(parameters["ram"].String());
                std::cout << "[Jose] Free memory available :  "<< ram << std::endl;
            }
        }

        SERVICE_REGISTRATION(MemMonitor, 1, 0);

        MemMonitor::MemMonitor() : AbstractPlugin(2), 
        SubscribedToEvents(false), interceptEnabled(false)
        {
            LOGINFO("ctor");
            m_timer.connect(std::bind(&MemMonitor::onTimer, this));
            if (m_timer.isActive())
            {
                m_timer.stop();
            }
            MemMonitor::_instance = this;
        }
        MemMonitor::~MemMonitor()
        {
            LOGINFO("dtor");
            if (SubscribedToEvents)
            {
                std::string eventName("onDeviceLowRamWarning");
                Utils::getThunderControllerClient(SUBSCRIPTION_CALLSIGN_VER)->Unsubscribe(THUNDER_TIMEOUT, _T("onDeviceLowRamWarning"));

                SubscribedToEvents = false;
            }
        }
        const string MemMonitor::Initialize(PluginHost::IShell * /* service */)
        {
            std::cout << "[Jose] version 1.12 " << std::endl;
            m_timer.start(RECONNECTION_TIME_IN_MILLISECONDS);
            return "";
        }

        void MemMonitor::Deinitialize(PluginHost::IShell * /* service */)
        {
            MemMonitor::_instance = nullptr;
        }

        string MemMonitor::Information() const
        {
            return (string("{\"service \": \"org.rdk.MemMonitor\"}"));
        }
        void MemMonitor::onTimer()
        {
            std::lock_guard<std::mutex> guard(m_callMutex);
            if (!SubscribedToEvents)
            {
                SubscribeForMemoryEvents();
            }
            if (SubscribedToEvents)
            {
                if (m_timer.isActive())
                {
                    m_timer.stop();
                    LOGINFO("[Jose] Timer stopped.");
                }
                LOGINFO("[Jose]Subscription completed.");
            }
        }
        void MemMonitor::SubscribeForMemoryEvents()
        {
            if (Utils::isPluginActivated(SUBSCRIPTION_CALLSIGN))
            {
                uint32_t status = Core::ERROR_NONE;

                LOGINFO("[Jose] Attempting to subscribe for event: %s.%s\n", SUBSCRIPTION_CALLSIGN_VER, SUBSCRIPTION_EVENT);

                std::string serviceCallsign = "org.rdk.RDKShell";
                serviceCallsign.append(".1");
                if (nullptr == rdkshell_ptr)
                  rdkshell_ptr  = std::make_shared<WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>>(serviceCallsign.c_str(), nullptr, false);

                status = rdkshell_ptr->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_EVENT), &MemMonitor::pluginEventHandler, this);
                std::cout << "[Jose] Subscribed to RDKShell memory events.. " << (status == Core::ERROR_NONE ? "Success" : "Failed") << std::endl;
                SubscribedToEvents = true;

                JsonObject req, res;
                status = rdkshell_ptr->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, "getScreenResolution", req, res);
                if (Core::ERROR_NONE == status)
                {
                    if (res.HasLabel("w") && res.HasLabel("h"))
                    {
                        std::cout << "[Jose] screenWidth = " << std::stoi(res["w"].String()) << " screenHeight = " << std::stoi(res["h"].String()) << std::endl;
                    }
                }
                else
                {
                    std::cout << "[Jose] Failed to invoke getResolution." << std::endl;
                }
            }
            else
            {
                std::cout << "[Jose] RDKShell is not yet active. Wait for it.. " << std::endl;
            }
        }
    }
}