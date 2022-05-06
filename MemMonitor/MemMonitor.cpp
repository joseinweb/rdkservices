#include "MemMonitor.h"
#include <iostream>
#define SERVER_DETAILS "127.0.0.1:9998"
#define SUBSCRIPTION_CALLSIGN "org.rdk.RDKShell"
#define SUBSCRIPTION_CALLSIGN_VER SUBSCRIPTION_CALLSIGN ".1"

#define SUBSCRIPTION_LOW_MEMORY_EVENT "onDeviceCriticallyLowRamWarning"
#define SUBSCRIPTION_ONKEY_EVENT "onKeyEvent"
#define SUBSCRIPTION_ONLAUNCHED_EVENT "onLaunched"
#define SUBSCRIPTION_ONDESTROYED_EVENT "onDestroyed"

#define MY_VERSION "1.37b"
#define RECONNECTION_TIME_IN_MILLISECONDS 5500
//#define LAUNCH_URL "https://apps.rdkcentral.com/rdk-apps/accelerator-home-ui/index.html#menu"
//#define LAUNCH_URL "https://apps.rdkcentral.com/dev/Device_UI_3.6/index.html#menu"
#define LAUNCH_URL "http://127.0.0.1:50050/lxresui/index.html#menu"
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
            LOGINFO("[Jose] [Low memory  event], %s : %s Res app running ? %d ", __FUNCTION__, C_STR(message), m_isResAppRunning);

            if (parameters.HasLabel("ram"))
            {
                PluginHost::WorkerPool::Instance().Submit(Job::Create(this, OFFLOAD));
            }
        }
        void MemMonitor::onKeyEvent(const JsonObject &parameters)
        {
            string message, clients;
            if (parameters.HasLabel("keycode"))
            {
                if (!parameters["keyDown"].Boolean() && parameters["keycode"].Number() == 36)
                {
                    // Is there an active app ?
                    if (!activeCallsign.empty())
                    {
                        // destroy active app.
                        PluginHost::WorkerPool::Instance().Submit(Job::Create(this, REMOVE_ACTIVE_APP));
                    }
                    else
                    {
                        if (!m_isResAppRunning && !m_launchInitiated)
                        {
                            LOGINFO("[Jose] Hot key ... Launching resident app ");
                            PluginHost::WorkerPool::Instance().Submit(Job::Create(this, LAUNCH));
                        }
                        else
                        {
                            LOGINFO("[Jose] Hot key ... Resident app status launched: %d, islaunching : %d",
                                    m_isResAppRunning, m_launchInitiated);
                        }
                    }
                }
            }
        }
        void MemMonitor::onLaunched(const JsonObject &parameters)
        {
            if (parameters.HasLabel("client"))
            {
                activeCallsign = parameters["client"].String();
                LOGINFO("[Jose] Launch notification  ...%s  ", C_STR(activeCallsign));

                if (Utils::String::stringContains(activeCallsign, "residentapp"))
                {
                    m_callMutex.Lock();
                    m_isResAppRunning = true;
                    m_launchInitiated = false;
                    m_callMutex.Unlock();
                }
            }
        }
        void MemMonitor::onDestroyed(const JsonObject &parameters)
        {
            // Case 1.Focused app is not referenceapp.
            LOGINFO("[Jose] m_isResAppRunning =%d m_launchInitiated = %d ", m_isResAppRunning, m_launchInitiated);
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
                    m_callMutex.Lock();
                    m_isResAppRunning = false;
                    m_launchInitiated = false;
                    m_callMutex.Unlock();
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
            case LAUNCH:
                launchResidentApp();
                break;
            case OFFLOAD:
                req["callsign"] = "ResidentApp";
                status = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("destroy"), req, res);
                LOGINFO("destroyed residentApp, status : %d", (Core::ERROR_NONE == status));
                break;
            case REMOVE_ACTIVE_APP:
                req["callsign"] = activeCallsign.c_str();
                status = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("destroy"), req, res);
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
            req["uri"] = LAUNCH_URL;

            status = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("launch"), req, res);
            res.ToString(message);
            m_callMutex.Lock();
            m_launchInitiated = true;
            m_callMutex.Unlock();
            LOGINFO("[Jose] Launched residentapp . status : %d,  msg %s ",
                    (status == Core::ERROR_NONE), C_STR(message));
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

            LOGINFO("[Jose] version %s ", MY_VERSION);
            m_timer.start(RECONNECTION_TIME_IN_MILLISECONDS);
            Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), _T(SERVER_DETAILS));
            m_remoteObject = new WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement>(_T(SUBSCRIPTION_CALLSIGN_VER));
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
            LOGINFO("[Jose] Attempting event subscription");

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
                    LOGINFO("[Jose] Failed to invoke getClients.");
                }
            }
            else
            {
                LOGINFO("[Jose] RDKShell is not yet active. Wait for it.. ");
            }
        }
    }
}