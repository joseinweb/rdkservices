#include "MemMonitor.h"
#include <iostream>
#define SERVER_DETAILS "127.0.0.1:9998"
#define SUBSCRIPTION_CALLSIGN "org.rdk.RDKShell"
#define SUBSCRIPTION_CALLSIGN_VER SUBSCRIPTION_CALLSIGN ".1"
#define SUBSCRIPTION_LOW_MEMORY_EVENT "onDeviceLowRamWarning"
#define SUBSCRIPTION_ONKEY_EVENT "onKeyEvent"
#define RECONNECTION_TIME_IN_MILLISECONDS 5500

#define THUNDER_TIMEOUT 2000

namespace WPEFramework
{
    namespace Plugin
    {
        static std::string gThunderAccessValue = SERVER_DETAILS;
        static std::string sThunderSecurityToken;
        void MemMonitor::pluginEventHandler(const JsonObject &parameters)
        {
            string message;
            parameters.ToString(message);
            LOGINFO("[Jose] [Received  event], %s : %s", __FUNCTION__, C_STR(message));
            if (parameters.HasLabel("keycode") && !parameters["keyDown"].Boolean())
            {
                PluginHost::WorkerPool::Instance().Submit(Job::Create(this,
                                                                      static_cast<uint32_t>(parameters["keycode"].Number())));
                // Need to use  worker pool Job in real life.
            }
        }

        void MemMonitor::Dispatch(uint32_t keycode)
        {
            JsonObject req, res;
            std::cout << "I got dispatched " << std::endl;
           m_callMutex.Lock();
            std::cout << "Locked in.. " << std::endl;

            uint32_t status = Utils::getThunderControllerClient(_T(SUBSCRIPTION_CALLSIGN_VER))->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, "getClients", req, res);
            std::cout << "Invocation complete!" << std::endl;
            if (Core::ERROR_NONE == status)
            {
                string message;
                res.ToString(message);
                std::cout << "[Jose] " << __FUNCTION__ << " [Client List]" << C_STR(message) << std::endl;
            }
            else
            {
                std::cout << "[Jose] Failed to invoke getClients method " << std::endl;
            }
             m_callMutex.Unlock();
        }

        SERVICE_REGISTRATION(MemMonitor, 1, 0);

        MemMonitor::MemMonitor() : AbstractPlugin(2),
                                   m_subscribedToEvents(false), m_interceptEnabled(false),
                                   m_remoteObject(nullptr)
        {
            LOGINFO();
            m_timer.connect(std::bind(&MemMonitor::onTimer, this));
        }
        MemMonitor::~MemMonitor()
        {
        }
        const string MemMonitor::Initialize(PluginHost::IShell * /* service */)
        {
            LOGINFO();

            std::cout << "[Jose] version 1.20 " << std::endl;
            m_timer.start(RECONNECTION_TIME_IN_MILLISECONDS);
            Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), _T(SERVER_DETAILS));
            m_remoteObject = new WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>(_T(SUBSCRIPTION_CALLSIGN_VER));
            return "";
        }

        void MemMonitor::Deinitialize(PluginHost::IShell * /* service */)
        {

            LOGINFO();
            m_callMutex.Lock();
            if (m_subscribedToEvents)
            {
                m_remoteObject->Unsubscribe(THUNDER_TIMEOUT, _T(SUBSCRIPTION_LOW_MEMORY_EVENT));
                m_remoteObject->Unsubscribe(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONKEY_EVENT));
                m_subscribedToEvents = false;
                // delete m_remoteObject;
            }
            if (m_timer.isActive())
            {
                m_timer.stop();
            }
             m_callMutex.Unlock();
        }

        string MemMonitor::Information() const
        {
            return (string("{\"service \": \"org.rdk.MemMonitor\"}"));
        }
        void MemMonitor::onTimer()
        {
             m_callMutex.Lock();
            if (!m_subscribedToEvents)
            {
                SubscribeForMemoryEvents();
            }
            if (m_subscribedToEvents)
            {
                if (m_timer.isActive())
                {
                    m_timer.stop();
                    LOGINFO("[Jose] Timer stopped.");
                }
                LOGINFO("[Jose]Subscription completed.");
            }
             m_callMutex.Unlock();
        }
        void MemMonitor::SubscribeForMemoryEvents()
        {
            if (Utils::isPluginActivated(SUBSCRIPTION_CALLSIGN))
            {
                uint32_t status = Core::ERROR_NONE;

                LOGINFO("[Jose] Attempting to subscribe for event: %s.%s\n", SUBSCRIPTION_CALLSIGN_VER, SUBSCRIPTION_LOW_MEMORY_EVENT);

                std::string serviceCallsign = "org.rdk.RDKShell";
                serviceCallsign.append(".1");

                status = m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_LOW_MEMORY_EVENT), &MemMonitor::pluginEventHandler, this);
                std::cout << "[Jose] Subscribed to RDKShell " << SUBSCRIPTION_LOW_MEMORY_EVENT << (status == Core::ERROR_NONE ? "Success" : "Failed") << std::endl;
                m_subscribedToEvents = true;

                status = m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONKEY_EVENT), &MemMonitor::pluginEventHandler, this);
                std::cout << "[Jose] Subscribed to <<SUBSCRIPTION_ONKEY_EVENT << events.. " << (status == Core::ERROR_NONE ? "Success" : "Failed") << std::endl;

                JsonObject req, res;
                status = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, "getScreenResolution", req, res);
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