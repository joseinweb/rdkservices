#include "MemMonitor.h"
#include <iostream>
#define SERVER_DETAILS "127.0.0.1:9998"
#define SUBSCRIPTION_CALLSIGN "org.rdk.RDKShell"
#define SUBSCRIPTION_CALLSIGN_VER SUBSCRIPTION_CALLSIGN ".1"
#define SUBSCRIPTION_LOW_MEMORY_EVENT "onDeviceLowRamWarning"
#define SUBSCRIPTION_ONKEY_EVENT "onKeyEvent"
#define SUBSCRIPTION_ONLAUNCHED_EVENT "onLaunched"
#define SUBSCRIPTION_ONDESTROYED_EVENT "onDestroyed"

#define MY_VERSION "1.34"
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
                LOGINFO("[Jose] Launch notification  ...%s  ", activeCallsign);
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
                LOGINFO("[Jose] %s got destroyed  ... ", killed);

                if (!Utils::String::stringContains(killed, "residentapp"))
                    PluginHost::WorkerPool::Instance().Submit(Job::Create(this, DESTROYED));
            }
        }

        void MemMonitor::Dispatch(JOBNUMBER jobType)
        {
            string message, clients;
            JsonObject req, res;
            uint32_t status;

            status = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("getClients"), req, res);
            if (Core::ERROR_NONE == status)
            {
                clients = res["clients"].String();
            }
            LOGINFO("[Jose] activeCallSign : %s, clients : %s, Resident app status : %d",
                    activeCallsign, clients, m_isResAppRunning);

            if (HOTKEY == jobType && (!m_isResAppRunning && !m_launchInitiated))
            {

                if (!activeCallsign.empty() && !Utils::String::stringContains(activeCallsign, "residentapp"))
                {
                    LOGINFO("[Jose] Active call sign was %s", activeCallsign);
                    req["callsign"] = activeCallsign;
                    status = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("destroy"), req, res);
                }
                else
                {
                    launchResidentApp();
                }
            }
            else if (LOWMEMORY == jobType && m_isResAppRunning)
            {

                req["callsign"] = "ResidentApp";
                status = m_remoteObject->Invoke<JsonObject, JsonObject>(THUNDER_TIMEOUT, _T("destroy"), req, res);
                res.ToString(message);

                m_callMutex.Lock();
                m_isResAppRunning = !(Core::ERROR_NONE == status);
                m_callMutex.Unlock();

                LOGINFO("[Jose]  Unloaded residentapp . status : %d, apps: ",
                        (m_isResAppRunning ? "true" : "false"),
                        C_STR(message));
            }
            else if (DESTROYED == jobType)
            {
                if (!m_isResAppRunning && !m_launchInitiated)
                {
                    launchResidentApp();
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
            LOGINFO("[Jose] Launched residentapp . status : %d, , msg %s ",
                    m_isResAppRunning, C_STR(message));
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

            LOGINFO("[Jose] version %s " MY_VERSION);
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
                LOGINFO("[Jose] Subscribed to Low Memory events..  status %b", (status == Core::ERROR_NONE ? "Success" : "Failed"));

                status = m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONKEY_EVENT), &MemMonitor::pluginEventHandler, this);
                LOGINFO("[Jose] Subscribed to onKey events..  status %b", (status == Core::ERROR_NONE ? "Success" : "Failed"));

                status = m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONLAUNCHED_EVENT), &MemMonitor::pluginEventHandler, this);
                LOGINFO("[Jose] Subscribed to onLaunched events..  status %b", (status == Core::ERROR_NONE ? "Success" : "Failed"));

                status = m_remoteObject->Subscribe<JsonObject>(THUNDER_TIMEOUT, _T(SUBSCRIPTION_ONDESTROYED_EVENT), &MemMonitor::pluginEventHandler, this);
                LOGINFO("[Jose] Subscribed to ondestroyed events..  status %b", (status == Core::ERROR_NONE ? "Success" : "Failed"));

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