#include "MemMonitor.h"
#include <iostream>
#define SERVER_DETAILS "127.0.0.1:9998"
#define SUBSCRIPTION_CALLSIGN "org.rdk.RDKShell"
#define SUBSCRIPTION_CALLSIGN_VER SUBSCRIPTION_CALLSIGN ".1"

#define SUBSCRIPTION_LOW_MEMORY_EVENT "onDeviceCriticallyLowRamWarning"
#define SUBSCRIPTION_ONKEY_EVENT "onKeyEvent"
#define SUBSCRIPTION_ONLAUNCHED_EVENT "onLaunched"
#define SUBSCRIPTION_ONDESTROYED_EVENT "onDestroyed"

#define REVISION "1.40f"
#define RECONNECTION_TIME_IN_MILLISECONDS 5500
#define LAUNCH_URL "https://apps.rdkcentral.com/rdk-apps/accelerator-home-ui/index.html#menu"
#define THUNDER_TIMEOUT 2000

namespace WPEFramework
{
    namespace Plugin
    {
        static std::string gThunderAccessValue = SERVER_DETAILS;
        static std::string sThunderSecurityToken;
        void MemMonitor::onLowMemoryEvent(const JsonObject &parameters)
        {
            string message;
            parameters.ToString(message);
            LOGINFO(" [Low memory  event], %s : %s Res app running ? %d ", __FUNCTION__, C_STR(message), m_isResAppRunning);

            if (parameters.HasLabel("ram") && m_isResAppRunning)
            {
                PluginHost::WorkerPool::Instance().Submit(Job::Create(this, OFFLOAD));
            }
        }
        void MemMonitor::onKeyEvent(const JsonObject &parameters)
        {
            string message, clients;
            if (parameters.HasLabel("keycode"))
            {
                if (!parameters["keyDown"].Boolean() && parameters["keycode"].Number() == HOME_KEY)
                {
                    // Case 1. Is there an active app and not is not residentapp
                    bool isResActiveApp = Utils::String::stringContains(activeCallsign, "residentapp");
                    if (!activeCallsign.empty() && !isResActiveApp)
                    {
                        if (!m_isResAppRunning && !m_launchInitiated)
                        {
                            LOGINFO(" Hot key ... Launching resident app ");
                            PluginHost::WorkerPool::Instance().Submit(Job::Create(this, RESTORE_RES_APP));
                            activeCallsign = "ResidentApp";
                        }
                    }
                    else
                        LOGINFO(" [onKeyEvent] Case 1 is not applicable");
                    // Case 2. There is no active app and resident app is not launched.
                    if (activeCallsign.empty())
                    {
                        PluginHost::WorkerPool::Instance().Submit(Job::Create(this, LAUNCH));
                        activeCallsign = "ResidentApp";
                    }
                    else
                        LOGINFO(" [onKeyEvent] Case 2 is not applicable");
                }
            }
        }
        void MemMonitor::onLaunched(const JsonObject &parameters)
        {
            if (parameters.HasLabel("client"))
            {
                activeCallsign = parameters["client"].String();
                LOGINFO(" Launch notification  ...%s  ", C_STR(activeCallsign));

                if (Utils::String::stringContains(activeCallsign, "residentapp"))
                    updateState(true,false);
            }
        }
        void MemMonitor::updateState(bool running, bool started)
        {
                    m_callMutex.Lock();
                    m_isResAppRunning = running;
                    m_launchInitiated = started;
                    m_callMutex.Unlock();
        }
        void MemMonitor::onDestroyed(const JsonObject &parameters)
        {
            // Case 1.Focused app is not referenceapp.
            LOGINFO(" m_isResAppRunning =%d m_launchInitiated = %d ", m_isResAppRunning, m_launchInitiated);
            if (parameters.HasLabel("client"))
            {
                string destroyedApp = parameters["client"].String();
                if (!Utils::String::stringContains(destroyedApp, "residentapp") &&
                    !m_isResAppRunning && !m_launchInitiated)
                {
                    launchResidentApp();
                }
                else if (Utils::String::stringContains(destroyedApp, "residentapp"))
                {
                    updateState(false,false);
                    m_onHomeScreen = false;
                }
            }
        }

        void MemMonitor::Dispatch(JOBTYPE jobType)
        {
            string message, clients;
            JsonObject req, res;
            uint32_t status;

            switch (jobType)
            {
            case RESTORE_RES_APP:
            {
                string currApp = activeCallsign;
                LOGINFO(" [Dispatch] Restoring ResidentApp..");
                m_onHomeScreen = true;
                launchResidentApp();
                LOGINFO(" [Dispatch] Offloading active app...");
                offloadApplication(currApp);
            }
            break;
            case REMOVE_ACTIVE_APP:
                LOGINFO(" [Dispatch] Removing active app");
                offloadApplication(activeCallsign);
                break;
            case LAUNCH:
                launchResidentApp();
                break;
            case OFFLOAD:
                if (!m_onHomeScreen)
                {
                    offloadApplication("ResidentApp");
                }
                else
                {
                    LOGINFO("Skpping residentUI offloading. :");
                }
                break;
            }
        }
        void MemMonitor::launchResidentApp()
        {
            JsonObject req, res;
            uint32_t status;
            string message;

            req["callsign"] = "ResidentApp";
            req["type"] = "ResidentApp";
            req["visible"] = true;
            req["focus"] = true;
            req["uri"] = homeURL;

            status = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("launch"), req, res);
            res.ToString(message);
            m_callMutex.Lock();
            m_launchInitiated = true;
            m_callMutex.Unlock();
            LOGINFO(" Launched residentapp . status : %d,  msg %s ",
                    (status == Core::ERROR_NONE), C_STR(message));
        }

        SERVICE_REGISTRATION(MemMonitor, 1, 0);

        MemMonitor::MemMonitor() : AbstractPlugin(2),
                                   m_subscribedToEvents(false),
                                   m_remoteObject(nullptr),
                                   m_isResAppRunning(false),
                                   m_launchInitiated(false),
                                   m_onHomeScreen(false)
        {
            LOGINFO();
            m_timer.connect(std::bind(&MemMonitor::onTimer, this));
        }
        MemMonitor::~MemMonitor()
        {
        }

        void MemMonitor::offloadApplication(const string callsign)
        {
            JsonObject req, res;
            uint32_t status;

            req["callsign"] = callsign.c_str();
            status = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("destroy"), req, res);
            LOGINFO("Offloading  %s  result : %s", C_STR(callsign), (Core::ERROR_NONE == status) ? "Success" : "Failure");
        }
        const string MemMonitor::Initialize(PluginHost::IShell *service)
        {
            LOGINFO();

            LOGINFO(" Revision %s ", REVISION);
            m_timer.start(RECONNECTION_TIME_IN_MILLISECONDS);
            Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), _T(SERVER_DETAILS));
            m_remoteObject = new WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>(_T(SUBSCRIPTION_CALLSIGN_VER));

            configurations.FromString(service->ConfigLine());

            if (configurations.Homeurl.IsSet())
            {
                homeURL = configurations.Homeurl.Value();
            }
            else
            {
                LOGINFO("Home URL is not found in config");
                homeURL = LAUNCH_URL;
            }

            LOGINFO("Home URL is set to [%s] .", C_STR(homeURL));

            if (configurations.Callsigns.IsSet() && configurations.Callsigns.Length() > 1)
            {
                Core::JSON::ArrayType<Core::JSON::String>::Iterator index(configurations.Callsigns.Elements());
                while (index.Next())
                {
                    callsigns.push_back(index.Current().Value());
                }
                LOGINFO("Total callsign length is %d", configurations.Callsigns.Length());
            }
            else
            {
                LOGINFO("Callsigns is not found in configuration. Adding cobalt to the list.");
                callsigns.push_back("Cobalt");
            }

            return "";
        }

        void MemMonitor::Deinitialize(PluginHost::IShell * /* service */)
        {

            LOGINFO();
            if (m_subscribedToEvents)
            {
                m_remoteObject->Unsubscribe(THUNDER_TIMEOUT, _T(SUBSCRIPTION_LOW_MEMORY_EVENT));
                m_remoteObject->Unsubscribe(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONKEY_EVENT));
                m_remoteObject->Unsubscribe(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONLAUNCHED_EVENT));
                m_remoteObject->Unsubscribe(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONDESTROYED_EVENT));
                m_subscribedToEvents = false;
                // delete m_remoteObject;
            }
            if (m_timer.isActive())
            {
                m_timer.stop();
            }
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
                setMemoryLimits();
                if (m_timer.isActive())
                {
                    m_timer.stop();
                    LOGINFO(" Timer stopped.");
                }
                LOGINFO("Subscription completed.");
            }
            m_callMutex.Unlock();
        }
        void MemMonitor::SubscribeToEvents()
        {
            LOGINFO(" Attempting event subscription");

            if (Utils::isPluginActivated(SUBSCRIPTION_CALLSIGN))
            {
                uint32_t status = Core::ERROR_NONE;

                std::string serviceCallsign = "org.rdk.RDKShell.1";

                m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_LOW_MEMORY_EVENT), &MemMonitor::onLowMemoryEvent, this);
                m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONKEY_EVENT), &MemMonitor::onKeyEvent, this);
                m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONLAUNCHED_EVENT), &MemMonitor::onLaunched, this);
                m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONDESTROYED_EVENT), &MemMonitor::onDestroyed, this);

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
                    LOGINFO(" Failed to invoke getClients.");
                }
            }
            else
            {
                LOGINFO(" RDKShell is not yet active. Wait for it.. ");
            }
        }
        void MemMonitor::setMemoryLimits()
        {
            JsonObject req, res;
            uint32_t status = Core::ERROR_NONE;

            req["enable"] = true;
            req["lowRam"] = 150;
            req["criticallyLowRam"] = 75;
            status = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, "setMemoryMonitor", req, res);
            if (Core::ERROR_NONE == status)
            {
                LOGINFO(" Memory limits are set at 150 MB and 75M respectively.. ");
            }
        }
    }

}