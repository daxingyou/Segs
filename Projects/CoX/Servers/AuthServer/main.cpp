/*
 * Super Entity Game Server Project
 * http://segs.sf.net/
 * Copyright (c) 2006 - 2016 Super Entity Game Server Team (see Authors.txt)
 * This software is licensed! (See License.txt for details)
 *

 */
//#define ACE_NTRACE 0
#include "Servers/HandlerLocator.h"
#include "Servers/MessageBus.h"
#include "Servers/MessageBusEndpoint.h"
#include "Servers/InternalEvents.h"
#include "SEGSTimer.h"
#include "Settings.h"
#include "Logging.h"
#include "version.h"
//////////////////////////////////////////////////////////////////////////

#include "AuthServer.h"
#include "Servers/MapServer/MapServer.h"
#include "Servers/GameServer/GameServer.h"
#include "Servers/GameDatabase/GameDBSync.h"
#include "Servers/AuthDatabase/AuthDBSync.h"
//////////////////////////////////////////////////////////////////////////

#include <ace/ACE.h>
#include <ace/Singleton.h>
#include <ace/Log_Record.h>
#include <ace/INET_Addr.h>

#include <ace/Reactor.h>
#include <ace/TP_Reactor.h>
#include <ace/INET_Addr.h>
#include <ace/OS_main.h> //Included to enable file logging
#include <ace/streams.h> //Included to enable file logging

#include <QtCore/QCoreApplication>
#include <QtCore/QLoggingCategory>
#include <QtCore/QSettings>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMutexLocker>
#include <stdlib.h>
#include <memory>
#define TIMED_LOG(x,msg) {\
    QDebug log(qDebug());\
    log << msg << "..."; \
    QElapsedTimer timer;\
    timer.start();\
    x;\
    log << "done in"<<float(timer.elapsed())/1000.0f<<"s";\
}
namespace
{
static bool s_event_loop_is_done=false; //!< this is set to true when ace reactor is finished.
static std::unique_ptr<AuthServer> g_auth_server; // this is a global for now.
static std::unique_ptr<GameServer> g_game_server;
static std::unique_ptr<MapServer> g_map_server;
static std::unique_ptr<MessageBus> g_message_bus;
struct MessageBusMonitor : public EventProcessor
{
    MessageBusEndpoint m_endpoint;
    MessageBusMonitor() : m_endpoint(*this)
    {
        m_endpoint.subscribe(MessageBus::ALL_EVENTS);
        activate();
    }

    // EventProcessor interface
public:
    void dispatch(SEGSEvent *ev)
    {
        switch(ev->type())
        {
        case Internal_EventTypes::evServiceStatus:
            on_service_status(static_cast<ServiceStatusMessage *>(ev));
            break;
        default:
            ;//qDebug() << "Unhandled message bus monitor command" <<ev->info();
        }
    }
private:
    void on_service_status(ServiceStatusMessage *msg);
};
static std::unique_ptr<MessageBusMonitor> s_bus_monitor;
static void shutDownServers()
{
    if (GlobalTimerQueue::instance()->thr_count())
    {
        GlobalTimerQueue::instance()->deactivate();
    }
    if(g_game_server && g_game_server->thr_count())
    {
        g_game_server->ShutDown();
    }
    if(g_map_server && g_map_server->thr_count())
    {
        g_map_server->ShutDown();
    }
    if(g_auth_server && g_auth_server->thr_count())
    {
        g_auth_server->ShutDown();
    }
    if(s_bus_monitor && s_bus_monitor->thr_count())
    {
        s_bus_monitor->putq(new SEGSEvent(SEGS_EventTypes::evFinish));
    }
    if(g_message_bus && g_message_bus->thr_count())
    {
        g_message_bus->putq(new SEGSEvent(SEGS_EventTypes::evFinish));
    }

    s_event_loop_is_done = true;
}
void MessageBusMonitor::on_service_status(ServiceStatusMessage *msg)
{
    if (msg->m_data.status_value != 0)
    {
        qCritical().noquote() << msg->m_data.status_message;
        shutDownServers();
    }
    else
        qInfo() << msg->m_data.status_message;
}
void break_func()
{
    shutDownServers();
}
// this event stops main processing loop of the whole server
class ServerStopper : public ACE_Event_Handler
{
    void(*shut_down_func)();
public:
    ServerStopper(int signum, void(*func)()) // when instantiated adds itself to current reactor
    {
        ACE_Reactor::instance()->register_handler(signum, this);
        shut_down_func = func;
}
    // Called when object is signaled by OS.
    int handle_signal(int, siginfo_t */*s_i*/, ucontext_t */*u_c*/)
    {
        shutDownServers();
        if (shut_down_func)
            shut_down_func();
        return 0;
    }
};
bool CreateServers()
{
    static ReloadConfigMessage reload_config;
    GlobalTimerQueue::instance()->activate();

    TIMED_LOG(
                {
                    g_message_bus.reset(new MessageBus);
                    HandlerLocator::setMessageBus(g_message_bus.get());
                    g_message_bus->ReadConfigAndRestart();
                }
                ,"Creating message bus");
    TIMED_LOG(s_bus_monitor.reset(new MessageBusMonitor),"Starting message bus monitor");

    TIMED_LOG(startAuthDBSync(),"Starting auth db service");
    TIMED_LOG({
                  g_auth_server.reset(new AuthServer);
                  g_auth_server->activate();
              },"Starting auth service");
//    AdminServer::instance()->ReadConfig();
//    AdminServer::instance()->Run();
    TIMED_LOG(startGameDBSync(1),"Starting game(1) db service");
    TIMED_LOG({
                  g_game_server.reset(new GameServer(1));
                  g_game_server->activate();
              },"Starting game(1) server");
    TIMED_LOG({
                  g_map_server.reset(new MapServer(1));
                  g_map_server->sett_game_server_owner(1);
                  g_map_server->activate();
              },"Starting map server");

    qDebug() << "Asking AuthServer to load configuration and begin listening for external connections.";
    g_auth_server->putq(reload_config.shallow_copy());
    qDebug() << "Asking GameServer(0) to load configuration on begin listening for external connections.";
    g_game_server->putq(reload_config.shallow_copy());
    qDebug() << "Asking MapServer to load configuration on begin listening for external connections.";
    g_map_server->putq(reload_config.shallow_copy());

//    server_manger->AddGameServer(game_instance);
    return true;
}
QMutex log_mutex;

void segsLogMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QMutexLocker lock(&log_mutex);
    QFile segs_log_target;
    segs_log_target.setFileName("output.log");
    if (!segs_log_target.open(QFile::WriteOnly | QFile::Append))
    {
        fprintf(stderr,"Failed to open log file in write mode, will procede with console only logging");
    }
    QTextStream fileLog(&segs_log_target);
    QByteArray localMsg = msg.toLocal8Bit();
    QString message;
    switch (type)
    {
        case QtDebugMsg:
            message = QString("Debug   : %1").arg(localMsg.constData());
            break;
        case QtInfoMsg:
            // no prefix for informational messages
            message = localMsg.constData();
            break;
        case QtWarningMsg:
            message = QString("Warning : %1").arg(localMsg.constData());
            break;
        case QtCriticalMsg:
            message = QString("Critical: %1").arg(localMsg.constData());
            break;
        case QtFatalMsg:
            message = QString("Fatal error %1").arg(localMsg.constData());
    }
    fprintf(stdout, "%s\n", qPrintable(message));
    if (type != QtInfoMsg && segs_log_target.isOpen())
    {
        fileLog << message << "\n";
        fileLog.flush();
    }
    if (type == QtFatalMsg)
    {
        fflush(stdout);
        abort();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
} // End of anonymous namespace
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ACE_INT32 ACE_TMAIN (int argc, ACE_TCHAR *argv[])
{
    setLoggingFilter(); // Set QT Logging filters
    qInstallMessageHandler(segsLogMessageOutput);
    QCoreApplication q_app(argc,argv);
    QCoreApplication::setOrganizationDomain("segs.nemerle.eu");
    QCoreApplication::setOrganizationName("SEGS Project");
    QCoreApplication::setApplicationName("segs_server");
    QCoreApplication::setApplicationVersion(VersionInfo::getAuthVersion());

    QCommandLineParser parser;
    parser.setApplicationDescription("SEGS - CoX server");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
                          {{"f","config"},
                           "Use the provided settings file, default value is <settings.cfg>",
                           "filename","settings.cfg"}
                      });
    parser.process(q_app);
    if(parser.isSet("help")||parser.isSet("version"))
        return 0;

    Settings::setSettingsPath(parser.value("config")); // set settings.cfg from args

    ACE_Sig_Set interesting_signals;
    interesting_signals.sig_add(SIGINT);
    interesting_signals.sig_add(SIGHUP);

    const size_t N_THREADS = 1;
    //std::unique_ptr<ACE_Reactor> old_instance(ACE_Reactor::instance(&new_reactor)); // this will delete old instance when app finishes

    ServerStopper st(SIGINT,break_func); // it'll register itself with current reactor, and shut it down on sigint
    ACE_Reactor::instance()->register_handler(interesting_signals,&st);

    // Print out startup copyright messages

    qInfo().noquote() << VersionInfo::getCopyright();
    qInfo().noquote() << VersionInfo::getAuthVersion();

    qInfo().noquote() << "main";

    bool no_err = CreateServers();
    if(!no_err)
    {
        ACE_Reactor::instance()->end_reactor_event_loop();
        return -1;
    }
    // process all queued qt messages here.
    ACE_Time_Value event_processing_delay(0,1000*5);
    while( !s_event_loop_is_done )
    {
        ACE_Reactor::instance()->handle_events(&event_processing_delay);
        QCoreApplication::processEvents();
    }
    GlobalTimerQueue::instance()->wait();
    g_game_server->wait();
    g_map_server->wait();
    g_auth_server->wait();
    s_bus_monitor->wait();
    g_message_bus->wait();
    g_game_server.reset();
    g_map_server.reset();
    g_auth_server.reset();
    s_bus_monitor.reset();
    g_message_bus.reset();
    ACE_Reactor::instance()->handle_events(&event_processing_delay);
    ACE_Reactor::instance()->remove_handler(interesting_signals);

    ACE_Reactor::end_event_loop();
    return 0;
}
