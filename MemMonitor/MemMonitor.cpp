#include "MemMonitor.h"
#include <iostream>
#define SERVER_DETAILS "127.0.0.1:9998"
#define SUBSCRIPTION_CALLSIGN "org.rdk.RDKShell"
#define SUBSCRIPTION_CALLSIGN_VER SUBSCRIPTION_CALLSIGN ".1"
#define SUBSCRIPTION_LOW_MEMORY_EVENT "onDeviceLowRamWarning"
#define SUBSCRIPTION_ONKEY_EVENT "onKeyEvent"
#define SUBSCRIPTION_ONLAUNCHED_EVENT "onLaunched"
#define SUBSCRIPTION_ONDESTROYED_EVENT "onDestroyed"

#define MY_VERSION "1.31"
#define RECONNECTION_TIME_IN_MILLISECONDS 5500
//#define LAUNCH_URL "https://apps.rdkcentral.com/rdk-apps/accelerator-home-ui/index.html#menu"
#define LAUNCH_URL "https://apps.rdkcentral.com/dev/Device_UI_3.6/index.html#menu"
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
            LOGINFO("[Jose] [Received  event], %s : %s Res app running ? %d ", __FUNCTION__, C_STR(message), m_isResAppRunning);

            if (parameters.HasLabel("keycode"))
            {
                if (!parameters["keyDown"].Boolean() && parameters["keycode"].Number() == 36 && !m_isResAppRunning)
                {
                    LOGINFO("[Jose] Hot key ... Need to lauch ?");
                    PluginHost::WorkerPool::Instance().Submit(Job::Create(this, HOTKEY));
                }
            }
            else if (parameters.HasLabel("launchType"))
            {
                // Something got activated ..
                
                activeCallsign = parameters["client"].String();
                LOGINFO("[Jose] Launch notification  ...%s  ",activeCallsign);
                PluginHost::WorkerPool::Instance().Submit(Job::Create(this, LAUNCHED));
            }
            else if (parameters.HasLabel("ram"))
            {
                LOGINFO("[Jose] Low Memory  ... ");
                PluginHost::WorkerPool::Instance().Submit(Job::Create(this, LOWMEMORY));
            }
            else
            {
                string killed = parameters["client"].String();
                LOGINFO("[Jose] %s got destroyed  ... ",killed);
                
                if (!Utils::String::stringContains(killed, "residentapp"))
                    PluginHost::WorkerPool::Instance().Submit(Job::Create(this, DESTROYED));
            }
        }

        void MemMonitor::Dispatch(JOBNUMBER jobType)
        {
            string message, clients;
            JsonObject req, res;
            uint32_t status;

            WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement> client(SUBSCRIPTION_CALLSIGN_VER);
            status = client.Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("getClients"), req, res);
            if (Core::ERROR_NONE == status)
            {
                clients = res["clients"].String();
            }            
            std::cout << "[Jose] activeCallSign : " << activeCallsign << ", clients : " << clients <<
                      "Resident app running : " << m_isResAppRunning << std::endl;

            if (HOTKEY == jobType && (!m_isResAppRunning && !m_launchInitiated))
            {

                if (!Utils::String::stringContains(activeCallsign, "residentapp"))
                {
                    std::cout << "[Jose] Active call sign was " << activeCallsign << std::endl;
                    req["callsign"] = activeCallsign;
                    status = client.Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("destroy"), req, res);
                }
            }
            else if (LOWMEMORY == jobType && m_isResAppRunning )
            {
                req["callsign"] = "ResidentApp";
                status = client.Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("destroy"), req, res);
                res.ToString(message);

                m_callMutex.Lock();
                m_isResAppRunning = !(Core::ERROR_NONE == status);
                m_callMutex.Unlock();
                
                std::cout << "[Jose]  Unloaded residentapp . status : "
                          << (m_isResAppRunning ? "true" : "false")
                          << C_STR(message) << std::endl;
            }
            else if (DESTROYED == jobType)
            {
                if (!m_isResAppRunning && !m_launchInitiated)
                {
                    req["callsign"] = "ResidentApp";
                    req["type"] = "ResidentApp";
                    req["visible"] = true;
                    req["focus"] = true;
                    req["uri"] = LAUNCH_URL;

                    status = client.Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("launch"), req, res);
                    res.ToString(message);
                    m_callMutex.Lock();
                    m_launchInitiated = true;
                    m_callMutex.Unlock();
                    std::cout << "[Jose] Launched residentapp . status :"
                              << m_isResAppRunning << " , msg " << C_STR(message) << std::endl;
                }
            }
            else if (LAUNCHED == jobType)
            {
                if (Utils::String::stringContains(activeCallsign, "residentapp"))
                {
                    m_callMutex.Lock();
                    m_isResAppRunning = true;
                    m_launchInitiated = false;
                    m_callMutex.Unlock();
                }

            }
        }

        SERVICE_REGISTRATION(MemMonitor, 1, 0);

        MemMonitor::MemMonitor() : AbstractPlugin(2),
                                   m_subscribedToEvents(false),
                                   m_remoteObject(nullptr),
                                   m_isResAppRunning(false),
                                   m_launchInitiated(false)
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

            std::cout << "[Jose] version " << MY_VERSION << std::endl;
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
                m_remoteObject->Unsubscribe(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONLAUNCHED_EVENT));
                m_remoteObject->Unsubscribe(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONDESTROYED_EVENT));
                m_subscribedToEvents = false;
                delete m_remoteObject;
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
                SubscribeToEvents();
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
        void MemMonitor::SubscribeToEvents()
        {
            if (Utils::isPluginActivated(SUBSCRIPTION_CALLSIGN))
            {
                uint32_t status = Core::ERROR_NONE;

                LOGINFO("[Jose] Attempting to subscribe for event: %s.%s\n", SUBSCRIPTION_CALLSIGN_VER, SUBSCRIPTION_LOW_MEMORY_EVENT);

                std::string serviceCallsign = "org.rdk.RDKShell";
                serviceCallsign.append(".1");

                status = m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_LOW_MEMORY_EVENT), &MemMonitor::pluginEventHandler, this);
                std::cout << "[Jose] Subscribed to RDKShell " << SUBSCRIPTION_LOW_MEMORY_EVENT << (status == Core::ERROR_NONE ? " Success" : " Failed") << std::endl;

                status = m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONKEY_EVENT), &MemMonitor::pluginEventHandler, this);
                std::cout << "[Jose] Subscribed to <<SUBSCRIPTION_ONKEY_EVENT << events.. " << (status == Core::ERROR_NONE ? "Success" : "Failed") << std::endl;

                status = m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONLAUNCHED_EVENT), &MemMonitor::pluginEventHandler, this);
                std::cout << "[Jose] Subscribed to <<SUBSCRIPTION_ONLAUNCHED_EVENT << events.. " << (status == Core::ERROR_NONE ? "Success" : "Failed") << std::endl;

                status = m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONDESTROYED_EVENT), &MemMonitor::pluginEventHandler, this);
                std::cout << "[Jose] Subscribed to <<SUBSCRIPTION_ONDESTROYED_EVENT << events.. " << (status == Core::ERROR_NONE ? "Success" : "Failed") << std::endl;

                m_subscribedToEvents = true;

                JsonObject req, res;
                status = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, "getClients", req, res);
                if (Core::ERROR_NONE == status)
                {
                    string clients = res["clients"].String();
                    m_isResAppRunning = (clients.find("residentapp") != std::string::npos);
                }
                else
                {
                    std::cout << "[Jose] Failed to invoke getClients." << std::endl;
                }
            }
            else
            {
                std::cout << "[Jose] RDKShell is not yet active. Wait for it.. " << std::endl;
            }
        }
    }
}